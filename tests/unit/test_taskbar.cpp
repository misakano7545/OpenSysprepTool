// M10：任务栏固定 / 取消固定（SC 的 Cb_Locktaskbar / Cb_Unlocktaskbar）
//
// 这两个开关在 SC 里是用来推自家推广快捷方式的，属于宪章 §7 明令禁止的行为。
// 本实现只提供**机制**：用户自己在配置里列出要固定/取消固定的文件，程序
// 绝不预置任何条目。所以测试既验行为，也钉住「没有内置条目」这一点。

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/prepare.hpp"

using namespace osp;

namespace {

osp::prepare::Options pinnedOptions(const std::filesystem::path& fakeRoot)
{
    osp::prepare::Options options = osp::prepare::defaultOptions();
    options.systemDrive = reinterpret_cast<const char*>(fakeRoot.u8string().c_str());
    options.defaultUserProfile =
        reinterpret_cast<const char*>((fakeRoot / "Users" / "Default").u8string().c_str());
    // 关掉一切会碰真实系统的默认项，只留下本测试关心的动作
    options.windowsDir.clear();
    return options;
}

const osp::prepare::Action* findById(const osp::prepare::Plan& plan, const std::string& id)
{
    const auto it =
        std::find_if(plan.actions.begin(), plan.actions.end(), [&id](const osp::prepare::Action& action) {
            return action.id == id;
        });
    return it == plan.actions.end() ? nullptr : &*it;
}

std::filesystem::path makeTempRoot()
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "ospt-taskbar-unit";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "Users" / "Default", ec);
    return root;
}

} // namespace

TEST_CASE("prepare: 任务栏固定只写用户列出的文件，不预置任何条目")
{
    const std::filesystem::path root = makeTempRoot();
    const std::filesystem::path source = root / "我的工具.lnk";
    {
        std::ofstream out(source, std::ios::binary | std::ios::trunc);
        out << "shortcut bytes";
    }

    osp::config::Config cfg;
    cfg.systemInfo.bootMenuTimeoutSec = 0; // 不让 BCD 动作混进来
    cfg.optimization.lockTaskbarFiles = {std::string(reinterpret_cast<const char*>(source.u8string().c_str()))
    };

    const osp::prepare::Plan plan = osp::prepare::build(cfg, pinnedOptions(root));
    REQUIRE(plan.ok);

    // 清单里只有一个文件 → 计划里也只能有一个固定动作
    std::size_t pinCount = 0;
    for (const auto& action : plan.actions)
    {
        if (action.kind == osp::prepare::ActionKind::FileCopy)
        {
            ++pinCount;
        }
    }
    CHECK(pinCount == 1);

    // 按 sourceField + kind 找，不靠 id 里的中文拼写（id 还带 basename，靠它断言太脆）
    const osp::prepare::Action* pin = nullptr;
    for (const auto& action : plan.actions)
    {
        if (action.kind == osp::prepare::ActionKind::FileCopy &&
            action.sourceField == "optimization.lockTaskbarFiles")
        {
            pin = &action;
            break;
        }
    }
    REQUIRE(pin != nullptr);
    CHECK(pin->kind == osp::prepare::ActionKind::FileCopy);
    // 落点必须是「默认用户」的 User Pinned\TaskBar，新用户建配置目录时才会被复制过去
    CHECK(pin->destination.find("User Pinned") != std::string::npos);
    CHECK(pin->destination.find("TaskBar") != std::string::npos);
    CHECK(
        pin->destination.rfind(
            reinterpret_cast<const char*>((root / "Users" / "Default").u8string().c_str()), 0
        ) == 0
    );

    std::filesystem::remove_all(root);
}

TEST_CASE("prepare: 任务栏固定真执行后文件到位，取消固定后文件消失")
{
    const std::filesystem::path root = makeTempRoot();
    const std::filesystem::path source = root / "payload.lnk";
    {
        std::ofstream out(source, std::ios::binary | std::ios::trunc);
        out << "shortcut bytes";
    }
    const std::string sourcePath(reinterpret_cast<const char*>(source.u8string().c_str()));

    osp::config::Config cfg;
    cfg.systemInfo.bootMenuTimeoutSec = 0;
    cfg.optimization.lockTaskbarFiles = {sourcePath};

    const osp::prepare::Plan plan = osp::prepare::build(cfg, pinnedOptions(root));
    REQUIRE(plan.ok);

    const osp::prepare::ExecResult executed = osp::prepare::execute(plan);
    CHECK(executed.ok);
    CHECK(executed.applied == plan.actions.size());

    const std::filesystem::path pinned = root / "Users" / "Default" / "AppData" / "Roaming" / "Microsoft" /
        "Internet Explorer" / "Quick Launch" / "User Pinned" / "TaskBar" / "payload.lnk";
    CHECK(std::filesystem::exists(pinned));

    // 取消固定：只删默认用户里的同名快捷方式
    osp::config::Config unpinCfg;
    unpinCfg.systemInfo.bootMenuTimeoutSec = 0;
    unpinCfg.optimization.unlockTaskbarFiles = {sourcePath};
    const osp::prepare::Plan unpinPlan = osp::prepare::build(unpinCfg, pinnedOptions(root));
    REQUIRE(unpinPlan.ok);

    const osp::prepare::Action* unpin = nullptr;
    for (const auto& action : unpinPlan.actions)
    {
        if (action.kind == osp::prepare::ActionKind::FileDelete &&
            action.sourceField == "optimization.unlockTaskbarFiles")
        {
            unpin = &action;
            break;
        }
    }
    REQUIRE(unpin != nullptr);
    CHECK(unpin->kind == osp::prepare::ActionKind::FileDelete);

    const osp::prepare::ExecResult unexecuted = osp::prepare::execute(unpinPlan);
    CHECK(unexecuted.ok);
    CHECK_FALSE(std::filesystem::exists(pinned));

    // 再执行一次也必须是成功：目标本来就不在，「取消固定」要的就是它不在
    const osp::prepare::ExecResult again = osp::prepare::execute(unpinPlan);
    CHECK(again.ok);

    std::filesystem::remove_all(root);
}

TEST_CASE("prepare: 源文件不存在时只告警，不生成动作")
{
    const std::filesystem::path root = makeTempRoot();
    osp::config::Config cfg;
    cfg.systemInfo.bootMenuTimeoutSec = 0;
    cfg.optimization.lockTaskbarFiles = {
        std::string(reinterpret_cast<const char*>(root.u8string().c_str())) + std::string("\\no-such.lnk"),
    };

    const osp::prepare::Plan plan = osp::prepare::build(cfg, pinnedOptions(root));
    REQUIRE(plan.ok);
    for (const auto& action : plan.actions)
    {
        CHECK(action.kind != osp::prepare::ActionKind::FileCopy);
    }
    bool warned = false;
    for (const std::string& warning : plan.warnings)
    {
        if (warning.find("源文件不存在") != std::string::npos)
        {
            warned = true;
        }
    }
    CHECK(warned);

    std::filesystem::remove_all(root);
}

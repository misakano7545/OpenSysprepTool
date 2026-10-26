// M10：宽带连接（SC 的 Cb_CreateConnection）
//
// 两个模式对应 SC 的两个单选：
//   Rb_Silence_nc「静默创建[无界面]」 -> 封装前把 PPPoE 条目写进默认用户电话簿
//   Rb_Show_nc   「倒计时创建[有界面]」 -> 首次进桌面跑 rasphone -a 弹创建向导
//
// 静默模式依赖本机有没有 PPPoE 设备（WAN Miniport (PPPOE)）。CI 里的虚拟机
// 可能没有，所以断言按「有设备则验证内容，无设备则验证如实报错」两条路走 ——
// 不能因为环境缺设备就把测试写成必然跳过。

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/config.hpp"
#include "core/pppoe.hpp"
#include "core/prepare.hpp"
#include "core/unattend.hpp"

using namespace osp;

namespace {

const osp::prepare::Action* findPppoe(const osp::prepare::Plan& plan)
{
    for (const auto& action : plan.actions)
    {
        if (action.kind == osp::prepare::ActionKind::PppoeCreate)
        {
            return &action;
        }
    }
    return nullptr;
}

std::filesystem::path makeTempRoot()
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "ospt-pppoe-unit";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "Users" / "Default", ec);
    return root;
}

osp::prepare::Options optionsFor(const std::filesystem::path& root)
{
    osp::prepare::Options options = osp::prepare::defaultOptions();
    options.systemDrive = reinterpret_cast<const char*>(root.u8string().c_str());
    options.defaultUserProfile =
        reinterpret_cast<const char*>((root / "Users" / "Default").u8string().c_str());
    options.windowsDir.clear();
    return options;
}

} // namespace

TEST_CASE("prepare: 静默模式生成 PPPoE 动作，落点是默认用户电话簿")
{
    const std::filesystem::path root = makeTempRoot();

    osp::config::Config cfg;
    cfg.systemInfo.bootMenuTimeoutSec = 0;
    cfg.firstLogon.createConnection = true;
    cfg.firstLogon.connectionMode = "silent";
    cfg.firstLogon.connectionName = "我的宽带";

    const osp::prepare::Plan plan = osp::prepare::build(cfg, optionsFor(root));
    REQUIRE(plan.ok);

    const osp::prepare::Action* action = findPppoe(plan);
    REQUIRE(action != nullptr);
    CHECK(action->connName == "我的宽带");
    CHECK(action->destination.find("Microsoft") != std::string::npos);
    CHECK(action->destination.find("Pbk") != std::string::npos);
    CHECK(action->destination.find("rasphone.pbk") != std::string::npos);
    // 落点必须在默认用户下，不能是当前用户
    CHECK(action->destination.find("Users") != std::string::npos);

    std::filesystem::remove_all(root);
}

TEST_CASE("prepare: 展示模式不生成封装前动作（交给首次进桌面）")
{
    const std::filesystem::path root = makeTempRoot();

    osp::config::Config cfg;
    cfg.systemInfo.bootMenuTimeoutSec = 0;
    cfg.firstLogon.createConnection = true;
    cfg.firstLogon.connectionMode = "show";

    const osp::prepare::Plan plan = osp::prepare::build(cfg, optionsFor(root));
    REQUIRE(plan.ok);
    CHECK(findPppoe(plan) == nullptr);

    std::filesystem::remove_all(root);
}

TEST_CASE("unattend: 展示模式写入 rasphone -a，静默模式不写")
{
    osp::config::Config cfg;
    cfg.systemInfo.computerName = "OSPT-TEST";
    cfg.firstLogon.createConnection = true;
    cfg.firstLogon.connectionMode = "show";
    cfg.firstLogon.connectionName = "宽带连接";

    const osp::unattend::GenResult shown = osp::unattend::generate(cfg);
    REQUIRE(shown.ok);
    CHECK(shown.xml.find("FirstLogonCommands") != std::string::npos);
    CHECK(shown.xml.find("rasphone.exe -a") != std::string::npos);
    CHECK(shown.xml.find("宽带连接") != std::string::npos);

    // 静默模式已经建好了连接，首次进桌面不该再弹向导
    osp::config::Config silentCfg;
    silentCfg.systemInfo.computerName = "OSPT-TEST";
    silentCfg.firstLogon.createConnection = true;
    silentCfg.firstLogon.connectionMode = "silent";
    const osp::unattend::GenResult silent = osp::unattend::generate(silentCfg);
    REQUIRE(silent.ok);
    CHECK(silent.xml.find("rasphone") == std::string::npos);
}

TEST_CASE("pppoe: 创建后读回同一条目，删除后读不到")
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ospt-pppoe-probe";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const std::string pbk = reinterpret_cast<const char*>((dir / "probe.pbk").u8string().c_str());

    std::string error;
    const bool created = osp::netcfg::createPppoeEntry(pbk, "OSPT 单元测试", "user1", "pass1", error);
    if (!created)
    {
        // 没有 PPPoE 设备的机器上，必须给出明确原因而不是静默失败
        CHECK(!error.empty());
        CHECK(error.find("PPPoE") != std::string::npos);
        std::filesystem::remove_all(dir, ec);
        return;
    }

    CHECK(std::filesystem::exists(
        std::filesystem::path(reinterpret_cast<const char*>((dir / "probe.pbk").u8string().c_str()))
    ));

    std::string readUser;
    std::string readError;
    CHECK(osp::netcfg::readPppoeEntry(pbk, "OSPT 单元测试", readUser, readError));

    // 删掉之后必须读不到
    std::string deleteError;
    CHECK(osp::netcfg::deletePppoeEntry(pbk, "OSPT 单元测试", deleteError));
    readUser.clear();
    readError.clear();
    CHECK_FALSE(osp::netcfg::readPppoeEntry(pbk, "OSPT 单元测试", readUser, readError));

    // 重复删除也算成功
    deleteError.clear();
    CHECK(osp::netcfg::deletePppoeEntry(pbk, "OSPT 单元测试", deleteError));

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("pppoe: 空连接名直接拒绝")
{
    std::string error;
    CHECK_FALSE(osp::netcfg::createPppoeEntry("probe.pbk", "", "u", "p", error));
    CHECK(!error.empty());
}

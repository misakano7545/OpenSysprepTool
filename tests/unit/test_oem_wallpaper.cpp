// M10：OEM 壁纸 / 部署图片（SC 的 SA_Lab_oembgpic / SA_Lab_oemDeploy）
//
// 这两项在 SC 里是「遇品牌电脑则设置相应品牌桌面壁纸 / 部署图片」。本仓库
// 不含任何品牌素材（宪章 §5、§7），只实现机制：图片路径由用户提供，
// 程序负责复制进映像并把默认用户的壁纸设置改掉。

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/config.hpp"
#include "core/win7.hpp"
#include "core/prepare.hpp"
#include "core/runplan.hpp"

using namespace osp;

namespace {

std::filesystem::path makeTempRoot()
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "ospt-oem-wallpaper-unit";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "Windows", ec);
    std::filesystem::create_directories(root / "Users" / "Default", ec);
    return root;
}

// MSVC 下 path::u8string() 返回 std::u8string，且这里必须**先落到 std::string**：
// 直接 reinterpret_cast<const char*>(p.u8string().c_str()) 会拿到已析构临时对象的指针
// （悬垂），本机可能恰好还能用，换台机器就变成乱码或抛转换异常。
std::string u8(const std::filesystem::path& path)
{
    return std::string(reinterpret_cast<const char*>(path.u8string().c_str()));
}

osp::prepare::Options optionsFor(const std::filesystem::path& root)
{
    osp::prepare::Options options = osp::prepare::defaultOptions();
    options.systemDrive = u8(root);
    options.windowsDir = u8(root / "Windows");
    options.defaultUserProfile = u8(root / "Users" / "Default");
    return options;
}

} // namespace

TEST_CASE("prepare: OEM 壁纸生成复制 + 三项桌面设置")
{
    const std::filesystem::path root = makeTempRoot();
    // 必须用 pathFromUtf8 而不是 `root / "中文名"`：后者走 path 的隐式窄字符构造，
    // 会按 CP_ACP(936) 解释 UTF-8 字节，文件名当场变成乱码。
    const std::filesystem::path image = root / osp::win7::pathFromUtf8("品牌壁纸.bmp");
    {
        std::ofstream out(image, std::ios::binary | std::ios::trunc);
        out << "BM fake bitmap";
    }

    osp::config::Config cfg;
    cfg.systemInfo.bootMenuTimeoutSec = 0;
    cfg.oem.wallpaper = u8(image);

    const osp::prepare::Plan plan = osp::prepare::build(cfg, optionsFor(root));
    REQUIRE(plan.ok);

    const osp::prepare::Action* copy = nullptr;
    std::vector<const osp::prepare::Action*> hiveWrites;
    for (const auto& action : plan.actions)
    {
        if (action.id == "oem.wallpaper.copy")
        {
            copy = &action;
        }
        else if (action.kind == osp::prepare::ActionKind::HiveSet && action.sourceField == "oem.wallpaper")
        {
            hiveWrites.push_back(&action);
        }
    }

    REQUIRE(copy != nullptr);
    CHECK(copy->kind == osp::prepare::ActionKind::FileCopy);
    // 目标必须在映像的 Web\Wallpaper 下，且保留原文件名
    CHECK(copy->destination.find("Web") != std::string::npos);
    CHECK(copy->destination.find("Wallpaper") != std::string::npos);
    // 目标文件名里的中文必须原样保留（UTF-8 字节逐一比对）
    CHECK(copy->destination.find("品牌壁纸.bmp") != std::string::npos);

    // 三项设置：Wallpaper / WallpaperStyle / TileWallpaper
    REQUIRE(hiveWrites.size() == 3);
    bool hasWallpaper = false;
    bool hasStyle = false;
    bool hasTile = false;
    for (const auto* action : hiveWrites)
    {
        CHECK(action->hiveFile.find("NTUSER.DAT") != std::string::npos);
        CHECK(action->key == "Control Panel\\Desktop");
        if (action->valueName == "Wallpaper")
        {
            hasWallpaper = true;
            CHECK(action->valueType == "expandSz");
            CHECK(action->text == copy->destination);
        }
        else if (action->valueName == "WallpaperStyle")
        {
            hasStyle = true;
            CHECK(action->text == "10"); // 10 = 填充
        }
        else if (action->valueName == "TileWallpaper")
        {
            hasTile = true;
            CHECK(action->text == "0");
        }
    }
    CHECK(hasWallpaper);
    CHECK(hasStyle);
    CHECK(hasTile);

    std::filesystem::remove_all(root);
}

TEST_CASE("prepare: OEM 壁纸文件不存在时只告警，不生成动作")
{
    const std::filesystem::path root = makeTempRoot();
    osp::config::Config cfg;
    cfg.systemInfo.bootMenuTimeoutSec = 0;
    cfg.oem.wallpaper = u8(root / osp::win7::pathFromUtf8("没有这张图.bmp"));

    const osp::prepare::Plan plan = osp::prepare::build(cfg, optionsFor(root));
    REQUIRE(plan.ok);
    for (const auto& action : plan.actions)
    {
        CHECK(action.sourceField != "oem.wallpaper");
    }
    bool warned = false;
    for (const std::string& warning : plan.warnings)
    {
        if (warning.find("OEM 壁纸") != std::string::npos)
        {
            warned = true;
        }
    }
    CHECK(warned);

    std::filesystem::remove_all(root);
}

TEST_CASE("runplan: 部署图片在没显式指定时回落到 OEM 那张")
{
    osp::config::Config cfg;
    cfg.systemInfo.bootMenuTimeoutSec = 0;
    cfg.oem.deployImage = "D:\\skin\\bg.jpg";

    osp::runplan::DriveTable drives;
    const osp::runplan::PlanResult fallback = osp::runplan::build(cfg, drives);
    REQUIRE(fallback.ok);
    CHECK(fallback.presentation.backgroundImage == "D:\\skin\\bg.jpg");

    // 显式指定时 OEM 那张不许覆盖它
    cfg.deploy.backgroundImage = "D:\\mine\\bg.jpg";
    const osp::runplan::PlanResult explicitOne = osp::runplan::build(cfg, drives);
    REQUIRE(explicitOne.ok);
    CHECK(explicitOne.presentation.backgroundImage == "D:\\mine\\bg.jpg");
}

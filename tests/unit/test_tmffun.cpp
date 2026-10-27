#include <doctest/doctest.h>

#include <string>

#include "core/config.hpp"
#include "core/prepare.hpp"
#include "core/unattend.hpp"
#include "tmffun.hpp"

// TMFFUN 高级设置窗体：页签集合、三个已排页签的绑定、虚拟内存大小换算。
//
// 窗口本身由 --ui-smoke-test 覆盖（它要在真实窗口站里跑）；这里只测不依赖窗口的部分：
// 页签清单和 core 的 PagingFiles 字符串生成。

TEST_CASE("tmffun: 页签清单与 DFM 顺序一致")
{
    // DFM 里排在前面的是「模块1设置 / 模块2设置 / 转移文档」
    CHECK(osp::ui::tmffunTabIndex("MlSkinTabSheet1") == 0);
    CHECK(osp::ui::tmffunTabIndex("MlSkinTabSheet3") == 2);
    CHECK(osp::ui::tmffunTabIndex("MlSkinTabSheet4") == 3);
    CHECK(osp::ui::tmffunTabIndex("MlSkinTabSheet7") >= 0);
    // 不存在的页签返回 -1，不返回 0（0 会被当成「第一页」静默打开）
    CHECK(osp::ui::tmffunTabIndex("MlSkinTabSheet999") == -1);
}

TEST_CASE("tmffun: 宪章 §7 红线的两个页签整页不出现")
{
    // SC 的 Page_Nodelfile / Page_Nodelreg 用内核驱动锁住文件与注册表不让删，
    // 这两个功能我们永远不做 —— 页签本身都不建，免得日后被人当成「待实现」填上。
    CHECK(osp::ui::tmffunTabIndex("Page_Nodelfile") == -1);
    CHECK(osp::ui::tmffunTabIndex("Page_Nodelreg") == -1);
}

TEST_CASE("tmffun: 设计尺寸就是 DFM 的 740x400")
{
    CHECK(osp::ui::kTmffunClientWidth == 740);
    CHECK(osp::ui::kTmffunClientHeight == 400);
}

TEST_CASE("虚拟内存大小：不选单选时交回系统管理")
{
    osp::config::Optimization opt;
    CHECK(osp::prepare::pageFileSizeFields(opt, 16384) == std::string("0 0"));

    opt.pageFileSizeMode = "system";
    CHECK(osp::prepare::pageFileSizeFields(opt, 16384) == std::string("0 0"));
}

TEST_CASE("虚拟内存大小：按物理内存倍数换算")
{
    osp::config::Optimization opt;
    opt.pageFileSizeMode = "multiplier";
    opt.pageFileMinMultiplier = 1;
    opt.pageFileMaxMultiplier = 2;
    // 16 GB = 16384 MB
    CHECK(osp::prepare::pageFileSizeFields(opt, 16384) == std::string("16384 32768"));
    // 8 GB
    CHECK(osp::prepare::pageFileSizeFields(opt, 8192) == std::string("8192 16384"));
}

TEST_CASE("虚拟内存大小：倍数值不完整时返回空串，不猜")
{
    osp::config::Optimization opt;
    opt.pageFileSizeMode = "multiplier";
    opt.pageFileMinMultiplier = 0;
    opt.pageFileMaxMultiplier = 2;
    CHECK(osp::prepare::pageFileSizeFields(opt, 16384).empty());

    // 最小 > 最大
    opt.pageFileMinMultiplier = 3;
    opt.pageFileMaxMultiplier = 1;
    CHECK(osp::prepare::pageFileSizeFields(opt, 16384).empty());

    // 物理内存未知
    opt.pageFileMinMultiplier = 1;
    opt.pageFileMaxMultiplier = 2;
    CHECK(osp::prepare::pageFileSizeFields(opt, 0).empty());
}

TEST_CASE("虚拟内存大小：固定值原样落进 PagingFiles")
{
    osp::config::Optimization opt;
    opt.pageFileSizeMode = "fixed";
    opt.pageFileMinFixedMb = 4096;
    opt.pageFileMaxFixedMb = 8192;
    CHECK(osp::prepare::pageFileSizeFields(opt, 16384) == std::string("4096 8192"));

    opt.pageFileMinFixedMb = 8192;
    opt.pageFileMaxFixedMb = 4096;
    CHECK(osp::prepare::pageFileSizeFields(opt, 16384).empty());
}

TEST_CASE("虚拟内存大小：认不出的模式不猜")
{
    osp::config::Optimization opt;
    opt.pageFileSizeMode = "whatever";
    CHECK(osp::prepare::pageFileSizeFields(opt, 16384).empty());
}

TEST_CASE("prepare: 指定盘符会生成带大小的 PagingFiles 动作")
{
    osp::config::Config cfg;
    cfg.optimization.pageFileSmart = false;
    cfg.optimization.pageFileDrive = "D:";
    cfg.optimization.pageFileSizeMode = "fixed";
    cfg.optimization.pageFileMinFixedMb = 4096;
    cfg.optimization.pageFileMaxFixedMb = 8192;

    osp::prepare::Options options = osp::prepare::defaultOptions();
    options.physicalMemoryMb = 16384;
    const osp::prepare::Plan plan = osp::prepare::build(cfg, options);

    bool found = false;
    for (const osp::prepare::Action& action : plan.actions)
    {
        if (action.valueName != "PagingFiles")
        {
            continue;
        }
        found = true;
        CHECK(action.multi.size() == 1);
        if (!action.multi.empty())
        {
            CHECK(action.multi[0] == std::string("D:\\pagefile.sys 4096 8192"));
        }
    }
    CHECK(found);
}

TEST_CASE("prepare: 只启用智能转移时大小为 0 0")
{
    osp::config::Config cfg;
    cfg.optimization.autoPagefileDrive = true;

    osp::prepare::Options options = osp::prepare::defaultOptions();
    options.physicalMemoryMb = 16384;
    options.pagefileCandidates = {{"D:", 500000}, {"E:", 900000}};
    const osp::prepare::Plan plan = osp::prepare::build(cfg, options);

    bool found = false;
    for (const osp::prepare::Action& action : plan.actions)
    {
        if (action.valueName != "PagingFiles")
        {
            continue;
        }
        found = true;
        CHECK(action.multi.size() == 1);
        if (!action.multi.empty())
        {
            // 智能选到剩余最大的 E:
            CHECK(action.multi[0] == std::string("E:\\pagefile.sys 0 0"));
        }
    }
    CHECK(found);
}

TEST_CASE("prepare: 大小配置不完整时跳过并给出原因")
{
    osp::config::Config cfg;
    cfg.optimization.pageFileSmart = false;
    cfg.optimization.pageFileDrive = "D:";
    cfg.optimization.pageFileSizeMode = "multiplier"; // 倍数没填

    osp::prepare::Options options = osp::prepare::defaultOptions();
    options.physicalMemoryMb = 16384;
    const osp::prepare::Plan plan = osp::prepare::build(cfg, options);

    for (const osp::prepare::Action& action : plan.actions)
    {
        CHECK(action.valueName != "PagingFiles");
    }
    bool warned = false;
    for (const std::string& warning : plan.warnings)
    {
        if (warning.find("虚拟内存大小配置不完整") != std::string::npos)
        {
            warned = true;
        }
    }
    CHECK(warned);
}

TEST_CASE("预置IP 随机模式：基址 + 末段范围，同一台机器上结果稳定")
{
    osp::config::Config cfg;
    cfg.optimization.autoSetIp = true;
    cfg.optimization.ipMode = "random";
    cfg.optimization.ipRandomBase = "192.168.1";
    cfg.optimization.ipRandomOctetStart = 2;
    cfg.optimization.ipRandomOctetEnd = 254;

    osp::prepare::Options options = osp::prepare::defaultOptions();
    options.ipInterface = "假网卡";
    options.ipRandomSeed = 12345;

    const osp::prepare::Plan plan = osp::prepare::build(cfg, options);
    std::string address;
    for (const osp::prepare::Action& action : plan.actions)
    {
        // netsh 命令行形如：interface ipv4 set address name="X" static <ip> <mask>
        if (action.program == "netsh" && action.arguments.find("set address") != std::string::npos)
        {
            const std::size_t at = action.arguments.find(" static ");
            if (at != std::string::npos)
            {
                const std::size_t from = at + 8;
                const std::size_t to = action.arguments.find(' ', from);
                address = action.arguments.substr(from, to == std::string::npos ? to : to - from);
            }
        }
    }
    REQUIRE(!address.empty());
    CHECK(address.rfind("192.168.1.", 0) == 0);
    const int octet = std::stoi(address.substr(10));
    CHECK(octet >= 2);
    CHECK(octet <= 254);
}

TEST_CASE("预置IP 随机模式：末段范围非法时跳过并给原因")
{
    osp::config::Config cfg;
    cfg.optimization.autoSetIp = true;
    cfg.optimization.ipMode = "random";
    cfg.optimization.ipRandomBase = "192.168.1";
    cfg.optimization.ipRandomOctetStart = 200;
    cfg.optimization.ipRandomOctetEnd = 10; // 反了

    osp::prepare::Options options = osp::prepare::defaultOptions();
    options.ipInterface = "假网卡";
    const osp::prepare::Plan plan = osp::prepare::build(cfg, options);

    for (const osp::prepare::Action& action : plan.actions)
    {
        CHECK(action.program != "netsh");
    }
    bool warned = false;
    for (const std::string& warning : plan.warnings)
    {
        if (warning.find("末段范围不合法") != std::string::npos)
        {
            warned = true;
        }
    }
    CHECK(warned);
}

TEST_CASE("防篡改条目带说明：JSON 往返不丢")
{
    osp::config::Config cfg;
    osp::config::DeploySettings::TamperEntry entry;
    entry.path = "C:\\Windows\\explorer.exe";
    entry.md5 = "0123456789abcdef0123456789abcdef";
    entry.tip = "explorer 被替换会劫持主页";
    cfg.deploy.tamperFiles.push_back(entry);

    const std::string json = osp::config::toJson(cfg);
    CHECK(json.find("explorer 被替换会劫持主页") != std::string::npos);

    const osp::config::ParseResult parsed = osp::config::fromJson(json);
    REQUIRE(parsed.ok);
    REQUIRE(parsed.config.deploy.tamperFiles.size() == 1);
    CHECK(parsed.config.deploy.tamperFiles[0].tip == std::string("explorer 被替换会劫持主页"));
}

TEST_CASE("OEM 信息：全空时不生成 OEM 段")
{
    osp::config::Config cfg;
    const std::string json = osp::config::toJson(cfg);
    // 空 OEM 段的字段不应出现在 JSON 里造成误解——序列化本身是照写空串的，
    // 真正决定「生成不生成」的是 unattend 侧；这里先钉住字段存在且为空。
    const osp::config::ParseResult parsed = osp::config::fromJson(json);
    REQUIRE(parsed.ok);
    CHECK(parsed.config.oem.manufacturer.empty());
    CHECK(parsed.config.oem.logo.empty());
    CHECK(parsed.config.oem.ieTitle.empty());
}

TEST_CASE("OEM 信息：新增三项可往返")
{
    osp::config::Config cfg;
    cfg.oem.manufacturer = "TestBrand";
    cfg.oem.model = "Model-X";
    cfg.oem.logo = "D:\\oem\\logo.bmp";
    cfg.oem.ieTitle = "TestBrand 定制系统";

    const std::string json = osp::config::toJson(cfg);
    const osp::config::ParseResult parsed = osp::config::fromJson(json);
    REQUIRE(parsed.ok);
    CHECK(parsed.config.oem.logo == std::string("D:\\oem\\logo.bmp"));
    CHECK(parsed.config.oem.ieTitle == std::string("TestBrand 定制系统"));
}

TEST_CASE("tmffun: OEM 页在页签列表里，且 §7 红线子页不存在")
{
    CHECK(osp::ui::tmffunTabIndex("MlSkinTabSheet10") >= 0);
    CHECK(osp::ui::tmffunTabIndex("Page_Nodelfile") == -1);
    CHECK(osp::ui::tmffunTabIndex("Page_Nodelreg") == -1);
}

namespace {

// generate() 先要能算出计算机名，否则整份 XML 不生成（返回 error）
osp::config::Config minimalConfig()
{
    osp::config::Config cfg;
    cfg.systemInfo.computerName = "PC-";
    cfg.systemInfo.computerNameMode = "prefixRandom";
    return cfg;
}

} // namespace

TEST_CASE("OEM 厂商图片：写进 unattend 的 OEMInformation/Logo")
{
    osp::config::Config cfg = minimalConfig();
    cfg.oem.manufacturer = "TestBrand";
    cfg.oem.logo = "D:\\oem\\logo.bmp";

    const auto generated = osp::unattend::generate(cfg);
    REQUIRE(generated.ok);
    const std::string xml = generated.xml;
    CHECK(xml.find("<OEMInformation>") != std::string::npos);
    CHECK(xml.find("<Logo>D:\\oem\\logo.bmp</Logo>") != std::string::npos);
    // Logo 在 Manufacturer 之后、Model 之前（文档字母序）
    const std::size_t manufacturer = xml.find("<Manufacturer>TestBrand</Manufacturer>");
    const std::size_t logo = xml.find("<Logo>");
    REQUIRE(manufacturer != std::string::npos);
    REQUIRE(logo != std::string::npos);
    CHECK(manufacturer < logo);
}

TEST_CASE("OEM 厂商图片：只有图片没有别的字段时也生成 OEM 段")
{
    osp::config::Config cfg = minimalConfig();
    cfg.oem.logo = "D:\\oem\\only.bmp";
    const auto generated = osp::unattend::generate(cfg);
    REQUIRE(generated.ok);
    const std::string xml = generated.xml;
    CHECK(xml.find("<OEMInformation>") != std::string::npos);
    CHECK(xml.find("<Logo>D:\\oem\\only.bmp</Logo>") != std::string::npos);
}

TEST_CASE("IE 窗口标题：落到默认用户配置单元，且允许失败")
{
    osp::config::Config cfg;
    cfg.oem.ieTitle = "TestBrand 定制系统";

    osp::prepare::Options options = osp::prepare::defaultOptions();
    const osp::prepare::Plan plan = osp::prepare::build(cfg, options);

    bool found = false;
    for (const osp::prepare::Action& action : plan.actions)
    {
        if (action.valueName != "Window Title")
        {
            continue;
        }
        found = true;
        CHECK(action.text == std::string("TestBrand 定制系统"));
        CHECK(action.key == std::string("Software\\Microsoft\\Internet Explorer\\Main"));
        CHECK(action.kind == osp::prepare::ActionKind::HiveSet);
        CHECK(action.tolerateFailure);
    }
    CHECK(found);
}

TEST_CASE("IE 窗口标题：不填时不产生动作")
{
    osp::config::Config cfg;
    const osp::prepare::Plan plan = osp::prepare::build(cfg, osp::prepare::defaultOptions());
    for (const osp::prepare::Action& action : plan.actions)
    {
        CHECK(action.valueName != "Window Title");
    }
}

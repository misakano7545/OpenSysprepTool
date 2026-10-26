// M10：INI 解析与 OEM 档案的单元测试
//
// 这里覆盖两件容易出错的事：
//   1. 编码 —— SC 的 .ini 是 GBK，本项目内部一律 UTF-8
//   2. 品牌匹配 —— BIOS 里的厂商名与 SC 目录名往往不完全一致

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <windows.h>

#include "core/ini.hpp"
#include "core/oem.hpp"

using namespace osp;

namespace {

// GBK 编码的「宏碁」二字（D6 D0 CE C4 是「中文」，这里用真实字节更省事：
// 直接用 GBK 的「宏碁」= BA EA E8 B7）。为避免手写字节出错，
// 测试只在需要时用 CP_ACP 无法构造的场景，一律用 ASCII + UTF-8 文本覆盖结构，
// 单独的编码用例见 decodeToUtf8。
const std::string kUtf8Text = "[Support]\r\n"
                              "Url=http://example.com\r\n"
                              "Tel=400-000-0000\r\n"
                              "Stime=7*24\r\n"
                              "[General_CHS]\r\n"
                              "Manufacturer=示例厂商（中国）\r\n"
                              "Model=示例系列电脑\r\n"
                              "[General_EN]\r\n"
                              "Manufacturer=Example\r\n"
                              "Model=Example Series\r\n";

} // namespace

TEST_CASE("INI：节、键值、注释与引号都按 TIniFile 的规矩来")
{
    const std::string text = "; 开头注释\r\n"
                             "# 另一种注释\r\n"
                             "\r\n"
                             "[Section]\r\n"
                             "Plain=value\r\n"
                             "Spaced =  去掉两侧空白  \r\n"
                             "Quoted=\"带引号的值\"\r\n"
                             "WithEquals=a=b=c\r\n"
                             "\r\n"
                             "[Second]\r\n"
                             "Only=1\r\n";

    const ini::Document doc = ini::parse(text);
    REQUIRE(doc.ok);
    CHECK(doc.get("Section", "Plain") == "value");
    CHECK(doc.get("Section", "Spaced") == "去掉两侧空白");
    CHECK(doc.get("Section", "Quoted") == "带引号的值");
    // 只按第一个 '=' 切分，值里的 '=' 保留
    CHECK(doc.get("Section", "WithEquals") == "a=b=c");
    CHECK(doc.get("Second", "Only") == "1");
    CHECK(doc.get("Second", "Missing", "fallback") == "fallback");

    const std::vector<std::string> sections = doc.sections();
    REQUIRE(sections.size() == 2);
    CHECK(sections[0] == "Section");
    CHECK(sections[1] == "Second");
}

TEST_CASE("INI：无键行不丢——SC 的 nt5.ini 里 [Version] 第一行就是光秃秃的产品名")
{
    const std::string text = "[Version]\r\n"
                             "Microsoft Windows XP Professional\r\n"
                             "WinVer=5.01\r\n"
                             "[General]\r\n"
                             "Manufacturer=ACER\r\n";

    const ini::Document doc = ini::parse(text);
    REQUIRE(doc.ok);
    const std::vector<ini::Entry> version = doc.inSection("Version");
    REQUIRE(version.size() == 2);
    CHECK(version[0].key.empty());
    CHECK(version[0].value == "Microsoft Windows XP Professional");
    CHECK(version[1].key == "WinVer");
    CHECK(doc.get("General", "Manufacturer") == "ACER");
}

TEST_CASE("INI：编码判定——UTF-8 BOM / 合法 UTF-8 / GBK 都转成 UTF-8")
{
    // 合法 UTF-8 原样返回
    CHECK(ini::decodeToUtf8("纯 UTF-8 文本") == "纯 UTF-8 文本");

    // UTF-8 BOM 去掉
    const std::string withBom = std::string("\xEF\xBB\xBF") + "带 BOM";
    CHECK(ini::decodeToUtf8(withBom) == "带 BOM");

    // GBK：D6 D0 CE C4 是「中文」。它不是合法 UTF-8，而 SC 的 .ini 全是 GBK，
    // 所以必须按 936 解 —— **与本机 ANSI 代码页无关**。曾经用 CP_ACP 兜底，
    // 结果在 ACP=1252 的 CI runner 上解成乱码（ACER£¨ºê³ž£©）。
    const std::string gbk("\xD6\xD0\xCE\xC4", 4);
    CHECK(ini::decodeToUtf8(gbk) == "中文");

    // UTF-16LE + BOM
    std::string utf16 = "\xFF\xFE";
    for (char ch : std::string("A中"))
    {
        (void)ch;
    }
    const wchar_t sample[] = L"AB";
    for (int i = 0; i < 2; ++i)
    {
        const wchar_t w = sample[i];
        utf16.push_back(static_cast<char>(w & 0xFF));
        utf16.push_back(static_cast<char>((w >> 8) & 0xFF));
    }
    CHECK(ini::decodeToUtf8(utf16) == "AB");
}

TEST_CASE("OEM：nt6.ini 按语言取 General_CHS / General_EN，nt5 回退 General")
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ospt-oem-unit";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path nt6 = dir / "nt6.ini";
    {
        std::ofstream out(nt6, std::ios::binary | std::ios::trunc);
        out << kUtf8Text;
    }

    const oem::LoadResult chs = oem::loadIni(reinterpret_cast<const char*>(nt6.u8string().c_str()), "CHS");
    REQUIRE(chs.ok);
    CHECK(chs.profile.manufacturer == "示例厂商（中国）");
    CHECK(chs.profile.model == "示例系列电脑");
    CHECK(chs.profile.supportUrl == "http://example.com");
    CHECK(chs.profile.supportPhone == "400-000-0000");
    CHECK(chs.profile.supportHours == "7*24");
    CHECK(chs.profile.source.find("General_CHS") != std::string::npos);

    const oem::LoadResult en = oem::loadIni(reinterpret_cast<const char*>(nt6.u8string().c_str()), "EN");
    REQUIRE(en.ok);
    CHECK(en.profile.manufacturer == "Example");
    CHECK(en.profile.model == "Example Series");
    CHECK(en.profile.source.find("General_EN") != std::string::npos);

    // nt5 只有 [General]
    const std::filesystem::path nt5 = dir / "nt5.ini";
    {
        std::ofstream out(nt5, std::ios::binary | std::ios::trunc);
        out << "[Version]\r\nMicrosoft Windows XP Professional\r\nWinVer=5.01\r\n"
               "[General]\r\nManufacturer=ACER\r\nModel=ACER Series\r\n"
               "[Support Information]\r\nLine1=\"Tel:123\"\r\n";
    }
    const oem::LoadResult legacy = oem::loadIni(reinterpret_cast<const char*>(nt5.u8string().c_str()), "CHS");
    REQUIRE(legacy.ok);
    CHECK(legacy.profile.manufacturer == "ACER");
    CHECK(legacy.profile.model == "ACER Series");
    CHECK(legacy.profile.source.find("General") != std::string::npos);
}

TEST_CASE("OEM：一份没有任何字段的 ini 必须报错，而不是当作成功")
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ospt-oem-unit";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path empty = dir / "empty.ini";
    {
        std::ofstream out(empty, std::ios::binary | std::ios::trunc);
        out << "[Other]\r\nNothing=1\r\n";
    }
    const oem::LoadResult result =
        oem::loadIni(reinterpret_cast<const char*>(empty.u8string().c_str()), "CHS");
    CHECK_FALSE(result.ok);
    CHECK(!result.error.empty());

    const oem::Profile profile;
    CHECK_FALSE(oem::hasContent(profile));
}

TEST_CASE("OEM：品牌目录匹配忽略大小写，且不因目录顺序而漂移")
{
    // 造一个品牌目录：目录名 seewo，BIOS 厂商 SEEWO —— 大小写不同也要命中
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "ospt-oem-skin";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "seewo", ec);
    std::filesystem::create_directories(root / "Other", ec);
    {
        std::ofstream out(root / "seewo" / "nt6.ini", std::ios::binary | std::ios::trunc);
        out << kUtf8Text;
    }
    {
        std::ofstream out(root / "Other" / "nt6.ini", std::ios::binary | std::ios::trunc);
        out << "[General_CHS]\r\nManufacturer=别的厂商\r\n";
    }

    oem::BiosInfo bios;
    bios.ok = true;
    bios.manufacturer = "SEEWO";
    bios.productName = "D1530";

    const std::string rootUtf8 = reinterpret_cast<const char*>(root.u8string().c_str());
    const oem::DetectResult hit = oem::detectFromSkinDir(rootUtf8, bios, "CHS");
    REQUIRE(hit.ok);
    CHECK(hit.brand == "seewo");
    CHECK(hit.profile.manufacturer == "示例厂商（中国）");

    // 匹配不上时给出原因，而不是静默返回空
    bios.manufacturer = "NoSuchBrand";
    const oem::DetectResult miss = oem::detectFromSkinDir(rootUtf8, bios, "CHS");
    CHECK_FALSE(miss.ok);
    CHECK(!miss.error.empty());
    CHECK(miss.tried.size() == 2);
}

TEST_CASE("OEM：没有 ini 时用 BIOS 字段兜底，且 supportProvider 跟随厂商")
{
    oem::BiosInfo bios;
    bios.ok = true;
    bios.manufacturer = "LENOVO";
    bios.productName = "20UD";
    const oem::Profile profile = oem::fromBios(bios);
    CHECK(profile.manufacturer == "LENOVO");
    CHECK(profile.model == "20UD");
    CHECK(profile.supportProvider == "LENOVO");
    CHECK(oem::hasContent(profile));

    // BIOS 读不到时什么都不填
    oem::BiosInfo failed;
    failed.ok = false;
    CHECK_FALSE(oem::hasContent(oem::fromBios(failed)));
}

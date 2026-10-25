// M6：模式 B（SRS 离线 hive 注入）的单元测试

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "core/srsinject.hpp"

using namespace osp;

namespace {

std::filesystem::path makeTempDir(const std::string& name)
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void writeUtf16(const std::filesystem::path& path, const std::vector<std::string>& lines)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    const unsigned char bom[2] = {0xFF, 0xFE};
    out.write(reinterpret_cast<const char*>(bom), 2);
    for (const std::string& line : lines)
    {
        for (char ch : line)
        {
            if (static_cast<unsigned char>(ch) < 0x80)
            {
                out.put(ch);
                out.put('\0');
            }
        }
        out.put('\r');
        out.put('\0');
        out.put('\n');
        out.put('\0');
    }
}

std::string pathUtf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.c_str()), u8.size());
}

} // namespace

TEST_CASE(".reg 解析：字符串、dword、hex(2)、删除值、删除键")
{
    const std::filesystem::path dir = makeTempDir("osp-srs-parse");
    const std::filesystem::path file = dir / "Drv.reg";

    writeUtf16(
        file,
        {
            "Windows Registry Editor Version 5.00",
            "",
            R"([HKEY_LOCAL_MACHINE\SYSTEM\ControlSet001\Services\demo])",
            R"("Group"="System Bus Extender")",
            R"("Start"=dword:00000000)",
            R"("Type"=dword:00000001)",
            R"("ImagePath"=hex(2):73,00,72,00,76,00,00,00)",
            R"("Gone"=-)",
            "",
            R"([-HKEY_LOCAL_MACHINE\SYSTEM\ControlSet001\Services\removed])",
        }
    );

    const srsinject::RegFile parsed = srsinject::parseRegFile(pathUtf8(file));

    REQUIRE(parsed.ok);
    CHECK(parsed.encoding == "utf-16le");
    REQUIRE(parsed.keys.size() == 2);
    CHECK(parsed.keys[1].deleted);

    const srsinject::RegKey& key = parsed.keys[0];
    CHECK(key.path.find("Services\\demo") != std::string::npos);
    REQUIRE(key.values.size() == 5);

    CHECK(key.values[0].name == "Group");
    CHECK(key.values[0].type == srsinject::RegType::String);
    CHECK(key.values[0].text == "System Bus Extender");

    CHECK(key.values[1].name == "Start");
    CHECK(key.values[1].type == srsinject::RegType::Dword);
    CHECK(key.values[1].bytes.size() == 4);

    CHECK(key.values[2].name == "Type");
    CHECK(key.values[2].display.find("0x00000001") != std::string::npos);

    // hex(2) 是 REG_EXPAND_SZ，要解出可读文本 —— dry-run 打印的就是它
    CHECK(key.values[3].name == "ImagePath");
    CHECK(key.values[3].type == srsinject::RegType::ExpandString);
    CHECK(key.values[3].text == "srv");
    CHECK(key.values[3].bytes.size() == 8);

    CHECK(key.values[4].type == srsinject::RegType::Deleted);
}

TEST_CASE("挂载键映射：HKLM\\SYSTEM 与 HK_USERS\\DCSRS 都收敛到挂载键，其它 hive 跳过")
{
    const std::string mount = "OSPT_SRS";

    const srsinject::MountedKey system =
        srsinject::mountKeyOf(R"(HKEY_LOCAL_MACHINE\SYSTEM\ControlSet001\Services\pciide)", mount);
    CHECK(system.injectable);
    CHECK(system.relative == "ControlSet001\\Services\\pciide");
    CHECK(system.apiPath == "OSPT_SRS\\ControlSet001\\Services\\pciide");
    CHECK(system.displayPath == "HKLM\\OSPT_SRS\\ControlSet001\\Services\\pciide");

    // Drv.pesrs 用的就是 HK_USERS\DCSRS\...
    const srsinject::MountedKey users = srsinject::mountKeyOf(
        R"(HKEY_USERS\DCSRS\ControlSet001\Control\CriticalDeviceDatabase\PCI#VEN_1002)", mount
    );
    CHECK(users.injectable);
    CHECK(users.relative == "ControlSet001\\Control\\CriticalDeviceDatabase\\PCI#VEN_1002");
    CHECK(users.displayPath.find("HKLM\\OSPT_SRS") == 0);

    // 别的 hive 离线注入管不到，必须显式标出来而不是悄悄丢掉
    const srsinject::MountedKey currentUser =
        srsinject::mountKeyOf(R"(HKEY_CURRENT_USER\Software\Foo)", mount);
    CHECK_FALSE(currentUser.injectable);
    CHECK(currentUser.displayPath == R"(HKEY_CURRENT_USER\Software\Foo)");
}

TEST_CASE("包扫描优先用 Drv.pesrs，并收齐 drivers 下的 .sys")
{
    const std::filesystem::path root = makeTempDir("osp-srs-scan");
    writeUtf16(root / "VendorA" / "Drv.pesrs", {"Windows Registry Editor Version 5.00"});
    writeUtf16(root / "VendorA" / "Drv.reg", {"Windows Registry Editor Version 5.00"});
    std::error_code ec;
    std::filesystem::create_directories(root / "VendorA" / "drivers", ec);
    std::ofstream(root / "VendorA" / "drivers" / "ahci.sys", std::ios::binary) << "stub";

    const srsinject::Plan plan = srsinject::plan("W:\\Win7", pathUtf8(root), "");

    REQUIRE(plan.ok);
    REQUIRE(plan.packages.size() == 1);
    CHECK(plan.packages[0].usedPesrs);
    CHECK(plan.packages[0].regFile.find("Drv.pesrs") != std::string::npos);
    CHECK(plan.packages[0].sysFiles.size() == 1);
}

TEST_CASE("裸 .sys 没有 Services 项时按内核驱动补一条推断项")
{
    const std::filesystem::path root = makeTempDir("osp-srs-bare");
    writeUtf16(root / "VendorB" / "Drv.pesrs", {"Windows Registry Editor Version 5.00"});
    std::error_code ec;
    std::filesystem::create_directories(root / "VendorB" / "drivers", ec);
    std::ofstream(root / "VendorB" / "drivers" / "lonely.sys", std::ios::binary) << "stub";

    const srsinject::Plan plan = srsinject::plan("W:\\Win7", pathUtf8(root), "");

    REQUIRE(plan.ok);
    REQUIRE(plan.serviceOps.size() == 1);
    const srsinject::RegistryOp& op = plan.serviceOps[0];
    CHECK(op.subject == "lonely");
    CHECK(op.inferred);
    CHECK(op.displayPath.find("ControlSet001\\Services\\lonely") != std::string::npos);
    CHECK(op.values.size() == 3); // Type / Start / ImagePath
}

TEST_CASE("计划给出完整的五步，且挂载键可定制")
{
    const std::filesystem::path root = makeTempDir("osp-srs-steps");
    writeUtf16(
        root / "V" / "Drv.pesrs",
        {"Windows Registry Editor Version 5.00",
         "",
         R"([HKEY_LOCAL_MACHINE\SYSTEM\ControlSet001\Control\CriticalDeviceDatabase\PCI#VEN_1])",
         R"("ClassGUID"="{4D36E97B-E325-11CE-BFC1-08002BE10318}")",
         R"("Service"="x")"}
    );

    const srsinject::Plan plan = srsinject::plan("W:\\Win7", pathUtf8(root), "MY_SRS");

    REQUIRE(plan.ok);
    CHECK(plan.mountKey == "MY_SRS");
    CHECK(plan.hivePath == "W:\\Win7\\Windows\\System32\\config\\SYSTEM");
    CHECK(plan.driversDir == "W:\\Win7\\Windows\\System32\\drivers");

    const std::vector<std::string> steps = srsinject::executionSteps(plan);
    REQUIRE(steps.size() == 5);
    CHECK(steps[0].find("RegLoadKey") != std::string::npos);
    CHECK(steps[1].find("复制") != std::string::npos);
    CHECK(steps[2].find("Services") != std::string::npos);
    CHECK(steps[3].find("CriticalDeviceDatabase") != std::string::npos);
    CHECK(steps[4].find("RegUnLoadKey") != std::string::npos);

    REQUIRE(plan.cddOps.size() == 1);
    CHECK(plan.cddOps[0].displayPath.find("HKLM\\MY_SRS\\") == 0);
}

TEST_CASE("用错模式时给出可操作的错误")
{
    const std::filesystem::path root = makeTempDir("osp-srs-wrongmode");
    std::error_code ec;
    std::filesystem::create_directories(root / "inf" / "pkg", ec);
    std::ofstream(root / "inf" / "pkg" / "a.inf") << "[Version]";

    const srsinject::Plan plan = srsinject::plan("W:\\Win7", pathUtf8(root), "");

    CHECK_FALSE(plan.ok);
    CHECK(plan.error.find("--driver") != std::string::npos);
}

// M6：模式 A（dism 标准驱动包）的单元测试

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "core/dism.hpp"

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

void writeFile(const std::filesystem::path& path, const std::string& text)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    out << text;
}

std::string pathUtf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.c_str()), u8.size());
}

} // namespace

TEST_CASE("dism 命令行与微软文档一致")
{
    const std::string line = dism::buildAddDriverCommandLine(
        "C:\\Windows\\System32\\Dism.exe", "W:\\Win7\\", "D:\\drivers\\a\\x.inf"
    );

    CHECK(
        line ==
        "\"C:\\Windows\\System32\\Dism.exe\" /image:\"W:\\Win7\" "
        "/add-driver:\"D:\\drivers\\a\\x.inf\" /recurse"
    );
    // 尾部反斜杠要归一化，否则 /image:"W:\Win7\" 里的反斜杠会转义掉收尾引号
    CHECK(line.find("/image:\"W:\\Win7\"") != std::string::npos);
    CHECK(line.find("/recurse") != std::string::npos);
}

TEST_CASE("dism.exe 的路径指向 64 位本体，不是同名目录")
{
    const std::string path = dism::dismPath();

    CHECK(!path.empty());
    CHECK(path.find("System32") != std::string::npos);
    // 官方位置是 System32\Dism.exe；System32\Dism\ 只是放 provider DLL 的目录
    CHECK(path.find("Dism\\dism.exe") == std::string::npos);
}

TEST_CASE("扫描只认含 .inf 的目录")
{
    const std::filesystem::path root = makeTempDir("osp-dism-scan");

    writeFile(root / "good" / "a.inf", "[Version]");
    writeFile(root / "good" / "a.sys", "stub");
    writeFile(root / "good" / "a.cat", "stub");
    // 没有 .inf 的目录不算模式 A 的包（那是模式 B 的形态）
    writeFile(root / "bare" / "b.sys", "stub");

    std::string error;
    const std::vector<dism::DriverPackage> packages = dism::scan(pathUtf8(root), error);

    CHECK(error.empty());
    REQUIRE(packages.size() == 1);
    CHECK(packages[0].dir.find("good") != std::string::npos);
    CHECK(packages[0].infFiles.size() == 1);
    CHECK(packages[0].sysFiles.size() == 1);
    CHECK(packages[0].catFiles.size() == 1);
    CHECK(packages[0].hasInf());
}

TEST_CASE("目录不存在时给出可操作的错误")
{
    std::string error;
    const std::vector<dism::DriverPackage> packages =
        dism::scan(pathUtf8(makeTempDir("osp-dism-none") / "not-there"), error);

    CHECK(packages.empty());
    CHECK(error.find("不存在") != std::string::npos);
}

TEST_CASE("计划里每个 .inf 一条命令，且都带引号")
{
    const std::filesystem::path root = makeTempDir("osp-dism-plan");
    writeFile(root / "pkg one" / "a.inf", "[Version]");
    writeFile(root / "pkg one" / "a.sys", "stub");
    writeFile(root / "pkg two" / "b.inf", "[Version]");
    writeFile(root / "pkg two" / "b.sys", "stub");

    const dism::Plan plan = dism::plan("W:\\Win7", pathUtf8(root));

    REQUIRE(plan.ok);
    CHECK(plan.image == "W:\\Win7");
    CHECK(plan.packages.size() == 2);
    CHECK(plan.commands.size() == 2);
    for (const dism::Command& command : plan.commands)
    {
        CHECK(command.commandLine.front() == '"');
        CHECK(command.commandLine.find("/image:\"W:\\Win7\"") != std::string::npos);
        CHECK(command.infPath.find(".inf") != std::string::npos);
    }
}

TEST_CASE("没有 .inf 的目录会被判为用错了模式")
{
    const std::filesystem::path root = makeTempDir("osp-dism-wrongmode");
    writeFile(root / "srs" / "x.sys", "stub");
    writeFile(root / "srs" / "Drv.reg", "Windows Registry Editor Version 5.00");

    const dism::Plan plan = dism::plan("W:\\Win7", pathUtf8(root));

    CHECK_FALSE(plan.ok);
    CHECK(plan.error.find("--srs") != std::string::npos);
}

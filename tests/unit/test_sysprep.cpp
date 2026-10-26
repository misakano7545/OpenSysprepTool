// M6：sysprep 编排的单元测试

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "core/sysprep.hpp"

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

std::string pathUtf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.c_str()), u8.size());
}

// 只带 PE 头的最小内核文件，够读 MajorOperatingSystemVersion
void craftKernel(const std::filesystem::path& path, std::uint16_t major, std::uint16_t minor)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::vector<unsigned char> data(0x200, 0);
    data[0] = 'M';
    data[1] = 'Z';
    data[0x3C] = 0x80;
    data[0x80] = 'P';
    data[0x81] = 'E';
    const std::size_t opt = 0x80 + 24;
    data[opt] = 0x0B;
    data[opt + 1] = 0x02; // PE32+
    data[opt + 40] = static_cast<unsigned char>(major & 0xFF);
    data[opt + 41] = static_cast<unsigned char>(major >> 8);
    data[opt + 42] = static_cast<unsigned char>(minor & 0xFF);
    data[opt + 43] = static_cast<unsigned char>(minor >> 8);

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

} // namespace

TEST_CASE("命令行按微软文档的语法行拼装")
{
    sysprep::Options options;
    options.afterAction = "shutdown";
    options.unattendPath = "D:\\out\\unattend.xml";

    const std::string line =
        sysprep::buildCommandLine(options, "C:\\Windows\\System32\\Sysprep\\sysprep.exe");

    CHECK(line.find("\"C:\\Windows\\System32\\Sysprep\\sysprep.exe\"") == 0);
    CHECK(line.find("/generalize") != std::string::npos);
    CHECK(line.find("/oobe") != std::string::npos);
    CHECK(line.find("/shutdown") != std::string::npos);
    CHECK(line.find("/quiet") != std::string::npos);
    CHECK(line.find("/unattend:\"D:\\out\\unattend.xml\"") != std::string::npos);
    // 顺序：generalize -> oobe -> 收尾动作 -> quiet -> unattend
    CHECK(line.find("/generalize") < line.find("/oobe"));
    CHECK(line.find("/oobe") < line.find("/shutdown"));
    CHECK(line.find("/shutdown") < line.find("/quiet"));
    CHECK(line.find("/quiet") < line.find("/unattend:"));
}

TEST_CASE("三种封装后动作各对应一个开关")
{
    const std::string program = "sysprep.exe";

    sysprep::Options reboot;
    reboot.afterAction = "reboot";
    CHECK(sysprep::buildCommandLine(reboot, program).find("/reboot") != std::string::npos);

    sysprep::Options quit;
    quit.afterAction = "quit";
    CHECK(sysprep::buildCommandLine(quit, program).find("/quit") != std::string::npos);

    sysprep::Options shutdown;
    shutdown.afterAction = "shutdown";
    CHECK(sysprep::buildCommandLine(shutdown, program).find("/shutdown") != std::string::npos);

    // 审计模式用 /audit，与 /oobe 互斥
    sysprep::Options audit;
    audit.oobe = false;
    const std::string auditLine = sysprep::buildCommandLine(audit, program);
    CHECK(auditLine.find("/audit") != std::string::npos);
    CHECK(auditLine.find("/oobe") == std::string::npos);
}

TEST_CASE("命令行与步骤必须基于同一份默认值，不能自相矛盾")
{
    // 不给 afterAction 时默认是关机；步骤文案必须说关机，而不是「停留在当前界面」
    const sysprep::Options options;
    const sysprep::Report report = sysprep::plan(options);

    CHECK(report.error.empty());
    CHECK(report.commandLine.find("/shutdown") != std::string::npos);

    bool sawShutdownStep = false;
    bool sawQuitStep = false;
    for (const std::string& step : report.steps)
    {
        if (step.find("关机") != std::string::npos)
        {
            sawShutdownStep = true;
        }
        if (step.find("停留在当前界面") != std::string::npos)
        {
            sawQuitStep = true;
        }
    }
    CHECK(sawShutdownStep);
    CHECK_FALSE(sawQuitStep);
}

TEST_CASE("参数逐条列出，便于与文档核对")
{
    sysprep::Options options;
    options.afterAction = "reboot";
    const sysprep::Report report = sysprep::plan(options);

    REQUIRE(report.parameters.size() >= 5);
    CHECK(report.parameters[0].front() == '"'); // 程序路径带引号
    CHECK(
        std::find(report.parameters.begin(), report.parameters.end(), "/generalize") !=
        report.parameters.end()
    );
    CHECK(std::find(report.parameters.begin(), report.parameters.end(), "/oobe") != report.parameters.end());
    CHECK(
        std::find(report.parameters.begin(), report.parameters.end(), "/reboot") != report.parameters.end()
    );
}

TEST_CASE("没有 --image 时不判定 SHA-2 支持，而不是笼统说「否」")
{
    const sysprep::Sha2Check sha2 = sysprep::checkSha2Support("");

    CHECK_FALSE(sha2.checked);
    CHECK_FALSE(sha2.ok);
    CHECK(sha2.note.find("KB4474419") != std::string::npos);
}

TEST_CASE("离线读到 6.1 且存在 KB4474419 时判定为具备 SHA-2 支持")
{
    const std::filesystem::path root = makeTempDir("osp-sysprep-win7");
    craftKernel(root / "Windows" / "System32" / "ntoskrnl.exe", 6, 1);

    std::error_code ec;
    std::filesystem::create_directories(root / "Windows" / "servicing" / "Packages", ec);
    std::ofstream(
        root / "Windows" / "servicing" / "Packages" /
        "Package_for_KB4474419~31bf3856ad364e35~amd64~~6.1.1.2.mum"
    ) << "stub";

    const sysprep::Sha2Check sha2 = sysprep::checkSha2Support(pathUtf8(root));

    CHECK(sha2.checked);
    CHECK(sha2.targetIsWindows7);
    CHECK(sha2.targetOsVersion == "6.1");
    CHECK(sha2.packageFound);
    CHECK(sha2.ok);
}

TEST_CASE("离线读到 6.1 但缺 KB4474419 时判定为不具备，并给出原因")
{
    const std::filesystem::path root = makeTempDir("osp-sysprep-win7-nopatch");
    craftKernel(root / "Windows" / "System32" / "ntoskrnl.exe", 6, 1);
    std::error_code ec;
    std::filesystem::create_directories(root / "Windows" / "servicing" / "Packages", ec);

    const sysprep::Sha2Check sha2 = sysprep::checkSha2Support(pathUtf8(root));

    CHECK(sha2.checked);
    CHECK(sha2.targetIsWindows7);
    CHECK_FALSE(sha2.packageFound);
    CHECK_FALSE(sha2.ok);
    CHECK(sha2.note.find("KB4474419") != std::string::npos);
}

TEST_CASE("目标不是 Win7 系时不需要 KB4474419")
{
    const std::filesystem::path root = makeTempDir("osp-sysprep-win10");
    craftKernel(root / "Windows" / "System32" / "ntoskrnl.exe", 10, 0);
    std::error_code ec;
    std::filesystem::create_directories(root / "Windows" / "servicing" / "Packages", ec);

    const sysprep::Sha2Check sha2 = sysprep::checkSha2Support(pathUtf8(root));

    CHECK(sha2.checked);
    CHECK_FALSE(sha2.targetIsWindows7);
    CHECK(sha2.targetOsVersion == "10.0");
    CHECK(sha2.ok); // 自带 SHA-2 支持
    CHECK(sha2.note.find("不需要") != std::string::npos);
}

TEST_CASE("映像未挂载时不瞎猜")
{
    const std::filesystem::path root = makeTempDir("osp-sysprep-nomount");

    const sysprep::Sha2Check sha2 = sysprep::checkSha2Support(pathUtf8(root));

    CHECK_FALSE(sha2.checked);
    CHECK_FALSE(sha2.ok);
    CHECK(sha2.note.find("找不到") != std::string::npos);
}

TEST_CASE("sysprep: critical checkup items become blockers")
{
    osp::sysprep::Options options;
    options.afterAction = "quit";

    osp::sysinfo::Check pending;
    pending.id = "pendingServicing";
    pending.severity = "critical";
    pending.detected = true;
    pending.title = "待处理的服务操作";
    pending.message = "系统里有挂起的更新";
    pending.resolution = "先重启";

    osp::sysinfo::Check clean;
    clean.id = "bitlocker";
    clean.severity = "critical";
    clean.detected = false;
    clean.title = "BitLocker 加密状态";

    osp::sysinfo::Check warning;
    warning.id = "domainJoin";
    warning.severity = "warning";
    warning.detected = true;
    warning.title = "已加入域";

    const auto report = osp::sysprep::plan(options, {pending, clean, warning});
    REQUIRE(report.blockers.size() == 1);
    CHECK(report.blockers.front().find("待处理的服务操作") != std::string::npos);
    CHECK(report.blockers.front().find("先重启") != std::string::npos);
    CHECK(report.checks.size() == 3);

    // 干净机器上没有 critical 命中，就不该有阻塞项
    const auto cleanReport = osp::sysprep::plan(options, {clean, warning});
    CHECK(cleanReport.blockers.empty());
}

TEST_CASE("sysprep: dry-run report prints blockers when present")
{
    osp::sysprep::Options options;
    options.afterAction = "quit";

    osp::sysinfo::Check pending;
    pending.id = "pendingServicing";
    pending.severity = "critical";
    pending.detected = true;
    pending.title = "待处理的服务操作";
    pending.message = "系统里有挂起的更新";
    pending.resolution = "先重启";

    const auto report = osp::sysprep::plan(options, {pending});
    const std::string text = osp::sysprep::dryRunReport(report);
    CHECK(text.find("阻塞项（真实执行前必须先解决）") != std::string::npos);
    CHECK(text.find("待处理的服务操作") != std::string::npos);

    const std::string json = osp::sysprep::toJson(report);
    CHECK(json.find("\"checks\"") != std::string::npos);
    CHECK(json.find("pendingServicing") != std::string::npos);
}

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <windows.h>
#include <shellapi.h>

#include "core/config.hpp"
#include "core/dism.hpp"
#include "core/log.hpp"
#include "core/runplan.hpp"
#include "core/srsinject.hpp"
#include "core/sysinfo.hpp"
#include "core/sysprep.hpp"
#include "core/unattend.hpp"
#include "core/version.hpp"
#include "core/win7.hpp"

#include "ui/ui.hpp"

namespace osp::host {

// 定义在 cli_prepare.cpp（M9）
int runPrepareCommandLine(int argc, char** argv);
int runOemDetect(int argc, char** argv);
int runImportOem(int argc, char** argv);
int runRepair(int argc, char** argv);
int runRollbackCommandLine(int argc, char** argv);
int runPrepareSelfTest();

} // namespace osp::host

namespace {

constexpr const char* kRepoUrl = "https://github.com/misakano7545/OpenSysprepTool";

// 当前进程是否已提权（管理员令牌）。
static bool isElevated()
{
    HANDLE token = nullptr;
    if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token) == 0)
    {
        return false;
    }
    TOKEN_ELEVATION elevation = {};
    DWORD returned = 0;
    const BOOL ok = ::GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &returned);
    ::CloseHandle(token);
    return ok != 0 && elevation.TokenIsElevated != 0;
}

// 未提权时用 runas 重新启动自己，等待退出并返回真实退出码。
// CLI 的 stdout/stderr 由提权子进程重定向到临时文件，父进程读回自己的输出流。
// 返回 -1 表示已提权；>=0 表示提权副本的退出码；-2 表示提权失败。
static int relaunchElevatedAndWait(int argc, char** argv)
{
    if (isElevated())
    {
        return -1;
    }

    const bool wantsUi = argc < 2 || std::strcmp(argv[1], "--ui") == 0 ||
        (argc >= 3 && std::strcmp(argv[1], "--console") == 0 && std::strcmp(argv[2], "--ui") == 0);
    std::wstring args;
    for (int i = 1; i < argc; ++i)
    {
        if (i > 1)
        {
            args += L" ";
        }
        const char* a = argv[i];
        const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, a, -1, nullptr, 0);
        std::wstring wa(static_cast<size_t>(wlen - 1), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, a, -1, wa.data(), wlen);
        if (wa.find(L' ') != std::wstring::npos || wa.find(L'"') != std::wstring::npos)
        {
            std::wstring escaped = L"\"";
            for (wchar_t c : wa)
            {
                escaped += (c == L'"') ? L"\\\"" : std::wstring(1, c);
            }
            escaped += L'"';
            wa = std::move(escaped);
        }
        args += wa;
    }

    wchar_t exePath[MAX_PATH] = {};
    ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    // UI 只开窗口；CLI 要回传输出。临时文件只用于这一次提权调用，读完即删。
    wchar_t tempDir[MAX_PATH] = {};
    wchar_t logPath[MAX_PATH] = {};
    ::GetTempPathW(MAX_PATH, tempDir);
    ::GetTempFileNameW(tempDir, L"osp", 0, logPath);

    std::wstring command;
    if (wantsUi)
    {
        command = L"\"" + std::wstring(exePath) + L"\" " + args;
    }
    else
    {
        // 先 cd /d 到当前目录再跑：runas 启动的进程默认 CWD 是 System32，
        // 而 ShellExecuteEx 的 lpDirectory 在 runas 路径上并不可靠（实测被忽略），
        // 于是 `--prepare build\x.json --execute` 这类相对路径会被解析到
        // C:\Windows\System32\build\x.json。在命令行里显式 cd 是唯一稳的办法。
        wchar_t currentDir[MAX_PATH] = {};
        const std::wstring cdPrefix = (::GetCurrentDirectoryW(MAX_PATH, currentDir) != 0)
            ? (L"cd /d \"" + std::wstring(currentDir) + L"\" && ")
            : std::wstring();
        command = L"/d /s /c \"" + cdPrefix + L"\"" + std::wstring(exePath) + L"\" " + args + L" > \"" +
            std::wstring(logPath) + L"\" 2>&1\"";
    }

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = wantsUi ? exePath : L"cmd.exe";
    sei.lpParameters = wantsUi ? args.c_str() : command.c_str();
    sei.nShow = wantsUi ? SW_SHOWNORMAL : SW_HIDE;

    // 必须显式传工作目录：runas 启动的进程默认 CWD 是 System32，
    // 于是 `--prepare build\x.json --execute` 这类相对路径会被解析到
    // C:\Windows\System32\build\x.json，报「打不开配置文件」。这个坑在提权后极难自己看出来，
    // 所以这里尽量带上工作目录（runas 路径下不保证生效，命令行里的 cd 才是可靠的）。
    wchar_t currentDir[MAX_PATH] = {};
    if (::GetCurrentDirectoryW(MAX_PATH, currentDir) != 0)
    {
        sei.lpDirectory = currentDir;
    }

    if (::ShellExecuteExW(&sei) == 0 || sei.hProcess == nullptr)
    {
        ::DeleteFileW(logPath);
        return -2;
    }

    DWORD exitCode = 1;
    ::WaitForSingleObject(sei.hProcess, INFINITE);
    ::GetExitCodeProcess(sei.hProcess, &exitCode);
    ::CloseHandle(sei.hProcess);

    if (!wantsUi)
    {
        std::ifstream log(logPath, std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
        if (!bytes.empty())
        {
            std::fwrite(bytes.data(), 1, bytes.size(), stdout);
            std::fflush(stdout);
        }
        log.close();
    }
    ::DeleteFileW(logPath);
    return static_cast<int>(exitCode);
}

// ospt.exe 现在是 GUI 子系统：正常启动不再出现命令行窗口（宪章 §10 记的「M7 若改 GUI
// 子系统要按 target 区分」）。代价是它不再自动获得控制台 —— 命令行开关的输出在
// cmd.exe 这种「有控制台但没把句柄交下来」的场景会整段消失。
//
// 只在 **stdout 不可用** 的时候才回落到父控制台。反过来无条件 attach 会把调用方给的
// 重定向流劫走：脚本与 CI 抓到的就是空字符串（实测踩过）。
void attachParentConsoleIfNeeded(bool allocateIfMissing)
{
    // --console 是显式调试入口：即使 stdout 已被重定向，也创建/保留一个可见控制台；
    // 普通 CLI 则只在 stdout 不可用时挂回父控制台，绝不劫走重定向流。
    if (allocateIfMissing && ::GetConsoleWindow() == nullptr)
    {
        if (::AllocConsole() == 0)
        {
            return;
        }
    }
    else
    {
        const HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
        const bool usable =
            out != nullptr && out != INVALID_HANDLE_VALUE && ::GetFileType(out) != FILE_TYPE_UNKNOWN;
        if (usable)
        {
            return; // 管道 / 文件 / 已挂上控制台，一律不动
        }

        if (::AttachConsole(ATTACH_PARENT_PROCESS) == 0)
        {
            return; // 双击启动且不是 --console：输出丢弃，不弹黑框
        }
    }

    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    freopen_s(&stream, "CONIN$", "r", stdin);
}

// GPL-3.0 要求分发二进制时提供源码获取方式
void printVersion()
{
    std::printf("ospt %s\n", osp::versionString());
    std::printf("source: %s\n", kRepoUrl);
    std::printf("license: GPL-3.0\n");
}

void printUsage()
{
    std::printf("ospt %s\n\n", osp::versionString());
    std::printf("usage:\n");
    std::printf("  ospt                                             打开配置界面（等同 --ui）\n");
    std::printf("  ospt --ui [--config <cfg.json>]                  打开配置界面，可带一个初始配置\n");
    std::printf("  ospt --console                                         打开命令行窗口并显示用法\n");
    std::printf("  ospt --console --ui                                    命令行窗口 + 配置界面（调试用）\n");
    std::printf("  ospt --selftest    配置 JSON 往返自检\n");
    std::printf("  ospt --checkup [--json <path>]  系统体检\n");
    std::printf("  ospt --oem-detect [--skin <品牌目录>] [--lang CHS|EN] [--json <path>]  OEM 识别\n");
    std::printf("  ospt --import-oem <nt6.ini> [--lang CHS|EN] [--into <cfg.json>] [--out <path>]\n");
    std::printf("                                                    导入 SC 的 OEM 档案\n");
    std::printf("  ospt --repair [--dry-run|--execute] [--journal <json>]  一键修复（等同 SC 的一键修复）\n");
    std::printf("  ospt --gen-unattend <cfg.json> --out <unattend.xml>  生成应答文件\n");
    std::printf("  ospt --gen-runplan <plan.json> --out <run/> [--drives <drives.json>]  生成运行计划\n");
    std::printf("  ospt --inject-drivers --image <挂载点> --driver <目录> --dry-run  模式 A 标准驱动包\n");
    std::printf("  ospt --inject-drivers --image <挂载点> --srs <目录> --dry-run     模式 B SRS 注册表绑定\n"
    );
    std::printf("  ospt --sysprep --dry-run [--image <挂载点>] [--unattend <xml>]     封装编排\n");
    std::printf("  ospt --prepare <cfg.json> [--dry-run|--execute] [--report <json>] [--journal <json>]\n");
    std::printf("                                                    封装前设置：预览 / 执行 / 回滚日志\n");
    std::printf("  ospt --rollback <journal.json> [--dry-run|--execute]  回滚封装前设置\n");
    std::printf("  ospt --prepare-selftest                          封装前设置自检（只写 HKCU）\n");
    std::printf("  ospt --ui-smoke-test [--visible] [--report <json>]        界面冒烟测试\n");
    std::printf("  ospt --version     版本与源码地址\n");
    std::printf("  ospt --help        本说明\n");
}

osp::config::Config makeSampleConfig()
{
    osp::config::Config cfg;
    cfg.schemaVersion = osp::config::kSchemaVersion;

    cfg.systemInfo.registeredName = "OpenSysprep";
    cfg.systemInfo.registeredOrg = "misakano7545";
    cfg.systemInfo.serialNumber = "00000-00000-00000-00000";
    cfg.systemInfo.computerName = "PC-";
    cfg.systemInfo.computerNameMode = "prefixRandom";
    cfg.systemInfo.bootMenuTimeoutSec = 5;

    cfg.firstLogon.runDeviceManager = true;
    cfg.firstLogon.rebootAfterRun = false;
    cfg.firstLogon.resolutionX = 1920;
    cfg.firstLogon.resolutionY = 1080;
    cfg.firstLogon.refreshRate = 60;
    cfg.firstLogon.colorDepth = 32;
    cfg.firstLogon.syncIeHomepage = false;
    cfg.firstLogon.syncIeFavorites = false;

    cfg.optimization.pinTaskbarItems = true;
    cfg.optimization.closeNumLock = true;
    cfg.optimization.enableHibernate = false;
    cfg.optimization.closeRebootDialog = true;
    cfg.optimization.myDocumentsTarget = "";
    cfg.optimization.pageFileTarget = "D:\\";
    cfg.optimization.autoSetIp = false;
    cfg.optimization.ipMode = "fixed";
    cfg.optimization.networkLocation = "work";

    osp::config::RunPlanTask t1;
    t1.phase = "desktop";
    t1.type = "run";
    t1.target = "C:\\Windows\\System32\\notepad.exe";
    t1.windowStyle = "normal";
    t1.waitMode = "noWait";
    t1.cleanup = "keep";
    t1.searchScope = "systemPath";

    osp::config::RunPlanTask t2;
    t2.phase = "postinstall";
    t2.type = "reg";
    t2.target = "C:\\drivers\\oem.reg";
    t2.windowStyle = "hidden";
    t2.waitMode = "wait";
    t2.waitSeconds = 30;
    t2.cleanup = "deleteFile";
    t2.searchScope = "allDrives";

    cfg.runPlan.push_back(t1);
    cfg.runPlan.push_back(t2);

    cfg.deploy.module = "trueTrans";
    cfg.deploy.background = "systemDefault";
    cfg.deploy.titleText = "正在部署系统，请稍候…";
    cfg.deploy.skipOobe = true;
    cfg.deploy.autoCreateUser = true;
    cfg.deploy.userName = "User";
    cfg.deploy.userGroup = "Administrators";
    cfg.deploy.blockVirtualMachine = false;

    cfg.sysprep.mode = "classic";
    cfg.sysprep.generateNewSid = true;
    cfg.sysprep.uninstallDrivers = true;
    cfg.sysprep.loadSrsDrivers = false;
    cfg.sysprep.transferDesktopFiles = false;
    cfg.sysprep.cleanTemp = true;
    cfg.sysprep.cleanDllCache = true;
    cfg.sysprep.autoLogon = true;
    cfg.sysprep.autoLogonCount = 1;
    cfg.sysprep.workgroup = "WORKGROUP";
    cfg.sysprep.domain = "";
    cfg.sysprep.domainUser = "";
    cfg.sysprep.afterAction = "reboot";

    return cfg;
}

std::string toUtf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.c_str()), u8.size());
}

bool writeTextFile(const std::string& utf8Path, const std::string& text, std::string& error)
{
    const std::filesystem::path path = osp::win7::pathFromUtf8(utf8Path);
    // 父目录不存在时先建出来：--report build/m6/x.json 这类调用很常见，
    // 让调用方自己 mkdir 是没必要的摩擦。
    if (path.has_parent_path())
    {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        error = "cannot open for writing: " + utf8Path;
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
    if (!out)
    {
        error = "write failed: " + utf8Path;
        return false;
    }
    return true;
}

int runSelftest()
{
    const std::filesystem::path logPath = std::filesystem::temp_directory_path() / "ospt-selftest.log";

    if (!osp::log::openFile(toUtf8(logPath)))
    {
        std::printf("selftest FAILED: cannot open log file: %s\n", osp::log::lastError().c_str());
        return 1;
    }
    osp::log::info("selftest started");

    const osp::config::Config original = makeSampleConfig();
    const std::string first = osp::config::toJson(original);
    osp::log::info("config serialized, " + std::to_string(first.size()) + " bytes");

    const osp::config::ParseResult parsed = osp::config::fromJson(first);
    if (!parsed.ok)
    {
        osp::log::error("parse failed: " + parsed.error);
        std::printf("selftest FAILED: %s\n", parsed.error.c_str());
        osp::log::close();
        return 1;
    }

    const std::string second = osp::config::toJson(parsed.config);
    if (first != second)
    {
        osp::log::error("roundtrip mismatch");
        std::printf("selftest FAILED: roundtrip mismatch\n");
        osp::log::close();
        return 1;
    }

    if (parsed.config.runPlan.size() != original.runPlan.size())
    {
        std::printf("selftest FAILED: runPlan size mismatch\n");
        osp::log::close();
        return 1;
    }

    if (parsed.config.systemInfo.registeredName != original.systemInfo.registeredName ||
        parsed.config.sysprep.afterAction != original.sysprep.afterAction ||
        parsed.config.deploy.titleText != original.deploy.titleText)
    {
        std::printf("selftest FAILED: field mismatch after roundtrip\n");
        osp::log::close();
        return 1;
    }

    osp::log::info("selftest finished, roundtrip verified");
    osp::log::close();

    std::printf("log: %s\n", toUtf8(logPath).c_str());
    std::printf("config roundtrip OK\n");
    return 0;
}

// M2：系统体检。--json <path> 时额外落盘 JSON，退出码恒为 0（失败项写进 warnings）。
int runCheckup(int argc, char** argv)
{
    const char* jsonPath = nullptr;

    for (int i = 2; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--json") == 0)
        {
            if (i + 1 >= argc)
            {
                std::printf("--json requires an output path\n");
                return 2;
            }
            jsonPath = argv[++i];
        }
        else
        {
            std::printf("unknown option: %s\n\n", argv[i]);
            printUsage();
            return 2;
        }
    }

    const osp::sysinfo::CheckupReport report = osp::sysinfo::collect();

    if (jsonPath != nullptr)
    {
        const std::string text = osp::sysinfo::toJson(report);
        std::ofstream out(osp::win7::pathFromUtf8(jsonPath), std::ios::binary | std::ios::trunc);
        if (!out)
        {
            std::printf("cannot open for writing: %s\n", jsonPath);
            return 1;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.put('\n');
        out.close();
        if (!out)
        {
            std::printf("write failed: %s\n", jsonPath);
            return 1;
        }
        std::printf("checkup json: %s\n", jsonPath);
    }

    std::printf("%s", osp::sysinfo::toSummaryText(report).c_str());
    return 0;
}

// M3：生成 unattend.xml
// 用法: ospt --gen-unattend <cfg.json> --out <unattend.xml>
int runGenUnattend(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("--gen-unattend requires a config path\n\n");
        printUsage();
        return 2;
    }

    const char* configPath = argv[2];
    const char* outPath = nullptr;

    for (int i = 3; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--out") == 0)
        {
            if (i + 1 >= argc)
            {
                std::printf("--out requires a path\n");
                return 2;
            }
            outPath = argv[++i];
        }
        else
        {
            std::printf("unknown option: %s\n\n", argv[i]);
            printUsage();
            return 2;
        }
    }

    if (outPath == nullptr)
    {
        std::printf("--gen-unattend requires --out <unattend.xml>\n\n");
        printUsage();
        return 2;
    }

    std::ifstream in(osp::win7::pathFromUtf8(configPath), std::ios::binary);
    if (!in)
    {
        std::printf("cannot open config: %s\n", configPath);
        return 1;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    const osp::config::ParseResult parsed = osp::config::fromJson(text);
    if (!parsed.ok)
    {
        std::printf("config parse failed: %s\n", parsed.error.c_str());
        return 1;
    }

    const osp::unattend::GenResult generated = osp::unattend::generate(parsed.config);
    if (!generated.ok)
    {
        std::printf("unattend generation failed: %s\n", generated.error.c_str());
        return 1;
    }

    std::string error;
    if (!osp::unattend::writeToFile(outPath, generated.xml, error))
    {
        std::printf("%s\n", error.c_str());
        return 1;
    }

    std::printf("unattend: %s (%zu bytes)\n", outPath, generated.xml.size());
    return 0;
}

// M4：生成运行计划目录
// 用法: ospt --gen-runplan <plan.json> --out <run/> [--drives <drives.json>]
// 产物: <out>/runplan.json + 每阶段一个 .cmd
int runGenRunplan(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("--gen-runplan requires a plan path\n\n");
        printUsage();
        return 2;
    }

    const char* planPath = argv[2];
    const char* outDir = nullptr;
    const char* drivesPath = nullptr;

    for (int i = 3; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--out") == 0)
        {
            if (i + 1 >= argc)
            {
                std::printf("--out requires a path\n");
                return 2;
            }
            outDir = argv[++i];
        }
        else if (std::strcmp(argv[i], "--drives") == 0)
        {
            if (i + 1 >= argc)
            {
                std::printf("--drives requires a path\n");
                return 2;
            }
            drivesPath = argv[++i];
        }
        else
        {
            std::printf("unknown option: %s\n\n", argv[i]);
            printUsage();
            return 2;
        }
    }

    if (outDir == nullptr)
    {
        std::printf("--gen-runplan requires --out <run/>\n\n");
        printUsage();
        return 2;
    }

    std::ifstream in(osp::win7::pathFromUtf8(planPath), std::ios::binary);
    if (!in)
    {
        std::printf("cannot open plan: %s\n", planPath);
        return 1;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    const osp::config::ParseResult parsed = osp::config::fromJson(text);
    if (!parsed.ok)
    {
        std::printf("plan parse failed: %s\n", parsed.error.c_str());
        return 1;
    }

    osp::runplan::DriveTable drives;
    if (drivesPath != nullptr)
    {
        std::ifstream drivesIn(osp::win7::pathFromUtf8(drivesPath), std::ios::binary);
        if (!drivesIn)
        {
            std::printf("cannot open drives table: %s\n", drivesPath);
            return 1;
        }
        std::string drivesText((std::istreambuf_iterator<char>(drivesIn)), std::istreambuf_iterator<char>());
        drivesIn.close();

        std::string error;
        if (!osp::runplan::parseDriveTable(drivesText, drives, error))
        {
            std::printf("%s\n", error.c_str());
            return 1;
        }
    }
    else
    {
        std::string error;
        if (osp::runplan::driveTableFromPlan(text, drives, error))
        {
            std::printf("drives: 取自计划文件内嵌的驱动器表（%zu 个盘符）\n", drives.drives.size());
        }
        else if (!error.empty())
        {
            std::printf("%s\n", error.c_str());
            return 1;
        }
        else
        {
            // 既没给 --drives 也没内嵌：枚举本机驱动器。
            // 来源会写进 runplan.json，验收脚本据此判断路径解析是否依赖本机。
            drives = osp::runplan::enumerateLocalDrives();
            std::printf("drives: 未提供模拟表，使用本机枚举（%zu 个盘符）\n", drives.drives.size());
        }
    }

    const osp::runplan::PlanResult plan = osp::runplan::build(parsed.config, drives);
    if (!plan.ok)
    {
        std::printf("runplan generation failed: %s\n", plan.error.c_str());
        return 1;
    }

    std::error_code ec;
    const std::filesystem::path directory = osp::win7::pathFromUtf8(outDir);
    std::filesystem::create_directories(directory, ec);
    if (ec)
    {
        std::printf("cannot create output directory %s: %s\n", outDir, ec.message().c_str());
        return 1;
    }

    std::string error;
    const std::string planJson = osp::runplan::toJson(plan);
    if (!writeTextFile(toUtf8(directory / "runplan.json"), planJson, error))
    {
        std::printf("%s\n", error.c_str());
        return 1;
    }

    std::size_t totalTasks = 0;
    int written = 0;
    for (const osp::runplan::PhasePlan& phase : plan.phases)
    {
        const std::string script = osp::runplan::phaseScript(plan, phase);
        if (!writeTextFile(toUtf8(directory / phase.scriptName), script, error))
        {
            std::printf("%s\n", error.c_str());
            return 1;
        }
        ++written;
        totalTasks += phase.tasks.size();
        std::printf("  %-16s %zu 个任务\n", phase.scriptName.c_str(), phase.tasks.size());
    }

    for (const std::string& warning : plan.warnings)
    {
        std::printf("warning: %s\n", warning.c_str());
    }

    std::printf("runplan: %s\n", outDir);
    std::printf("  runplan.json + %d 个阶段脚本，共 %zu 个任务\n", written, totalTasks);
    return 0;
}

// ---------------------------------------------------------------- M6：驱动注入

int runInjectDrivers(int argc, char** argv)
{
    const char* image = nullptr;
    const char* driverDir = nullptr;
    const char* srsDir = nullptr;
    const char* mountKey = nullptr;
    const char* reportPath = nullptr;
    bool dryRun = false;
    bool execute = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const auto value = [&i, argc, argv](const char* name) -> const char* {
            if (i + 1 >= argc)
            {
                std::printf("%s 需要一个值\n", name);
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--inject-drivers")
        {
            continue;
        }
        if (arg == "--image")
        {
            image = value("--image");
        }
        else if (arg == "--driver")
        {
            driverDir = value("--driver");
        }
        else if (arg == "--srs")
        {
            srsDir = value("--srs");
        }
        else if (arg == "--mount-key")
        {
            mountKey = value("--mount-key");
        }
        else if (arg == "--report")
        {
            reportPath = value("--report");
        }
        else if (arg == "--dry-run")
        {
            dryRun = true;
        }
        else if (arg == "--execute")
        {
            execute = true;
        }
        else
        {
            std::printf("unknown option: %s\n\n", argv[i]);
            printUsage();
            return 2;
        }
    }

    if (image == nullptr)
    {
        std::printf("--image <挂载点> 是必需的\n\n");
        printUsage();
        return 2;
    }
    if (driverDir == nullptr && srsDir == nullptr)
    {
        std::printf("需要 --driver <目录>（模式 A 标准驱动包）或 --srs <目录>（模式 B SRS 包）\n");
        return 2;
    }
    if (driverDir != nullptr && srsDir != nullptr)
    {
        std::printf("--driver 与 --srs 是两种不同的注入模式，一次只能给一个\n");
        return 2;
    }

    if (driverDir != nullptr)
    {
        const osp::dism::Plan plan = osp::dism::plan(image, driverDir);
        if (!plan.ok)
        {
            std::printf("%s\n", plan.error.c_str());
            return 1;
        }

        std::fputs(osp::dism::dryRunReport(plan).c_str(), stdout);

        if (reportPath != nullptr)
        {
            std::string error;
            if (!writeTextFile(reportPath, osp::dism::toJson(plan), error))
            {
                std::printf("%s\n", error.c_str());
                return 1;
            }
        }

        if (dryRun)
        {
            std::printf("RESULT: DRY-RUN OK  （模式 A，%zu 条 dism 命令）\n", plan.commands.size());
            return 0;
        }
        if (!execute)
        {
            std::printf("\n真实执行会调用 dism 改动挂载点，需要显式加 --execute；"
                        "只想看将做什么请加 --dry-run\n");
            return 2;
        }

        for (const osp::dism::Command& command : plan.commands)
        {
            std::printf("执行: %s\n", command.commandLine.c_str());
            std::uint32_t code = 0;
            std::string error;
            if (!osp::dism::runCommand(command.commandLine, code, error))
            {
                std::printf("%s\n", error.c_str());
                std::printf("RESULT: FAILED\n");
                return 1;
            }
            std::printf("  退出码 %u\n", code);
            if (code != 0)
            {
                std::printf("RESULT: FAILED\n");
                return 1;
            }
        }
        std::printf("RESULT: OK\n");
        return 0;
    }

    const osp::srsinject::Plan plan =
        osp::srsinject::plan(image, srsDir, mountKey != nullptr ? std::string(mountKey) : std::string());
    if (!plan.ok)
    {
        std::printf("%s\n", plan.error.c_str());
        return 1;
    }

    std::fputs(osp::srsinject::dryRunReport(plan).c_str(), stdout);

    if (reportPath != nullptr)
    {
        std::string error;
        if (!writeTextFile(reportPath, osp::srsinject::toJson(plan), error))
        {
            std::printf("%s\n", error.c_str());
            return 1;
        }
    }

    if (dryRun)
    {
        std::printf(
            "RESULT: DRY-RUN OK  （模式 B，%zu 个 .sys / %zu 个注册表项）\n",
            plan.sysFileCount,
            plan.registryOpCount
        );
        return 0;
    }
    if (!execute)
    {
        std::printf("\n真实执行会挂载并改写目标 SYSTEM hive，需要管理员权限，"
                    "必须显式加 --execute；只想看将做什么请加 --dry-run\n");
        return 2;
    }

    std::vector<std::string> log;
    std::string error;
    const bool done = osp::srsinject::execute(plan, log, error);
    for (const std::string& line : log)
    {
        std::printf("  %s\n", line.c_str());
    }
    if (!done)
    {
        std::printf("%s\n", error.c_str());
        std::printf("RESULT: FAILED\n");
        return 1;
    }
    std::printf("RESULT: OK\n");
    return 0;
}

// ---------------------------------------------------------------- M6：sysprep

int runSysprep(int argc, char** argv)
{
    osp::sysprep::Options options;
    std::string reportPath;
    bool dryRun = false;
    bool execute = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const auto value = [&i, argc, argv](const char* name) -> std::string {
            if (i + 1 >= argc)
            {
                std::printf("%s 需要一个值\n", name);
                return std::string();
            }
            return std::string(argv[++i]);
        };

        if (arg == "--sysprep")
        {
            continue;
        }
        if (arg == "--image")
        {
            options.image = value("--image");
        }
        else if (arg == "--unattend")
        {
            options.unattendPath = value("--unattend");
        }
        else if (arg == "--after")
        {
            options.afterAction = value("--after");
        }
        else if (arg == "--mode")
        {
            options.mode = value("--mode");
        }
        else if (arg == "--report")
        {
            reportPath = value("--report");
        }
        else if (arg == "--dry-run")
        {
            dryRun = true;
        }
        else if (arg == "--execute")
        {
            execute = true;
        }
        else
        {
            std::printf("unknown option: %s\n\n", argv[i]);
            printUsage();
            return 2;
        }
    }

    if (!options.afterAction.empty() && options.afterAction != "quit" && options.afterAction != "reboot" &&
        options.afterAction != "shutdown")
    {
        std::printf("--after 只接受 quit / reboot / shutdown，收到 %s\n", options.afterAction.c_str());
        return 2;
    }

    const osp::sysprep::Report report = osp::sysprep::plan(options);
    if (!report.error.empty())
    {
        std::printf("%s\n", report.error.c_str());
        return 1;
    }

    std::fputs(osp::sysprep::dryRunReport(report).c_str(), stdout);

    if (!reportPath.empty())
    {
        std::string writeError;
        if (!writeTextFile(reportPath, osp::sysprep::toJson(report), writeError))
        {
            std::printf("%s\n", writeError.c_str());
            return 1;
        }
    }

    if (dryRun)
    {
        std::printf("RESULT: DRY-RUN OK  （命令行 %zu 个参数）\n", report.parameters.size());
        return 0;
    }
    if (!execute)
    {
        std::printf("\n真实执行会立即封装本机（不可逆）并关机/重启，必须显式加 --execute；"
                    "只想看将做什么请加 --dry-run\n");
        return 2;
    }
    if (!report.programExists)
    {
        std::printf("找不到 sysprep.exe: %s，无法执行\n", report.program.c_str());
        std::printf("RESULT: FAILED\n");
        return 1;
    }
    if (!report.blockers.empty())
    {
        std::printf("存在阻塞项，拒绝执行：\n");
        for (const std::string& blocker : report.blockers)
        {
            std::printf("  - %s\n", blocker.c_str());
        }
        std::printf("RESULT: FAILED\n");
        return 1;
    }

    std::uint32_t code = 0;
    std::string runError;
    std::printf("执行: %s\n", report.commandLine.c_str());
    if (!osp::dism::runCommand(report.commandLine, code, runError))
    {
        std::printf("%s\n", runError.c_str());
        std::printf("RESULT: FAILED\n");
        return 1;
    }
    std::printf("退出码 %u\n", code);
    std::printf("RESULT: %s\n", code == 0 ? "OK" : "FAILED");
    return code == 0 ? 0 : 1;
}

// ---------------------------------------------------------------- M7a：UI 冒烟测试

int runUi(int argc, char** argv)
{
    std::string configPath;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--ui")
        {
            continue;
        }
        if (arg == "--config")
        {
            if (i + 1 >= argc)
            {
                std::printf("--config 需要一个值\n");
                return 2;
            }
            configPath = argv[++i];
        }
        else
        {
            std::printf("unknown option: %s\n\n", argv[i]);
            printUsage();
            return 2;
        }
    }

    std::printf("OpenSysprepTool %s 配置界面\n", osp::versionString());
    if (!configPath.empty())
    {
        std::printf("启动时载入：%s\n", configPath.c_str());
    }
    std::printf("关掉窗口即退出。\n");
    return osp::ui::runApplication(configPath);
}

int runUiSmokeTest(int argc, char** argv)
{
    bool visible = false;
    const char* reportPath = nullptr;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--ui-smoke-test")
        {
            continue;
        }
        if (arg == "--visible")
        {
            visible = true;
        }
        else if (arg == "--report")
        {
            if (i + 1 >= argc)
            {
                std::printf("--report 需要一个值\n");
                return 2;
            }
            reportPath = argv[++i];
        }
        else
        {
            std::printf("unknown option: %s\n\n", argv[i]);
            printUsage();
            return 2;
        }
    }

    const osp::ui::SmokeResult result = osp::ui::runSmokeTest(visible);
    std::fputs(osp::ui::smokeReport(result).c_str(), stdout);

    if (reportPath != nullptr)
    {
        std::string error;
        if (!writeTextFile(reportPath, osp::ui::smokeJson(result), error))
        {
            std::printf("%s\n", error.c_str());
            return 1;
        }
    }

    // 无头环境显式跳过：不算失败，但必须把原因印出来（宪章 §9.7 M7）
    if (result.skipped)
    {
        std::printf("跳过的原因：%s\n", result.skipReason.c_str());
        return 0;
    }
    return result.ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    // Windows 的 argv 是 ANSI 编码：中文路径在非中文代码页的系统上会变成 '?'。
    // 换成从 GetCommandLineW 取的 UTF-8 版本，后面所有逻辑（含提权重启时的参数重建）
    // 都按 UTF-8 处理。
    std::vector<std::string> utf8Args = osp::win7::utf8CommandLineArguments();
    std::vector<char*> utf8Argv;
    utf8Argv.reserve(utf8Args.size());
    for (std::string& item : utf8Args)
    {
        utf8Argv.push_back(item.data());
    }
    if (!utf8Argv.empty())
    {
        argc = static_cast<int>(utf8Argv.size());
        argv = utf8Argv.data();
    }

    // 无参数 / --ui：只显示界面；--console：显式要求保留命令行窗口。
    const bool wantsConsole = argc >= 2 && std::strcmp(argv[1], "--console") == 0;
    const bool wantsUi = argc < 2 || std::strcmp(argv[1], "--ui") == 0 ||
        (wantsConsole && argc >= 3 && std::strcmp(argv[2], "--ui") == 0);

    // 界面与需要管理员的操作在 main 开头自动提权；只读开关和 dry-run 不提权。
    bool dryRun = false;
    for (int i = 2; i < argc; ++i)
    {
        dryRun = dryRun || std::strcmp(argv[i], "--dry-run") == 0;
    }
    const bool operation = argc >= 2 &&
        (std::strcmp(argv[1], "--inject-drivers") == 0 || std::strcmp(argv[1], "--sysprep") == 0 ||
         std::strcmp(argv[1], "--execute") == 0);
    const bool prepareOperation = argc >= 2 &&
        (std::strcmp(argv[1], "--prepare") == 0 || std::strcmp(argv[1], "--rollback") == 0 ||
         std::strcmp(argv[1], "--repair") == 0);
    bool wantsExecute = false;
    for (int i = 2; i < argc; ++i)
    {
        wantsExecute = wantsExecute || std::strcmp(argv[i], "--execute") == 0;
    }
    // --prepare / --rollback 只有 --execute 才需要管理员；--dry-run 与只打印计划不提权。
    const bool needsAdmin =
        wantsUi || (operation && !dryRun) || (prepareOperation && wantsExecute && !dryRun);

    if (needsAdmin)
    {
        const int elevatedExit = relaunchElevatedAndWait(argc, argv);
        if (elevatedExit == -2)
        {
            std::fprintf(stderr, "需要管理员权限：用户拒绝了 UAC 提权，或系统不允许提权\n");
            return 1;
        }
        if (elevatedExit >= 0)
        {
            return elevatedExit;
        }
    }

    // ospt.exe 是 GUI 子系统：正常启动不会新建命令行窗口。
    // CLI 开关若 stdout 已被管道/文件接住则直接写；否则挂回父控制台。
    // 双击启动时没有父控制台，输出自然丢弃，不会弹出黑框。
    attachParentConsoleIfNeeded(wantsConsole);

    if (wantsConsole && argc >= 3 && std::strcmp(argv[2], "--ui") == 0)
    {
        return runUi(argc - 1, argv + 1);
    }

    if (wantsConsole && argc >= 3)
    {
        return main(argc - 1, argv + 1);
    }

    if (wantsConsole)
    {
        printUsage();
        std::printf("\n--console：按回车关闭命令行窗口\n");
        (void)std::getchar();
        return 0;
    }

    // 不带参数 = 打开配置界面（双击 exe 就能用）
    if (argc < 2)
    {
        return runUi(argc, argv);
    }

    const char* arg = argv[1];

    if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "/?") == 0)
    {
        printUsage();
        return 0;
    }

    if (std::strcmp(arg, "--ui") == 0)
    {
        return runUi(argc, argv);
    }

    if (std::strcmp(arg, "--selftest") == 0)
    {
        return runSelftest();
    }

    if (std::strcmp(arg, "--checkup") == 0)
    {
        return runCheckup(argc, argv);
    }

    if (std::strcmp(arg, "--oem-detect") == 0)
    {
        return osp::host::runOemDetect(argc - 1, argv + 1);
    }

    if (std::strcmp(arg, "--import-oem") == 0)
    {
        return osp::host::runImportOem(argc - 1, argv + 1);
    }

    if (std::strcmp(arg, "--repair") == 0)
    {
        return osp::host::runRepair(argc - 1, argv + 1);
    }

    if (std::strcmp(arg, "--gen-unattend") == 0)
    {
        return runGenUnattend(argc, argv);
    }

    if (std::strcmp(arg, "--gen-runplan") == 0)
    {
        return runGenRunplan(argc, argv);
    }

    if (std::strcmp(arg, "--inject-drivers") == 0)
    {
        return runInjectDrivers(argc, argv);
    }

    if (std::strcmp(arg, "--sysprep") == 0)
    {
        return runSysprep(argc, argv);
    }

    if (std::strcmp(arg, "--prepare") == 0)
    {
        return osp::host::runPrepareCommandLine(argc - 1, argv + 1);
    }

    if (std::strcmp(arg, "--rollback") == 0)
    {
        return osp::host::runRollbackCommandLine(argc - 1, argv + 1);
    }

    if (std::strcmp(arg, "--prepare-selftest") == 0)
    {
        return osp::host::runPrepareSelfTest();
    }

    if (std::strcmp(arg, "--ui-smoke-test") == 0)
    {
        return runUiSmokeTest(argc, argv);
    }

    if (std::strcmp(arg, "--version") == 0)
    {
        printVersion();
        return 0;
    }

    std::printf("unknown option: %s\n\n", arg);
    printUsage();
    return 2;
}

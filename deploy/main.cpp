// osptd —— 部署运行时（宪章 §8 M5）
//
// 消费 ospt --gen-runplan 产出的 run/ 目录，按阶段执行任务。
// 它是唯一在**目标系统**上运行的产物，所以必须能在 Windows 7 SP1 x64 / x86 上跑起来。

#include <cstdio>
#include <memory>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/deploy.hpp"
#include "core/version.hpp"
#include "core/win7.hpp"

#include <windows.h>
#include <shellapi.h>

#include "progress.hpp"
#include "progress_ui.hpp"

namespace {

constexpr const char* kRepoUrl = "https://github.com/misakano7545/OpenSysprepTool";

void printVersion()
{
    std::printf("osptd %s\n", osp::versionString());
    std::printf("source: %s\n", kRepoUrl);
    std::printf("license: GPL-3.0\n");
}

void printUsage()
{
    std::printf("osptd %s —— 部署运行时\n\n", osp::versionString());
    std::printf("usage:\n");
    std::printf("  osptd --dry-run --plan <run/>              逐阶段打印将要执行的动作，不启动进程\n");
    std::printf("  osptd --plan <run/> [--log <log.txt>] [--report <report.json>]\n");
    std::printf("                                             真正执行运行计划\n");
    std::printf("  osptd --plan <run/> --continue-on-error    失败不中止，继续后续任务\n");
    std::printf("  osptd --preview-presentation --plan <run/> [--seconds <n>]\n");
    std::printf("                                             只画部署进度页，不执行任何任务（验收/演示用）\n"
    );
    std::printf("  osptd --win7-compat [--json]               Windows 7 兼容自述（供闸门脚本比对）\n");
    std::printf("  osptd --version                            版本与源码地址\n");
}

bool writeTextFile(const std::string& utf8Path, const std::string& text, std::string& error)
{
    std::ofstream out(osp::win7::pathFromUtf8(utf8Path), std::ios::binary | std::ios::trunc);
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

std::string hex32(std::uint32_t value)
{
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "0x%08X", value);
    return std::string(buffer);
}

// --win7-compat：把编译期常量、运行期系统、动态解析白名单一并吐出来。
// 闸门脚本拿它和 PE 实测值交叉比对 —— 自述值与实测值不一致就算失败。
int runWin7Compat(int argc, char** argv)
{
    bool json = false;
    for (int i = 2; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--json") == 0)
        {
            json = true;
        }
        else
        {
            std::printf("unknown option: %s\n\n", argv[i]);
            printUsage();
            return 2;
        }
    }

    // 先走一遍会触发动态解析的路径，resolvedProcs() 才有内容
    const osp::win7::OsVersion version = osp::win7::osVersion();
    const std::uint64_t stamp = osp::win7::fileTimeNow();
    (void)osp::win7::tickCountMs();

    if (json)
    {
        using nlohmann::json;

        json root;
        root["schemaVersion"] = 1;
        root["compileTime"]["WINVER"] = hex32(osp::win7::kWinVer);
        root["compileTime"]["_WIN32_WINNT"] = hex32(osp::win7::kWin32WinNt);
        root["compileTime"]["NTDDI_VERSION"] = hex32(osp::win7::kNtddiVersion);
        root["expected"]["subsystemMajor"] = osp::win7::kExpectedSubsystemMajor;
        root["expected"]["subsystemMinor"] = osp::win7::kExpectedSubsystemMinor;

        json forbidden = json::array();
        for (const char* dll : osp::win7::forbiddenRuntimeDlls())
        {
            forbidden.push_back(dll);
        }
        root["forbiddenRuntimeDlls"] = std::move(forbidden);

        json whitelist = json::array();
        for (const osp::win7::DynamicApi& api : osp::win7::dynamicApiWhitelist())
        {
            json item;
            item["module"] = api.module;
            item["proc"] = api.proc;
            item["since"] = api.since;
            item["purpose"] = api.purpose;
            item["fallback"] = api.fallback;
            whitelist.push_back(std::move(item));
        }
        root["dynamicApiWhitelist"] = std::move(whitelist);
        root["resolvedProcs"] = osp::win7::resolvedProcs();

        // osptd 的 manifest 要求管理员权限：能跑到这里就说明已经提权。
        // 闸门脚本用这一项确认「产物确实在要权限」，也用来记录 CI runner 的令牌状态。
        root["elevated"] = osp::win7::isElevated();

        json os;
        os["major"] = version.major;
        os["minor"] = version.minor;
        os["build"] = version.build;
        os["servicePackMajor"] = version.servicePackMajor;
        os["productName"] = version.productName;
        os["queriedFromNt"] = version.queriedFromNt;
        os["supported"] = osp::win7::isSupportedTarget(version);
        root["os"] = std::move(os);

        root["sampleFileTime"] = osp::win7::formatFileTime(stamp);

        std::printf("%s\n", root.dump(2).c_str());
        return 0;
    }

    std::printf("osptd %s  Windows 7 兼容自述\n\n", osp::versionString());
    std::printf("编译期（第一道闸，须在 sdkddkver.h 之前生效）\n");
    std::printf("  WINVER             = %s\n", hex32(osp::win7::kWinVer).c_str());
    std::printf("  _WIN32_WINNT       = %s\n", hex32(osp::win7::kWin32WinNt).c_str());
    std::printf("  NTDDI_VERSION      = %s\n", hex32(osp::win7::kNtddiVersion).c_str());
    std::printf(
        "  期望子系统版本     = %d.%d\n",
        osp::win7::kExpectedSubsystemMajor,
        osp::win7::kExpectedSubsystemMinor
    );

    std::printf("\n运行库（第三道闸）\n");
    std::printf("  链接方式           = 静态 /MT\n");
    std::printf("  禁止的动态依赖     =");
    for (const char* dll : osp::win7::forbiddenRuntimeDlls())
    {
        std::printf(" %s", dll);
    }
    std::printf(" api-ms-win-*\n");

    std::printf("\n动态解析的 Win8+ API 白名单（第二道闸）\n");
    if (osp::win7::dynamicApiWhitelist().empty())
    {
        std::printf("  （空 —— 没有用到任何 Win8+ 的 API）\n");
    }
    for (const osp::win7::DynamicApi& api : osp::win7::dynamicApiWhitelist())
    {
        std::printf("  %s!%s\n", api.module, api.proc);
        std::printf("      起始系统  : %s\n", api.since);
        std::printf("      用途      : %s\n", api.purpose);
        std::printf("      Win7 回退 : %s\n", api.fallback);
    }

    std::printf("\n本次解析成功的 API\n");
    for (const std::string& proc : osp::win7::resolvedProcs())
    {
        std::printf("  %s\n", proc.c_str());
    }

    std::printf("\n运行期系统\n");
    std::printf("  %s\n", osp::win7::describe(version).c_str());
    std::printf("  是否满足最低目标（Win7 SP1）: %s\n", osp::win7::isSupportedTarget(version) ? "是" : "否");
    std::printf("  已提权（管理员令牌）: %s\n", osp::win7::isElevated() ? "是" : "否");
    std::printf("  时间戳样例: %s\n", osp::win7::formatFileTime(stamp).c_str());
    return 0;
}

// 只画部署进度页，不执行任何任务。
// 存在的意义：图形页是本项目的验收难点 —— 没有它就只能靠「人肉看一眼」，
// 有了它，verify_presentation.py 能断言「窗口真的建起来了、真的在画、能正常退出」。
int runPreviewPresentation(int argc, char** argv)
{
    const char* planDir = nullptr;
    int seconds = 4;
    std::string screenshotPath;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--preview-presentation")
        {
            continue;
        }
        if (arg == "--plan")
        {
            if (i + 1 >= argc)
            {
                std::printf("--plan 需要一个目录\n");
                return 2;
            }
            planDir = argv[++i];
        }
        else if (arg == "--seconds")
        {
            if (i + 1 >= argc)
            {
                std::printf("--seconds 需要一个值\n");
                return 2;
            }
            seconds = std::atoi(argv[++i]);
            if (seconds <= 0 || seconds > 120)
            {
                std::printf("--seconds 取值 1..120\n");
                return 2;
            }
        }
        else if (arg == "--screenshot")
        {
            if (i + 1 >= argc)
            {
                std::printf("--screenshot 需要一个路径\n");
                return 2;
            }
            screenshotPath = argv[++i];
        }
        else
        {
            std::printf("unknown option: %s\n\n", arg.c_str());
            printUsage();
            return 2;
        }
    }

    if (!osp::deploy::graphicalPageAvailable())
    {
        std::printf("当前会话没有可见窗口站，图形进度页不可用（这不算失败）\n");
        std::printf("RESULT: SKIP\n");
        return 0;
    }

    osp::deploy::Presentation presentation;
    int totalTasks = 8;
    if (planDir != nullptr)
    {
        const osp::deploy::Plan plan = osp::deploy::load(planDir);
        if (!plan.ok)
        {
            std::printf("%s\n", plan.error.c_str());
            return 1;
        }
        presentation = osp::deploy::Presentation::fromPlan(plan);
        // 预览固定画 5 阶段 × 2 任务 = 10 条，总条数要跟着它走，
        // 否则会显示成「完成 2 / 1」这种自相矛盾的计数
        totalTasks = 10;
    }

    osp::deploy::GraphProgressPage page(presentation, totalTasks);
    if (!page.ok())
    {
        std::printf("图形进度页创建失败\n");
        std::printf("RESULT: FAILED\n");
        return 1;
    }

    // 造一个进度：5 个阶段、每个阶段两个任务，按秒推进
    const char* phases[] = {"preinstall", "install", "postinstall", "logon", "desktop"};
    const char* labels[] = {"部署前", "部署中", "部署后", "登录时", "进桌面"};

    for (int p = 0; p < 5; ++p)
    {
        osp::deploy::Phase phase;
        phase.id = phases[p];
        phase.label = labels[p];
        page.phaseStart(phase, p + 1, 5);

        for (int t = 0; t < 2; ++t)
        {
            osp::deploy::Task task;
            task.index = p * 2 + t + 1;
            task.commandLine = std::string("C:\\Windows\\System32\\cmd.exe /c echo ") + labels[p];
            page.taskStart(phase, task, t + 1, 2);

            const DWORD until = GetTickCount() + static_cast<DWORD>(seconds * 1000 / 10);
            while (GetTickCount() < until)
            {
                page.pump();
                Sleep(16);
            }

            osp::deploy::TaskOutcome outcome;
            outcome.index = task.index;
            outcome.ok = true;
            outcome.message = "exited 0";
            page.taskFinish(phase, task, outcome);
        }
        page.phaseFinish(phase, 2, 0);

        // 第一阶段结束就存一张：既能验收「真的画出了东西」，也方便人工看样式
        if (p == 0 && !screenshotPath.empty())
        {
            std::string error;
            if (!page.saveToBmp(screenshotPath, error))
            {
                std::printf("%s\n", error.c_str());
                std::printf("RESULT: FAILED\n");
                return 1;
            }
            std::printf("画面已保存: %s\n", screenshotPath.c_str());
        }
    }

    std::printf(
        "图形进度页已绘制完毕（模块 %s，背景 %s）\n",
        presentation.module.c_str(),
        presentation.background.c_str()
    );
    std::printf("RESULT: OK\n");
    return 0;
}

int runDeploy(int argc, char** argv, bool dryRun)
{
    const char* planDir = nullptr;
    const char* logPath = nullptr;
    const char* reportPath = nullptr;
    bool continueOnError = false;

    // 从 argv[1] 开始解析：入口既可能是 --dry-run，也可能是 --plan <dir>，
    // 后者把目录放在 argv[2]，从 2 起会把目录当成未知选项。
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--plan") == 0)
        {
            if (i + 1 >= argc)
            {
                std::printf("--plan requires a directory\n");
                return 2;
            }
            planDir = argv[++i];
        }
        else if (std::strcmp(argv[i], "--log") == 0)
        {
            if (i + 1 >= argc)
            {
                std::printf("--log requires a path\n");
                return 2;
            }
            logPath = argv[++i];
        }
        else if (std::strcmp(argv[i], "--report") == 0)
        {
            if (i + 1 >= argc)
            {
                std::printf("--report requires a path\n");
                return 2;
            }
            reportPath = argv[++i];
        }
        else if (std::strcmp(argv[i], "--continue-on-error") == 0)
        {
            continueOnError = true;
        }
        else if (std::strcmp(argv[i], "--dry-run") == 0)
        {
            dryRun = true;
        }
        else
        {
            std::printf("unknown option: %s\n\n", argv[i]);
            printUsage();
            return 2;
        }
    }

    if (planDir == nullptr)
    {
        std::printf("--plan <run/> is required\n\n");
        printUsage();
        return 2;
    }

    const osp::deploy::Plan plan = osp::deploy::load(planDir);
    if (!plan.ok)
    {
        std::printf("%s\n", plan.error.c_str());
        return 1;
    }

    // 部署前闸门：虚拟机检测 / 文件防篡改 / 部署主控校验。
    // 一律在**执行任何任务之前**跑：跑了一半才发现被篡改是最坏的情况。
    const osp::deploy::PreflightResult preflight = osp::deploy::preflight(plan);
    if (dryRun)
    {
        std::printf("=== 部署前闸门 ===\n");
        for (const std::string& line : preflight.checked)
        {
            std::printf("  [通过] %s\n", line.c_str());
        }
        for (const std::string& warning : preflight.warnings)
        {
            std::printf("  [告警] %s\n", warning.c_str());
        }
        if (preflight.blocked)
        {
            std::printf("  [拒绝] %s\n", preflight.reason.c_str());
            std::printf("（干跑只报告，不中止；真实执行会在这里停下）\n");
        }
        std::printf("\n");

        const std::string report = osp::deploy::dryRunReport(plan);
        std::fputs(report.c_str(), stdout);

        // dry-run 也要能被闸门查：给出机器可读的产物
        if (reportPath != nullptr)
        {
            using nlohmann::json;

            json root;
            root["schemaVersion"] = 1;
            root["dryRun"] = true;
            root["planDirectory"] = plan.directory;
            root["planFile"] = plan.planFile;
            root["taskCount"] = plan.taskCount;
            root["phaseCount"] = static_cast<int>(plan.phases.size());

            json phases = json::array();
            for (const osp::deploy::Phase& phase : plan.phases)
            {
                json item;
                item["phase"] = phase.id;
                item["label"] = phase.label;
                item["script"] = phase.script;
                item["taskCount"] = phase.taskCount;

                json tasks = json::array();
                for (const osp::deploy::Task& task : phase.tasks)
                {
                    json entry;
                    entry["index"] = task.index;
                    entry["type"] = task.type;
                    entry["action"] = osp::deploy::taskTypeLabel(task.type);
                    entry["resolvedPath"] = task.resolvedPath;
                    entry["commandLine"] = task.commandLine;
                    entry["isCleanupCommand"] = task.isCleanupCommand;
                    entry["windowStyle"] = task.windowStyle;
                    entry["showWindow"] = task.showWindow;
                    entry["waitMode"] = task.waitMode;
                    entry["waitDescription"] = osp::deploy::describeWait(task);
                    entry["creationFlags"] = task.creationFlags;
                    entry["creationFlagsName"] = task.creationFlagsName;
                    entry["timeoutMs"] = task.timeoutMs;
                    entry["timeoutAction"] = task.timeoutAction;
                    tasks.push_back(std::move(entry));
                }
                item["tasks"] = std::move(tasks);
                phases.push_back(std::move(item));
            }
            root["phases"] = std::move(phases);

            std::string error;
            if (!writeTextFile(reportPath, root.dump(2) + "\n", error))
            {
                std::printf("%s\n", error.c_str());
                return 1;
            }
            std::printf("\ndry-run 报告: %s\n", reportPath);
        }

        std::printf(
            "RESULT: DRY-RUN OK  （%d 个阶段 / %d 个任务）\n",
            static_cast<int>(plan.phases.size()),
            plan.taskCount
        );
        return 0;
    }

    // 闸门不放行就停在这里，一个任务都不跑
    if (preflight.blocked)
    {
        std::printf("=== 部署前闸门 ===\n");
        for (const std::string& line : preflight.checked)
        {
            std::printf("  [通过] %s\n", line.c_str());
        }
        std::printf("  [拒绝] %s\n", preflight.reason.c_str());
        // SC 的 SA_Lab_ShowTip：被劫持时把提示弹给用户
        if (preflight.controllerHijacked && !preflight.tip.empty())
        {
            // SC 的 SA_Lab_ShowTip：被劫持时把提示弹给用户（GUI 子系统可弹窗）
            const int wideLen = ::MultiByteToWideChar(CP_UTF8, 0, preflight.tip.c_str(), -1, nullptr, 0);
            if (wideLen > 0)
            {
                std::wstring wideTip(static_cast<std::size_t>(wideLen), L'\0');
                ::MultiByteToWideChar(CP_UTF8, 0, preflight.tip.c_str(), -1, wideTip.data(), wideLen);
                ::MessageBoxW(nullptr, wideTip.c_str(), L"部署已中止", MB_OK | MB_ICONWARNING);
            }
        }
        std::printf("RESULT: FAILED（部署前校验未通过，未执行任何任务）\n");
        return 1;
    }
    for (const std::string& line : preflight.checked)
    {
        std::printf("  [通过] %s\n", line.c_str());
    }
    for (const std::string& warning : preflight.warnings)
    {
        std::printf("  [告警] %s\n", warning.c_str());
    }

    // 真实执行：图形页优先（若 presentation 要求且当前会话能开窗口），否则控制台单页
    const osp::deploy::Presentation presentation = osp::deploy::Presentation::fromPlan(plan);
    std::unique_ptr<osp::deploy::GraphProgressPage> graphPage;
    std::unique_ptr<osp::deploy::ConsoleProgressPage> consolePage;
    osp::deploy::Sink* sink = nullptr;

    const bool wantsGraph = plan.presentationModule != "console";
    if (wantsGraph && osp::deploy::graphicalPageAvailable())
    {
        graphPage = std::make_unique<osp::deploy::GraphProgressPage>(presentation, plan.taskCount);
        if (graphPage->ok())
        {
            sink = graphPage.get();
        }
        else
        {
            graphPage.reset();
            std::printf("图形进度页创建失败，退回控制台进度页\n");
        }
    }
    if (sink == nullptr)
    {
        consolePage = std::make_unique<osp::deploy::ConsoleProgressPage>(
            std::string("OpenSysprepTool 部署运行时  osptd ") + osp::versionString(), plan.taskCount
        );
        sink = consolePage.get();
    }

    osp::deploy::Options options;
    options.stopOnError = !continueOnError;
    options.logPath = logPath != nullptr ? std::string(logPath) : std::string();
    options.sink = sink;

    const osp::deploy::RunResult result = osp::deploy::execute(plan, options);

    if (reportPath != nullptr)
    {
        std::string error;
        if (!writeTextFile(reportPath, osp::deploy::toJson(result), error))
        {
            std::printf("%s\n", error.c_str());
            return 1;
        }
    }

    if (!result.error.empty())
    {
        std::printf("%s\n", result.error.c_str());
        return 1;
    }

    if (!result.ok)
    {
        std::printf(
            "执行失败：阶段 %s #%d 出错，共 %d 个任务失败（耗时 %llums）\n",
            result.abortedAtPhase.c_str(),
            result.abortedAtIndex,
            result.failedTasks,
            static_cast<unsigned long long>(result.elapsedMs)
        );
        if (!result.rollbackRecord.empty())
        {
            std::printf("已执行任务反序清单（%zu 条）:\n", result.rollbackRecord.size());
            for (const std::string& line : result.rollbackRecord)
            {
                std::printf("  %s\n", line.c_str());
            }
        }
        std::printf("RESULT: FAILED\n");
        return 1;
    }

    std::printf(
        "执行完成：%d 个任务全部成功（耗时 %llums）\n",
        result.executedTasks,
        static_cast<unsigned long long>(result.elapsedMs)
    );
    std::printf("RESULT: OK\n");
    return 0;
}

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

// 未提权时用 runas 重启，等待并返回真实退出码；控制台输出经临时文件回传。
static int relaunchElevatedAndWait(int argc, char** argv)
{
    if (isElevated())
    {
        return -1;
    }

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
    wchar_t tempDir[MAX_PATH] = {};
    wchar_t logPath[MAX_PATH] = {};
    ::GetTempPathW(MAX_PATH, tempDir);
    ::GetTempFileNameW(tempDir, L"osp", 0, logPath);
    const std::wstring command = L"/d /s /c \"\"" + std::wstring(exePath) + L"\" " + args + L" > \"" +
        std::wstring(logPath) + L"\" 2>&1\"";

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = command.c_str();
    sei.nShow = SW_HIDE;
    if (::ShellExecuteExW(&sei) == 0 || sei.hProcess == nullptr)
    {
        ::DeleteFileW(logPath);
        return -2;
    }

    DWORD exitCode = 1;
    ::WaitForSingleObject(sei.hProcess, INFINITE);
    ::GetExitCodeProcess(sei.hProcess, &exitCode);
    ::CloseHandle(sei.hProcess);

    std::ifstream log(logPath, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
    if (!bytes.empty())
    {
        std::fwrite(bytes.data(), 1, bytes.size(), stdout);
        std::fflush(stdout);
    }
    log.close();
    ::DeleteFileW(logPath);
    return static_cast<int>(exitCode);
}

} // namespace

int main(int argc, char** argv)
{
    // Windows 的 argv 是 ANSI 编码：中文路径在非中文代码页的系统上会变成 '?'。
    // 换成从 GetCommandLineW 取的 UTF-8 版本。
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

    // 只读开关不自动提权：--version / --help / --win7-compat / --dry-run 这些不需要管理员。
    // 只有真正需要管理员的操作（--plan 不带 --dry-run）才提权。
    const bool needsAdmin = argc >= 2 && std::strcmp(argv[1], "--plan") == 0;

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

    if (argc < 2)
    {
        printUsage();
        return 0;
    }

    const char* arg = argv[1];

    if (std::strcmp(arg, "--version") == 0)
    {
        printVersion();
        return 0;
    }

    if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0)
    {
        printUsage();
        return 0;
    }

    if (std::strcmp(arg, "--win7-compat") == 0)
    {
        return runWin7Compat(argc, argv);
    }

    if (std::strcmp(arg, "--preview-presentation") == 0)
    {
        return runPreviewPresentation(argc, argv);
    }

    if (std::strcmp(arg, "--dry-run") == 0)
    {
        return runDeploy(argc, argv, true);
    }

    if (std::strcmp(arg, "--plan") == 0)
    {
        return runDeploy(argc, argv, false);
    }

    std::printf("unknown option: %s\n\n", arg);
    printUsage();
    return 2;
}

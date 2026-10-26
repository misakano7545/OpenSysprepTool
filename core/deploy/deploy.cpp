#include "core/deploy.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

#include <nlohmann/json.hpp>

#include "core/log.hpp"
#include "core/win7.hpp"

namespace osp::deploy {
namespace {

// 超时被 TerminateProcess 终止时写回的退出码。
// waitResult == "timeout-killed" 才是权威信号，这个码只是给日志一个可读值。
constexpr DWORD kTimeoutKillExitCode = 1;

std::string toUtf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.c_str()), u8.size());
}

std::wstring toWide(const std::string& text)
{
    if (text.empty())
    {
        return std::wstring();
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0)
    {
        return std::wstring();
    }
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::string describeWin32Error(DWORD code)
{
    LPWSTR buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr
    );
    if (length == 0 || buffer == nullptr)
    {
        return std::string();
    }

    std::wstring wide(buffer, length);
    LocalFree(buffer);
    while (!wide.empty() && (wide.back() == L'\r' || wide.back() == L'\n' || wide.back() == L' '))
    {
        wide.pop_back();
    }

    const int size = WideCharToMultiByte(
        CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr
    );
    if (size <= 0)
    {
        return std::string();
    }
    std::string text(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), text.data(), size, nullptr, nullptr
    );
    return text;
}

std::string hex32(std::uint32_t value)
{
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "0x%08X", value);
    return std::string(buffer);
}

std::string taskFromJson(const nlohmann::json& item)
{
    return item.value("commandLine", std::string{});
}

// 单条任务的执行。返回的 TaskOutcome 是判定的唯一依据。
TaskOutcome runTask(const Phase& phase, const Task& task)
{
    TaskOutcome outcome;
    outcome.index = task.index;
    outcome.phase = phase.id;
    outcome.commandLine = task.commandLine;

    const std::uint64_t startedAt = win7::tickCountMs();

    std::wstring wide = toWide(task.commandLine);
    if (wide.empty())
    {
        outcome.waitResult = "launch-failed";
        outcome.message = "命令行无法转换为 UTF-16（空串或编码错误）";
        outcome.elapsedMs = win7::tickCountMs() - startedAt;
        return outcome;
    }

    std::vector<wchar_t> mutableCommandLine(wide.begin(), wide.end());
    mutableCommandLine.push_back(L'\0');

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    // 运行方式 -> wShowWindow，必须配 STARTF_USESHOWWINDOW 才生效
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = task.showWindow;

    PROCESS_INFORMATION process = {};

    // 等待策略的两列表：dwCreationFlags 原样传给 CreateProcess（宪章 §8 M4）
    if (CreateProcessW(
            nullptr,
            mutableCommandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            task.creationFlags,
            nullptr,
            nullptr,
            &startup,
            &process
        ) == 0)
    {
        const DWORD error = GetLastError();
        outcome.waitResult = "launch-failed";
        outcome.message = "CreateProcess 失败，GetLastError=" + std::to_string(error);
        const std::string detail = describeWin32Error(error);
        if (!detail.empty())
        {
            outcome.message += " " + detail;
        }
        outcome.elapsedMs = win7::tickCountMs() - startedAt;
        return outcome;
    }

    outcome.launched = true;
    outcome.pid = process.dwProcessId;

    if (task.waitMode == "none")
    {
        // 不等待：立刻关句柄，进程继续跑
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        outcome.waitResult = "not-waited";
        outcome.ok = true;
        outcome.message = "已启动，未等待（句柄已关闭）";
    }
    else
    {
        const DWORD timeoutMs = (task.waitMode == "timeout") ? static_cast<DWORD>(task.timeoutMs) : INFINITE;
        const DWORD waited = WaitForSingleObject(process.hProcess, timeoutMs);

        if (waited == WAIT_OBJECT_0)
        {
            DWORD code = 0;
            GetExitCodeProcess(process.hProcess, &code);
            outcome.exitCode = code;
            outcome.waitResult = "exited";
            outcome.ok = (code == 0);
            outcome.message = "已结束，退出码 " + std::to_string(code);
        }
        else if (waited == WAIT_TIMEOUT)
        {
            // 超时：按 timeoutAction 决定终止还是继续，两种结果都要留证据
            if (task.timeoutAction == "kill")
            {
                TerminateProcess(process.hProcess, kTimeoutKillExitCode);
                WaitForSingleObject(process.hProcess, 5000);
                DWORD code = 0;
                GetExitCodeProcess(process.hProcess, &code);
                outcome.exitCode = code;
                outcome.waitResult = "timeout-killed";
                outcome.ok = true; // 操作员显式配了超时，终止即为预期行为，不算失败
                outcome.message = "等待超时 " + std::to_string(task.timeoutMs) + "ms，已按 " +
                    task.timeoutAction + " 终止进程";
            }
            else
            {
                outcome.waitResult = "timeout-continued";
                outcome.ok = true;
                outcome.message = "等待超时 " + std::to_string(task.timeoutMs) + "ms，已按 " +
                    task.timeoutAction + " 继续后续执行";
            }
        }
        else
        {
            const DWORD error = GetLastError();
            outcome.waitResult = "launch-failed";
            outcome.ok = false;
            outcome.message = "WaitForSingleObject 失败，GetLastError=" + std::to_string(error);
        }

        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }

    outcome.elapsedMs = win7::tickCountMs() - startedAt;
    return outcome;
}

} // namespace

// ---------------------------------------------------------------- 计划加载

Plan load(const std::string& utf8Directory)
{
    Plan plan;
    plan.directory = utf8Directory;

    if (utf8Directory.empty())
    {
        plan.error = "运行计划目录为空";
        return plan;
    }

    const std::filesystem::path directory = std::filesystem::path(utf8Directory);
    const std::filesystem::path file = directory / "runplan.json";
    plan.planFile = toUtf8(file);

    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        plan.error = "读不到运行计划：" + plan.planFile + "（先跑 ospt --gen-runplan）";
        return plan;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(text);
    }
    catch (const nlohmann::json::exception& exc)
    {
        plan.error = std::string("runplan.json 不是合法 JSON：") + exc.what();
        return plan;
    }

    if (!root.contains("phases") || !root["phases"].is_array())
    {
        plan.error = "runplan.json 缺少 phases 数组";
        return plan;
    }

    // 呈现设置是可选段：老计划文件没有它时按默认值跑控制台进度页
    if (root.contains("presentation") && root["presentation"].is_object())
    {
        const auto& presentation = root["presentation"];
        plan.presentationModule = presentation.value("module", std::string("grid"));
        plan.presentationBackground = presentation.value("background", std::string("systemDefault"));
        plan.presentationTitle = presentation.value("titleText", std::string());
        plan.presentationImage = presentation.value("backgroundImage", std::string());
        plan.presentationSwitchSeconds = presentation.value("imageSwitchSeconds", 10);
        plan.presentationResolutionX = presentation.value("resolutionX", 0);
        plan.presentationResolutionY = presentation.value("resolutionY", 0);
        plan.presentationFontColor = presentation.value("fontColor", std::string());
    }

    for (const auto& item : root["phases"])
    {
        Phase phase;
        phase.id = item.value("phase", std::string{});
        phase.label = item.value("label", std::string{});
        phase.script = item.value("script", std::string{});

        if (item.contains("tasks") && item["tasks"].is_array())
        {
            for (const auto& entry : item["tasks"])
            {
                Task task;
                task.index = entry.value("index", 0);
                task.type = entry.value("type", std::string{});
                task.sourceTarget = entry.value("sourceTarget", std::string{});
                task.resolvedPath = entry.value("resolvedPath", std::string{});
                task.searchedDrive = entry.value("searchedDrive", std::string{});
                task.resolvedFromDriveTable = entry.value("resolvedFromDriveTable", false);
                task.isCleanupCommand = entry.value("isCleanupCommand", false);
                task.commandLine = taskFromJson(entry);
                task.windowStyle = entry.value("windowStyle", std::string{});
                task.showWindow = static_cast<std::uint16_t>(entry.value("showWindow", 1));
                task.showWindowName = entry.value("showWindowName", std::string{});
                task.waitStrategy = entry.value("waitStrategy", std::string{});
                task.creationFlags = entry.value("creationFlags", 0u);
                task.creationFlagsName = entry.value("creationFlagsName", std::string{});
                task.waitMode = entry.value("waitMode", std::string{});
                task.timeoutMs = entry.value("timeoutMs", 0);
                task.timeoutAction = entry.value("timeoutAction", std::string{});
                phase.tasks.push_back(std::move(task));
            }
        }

        phase.taskCount = static_cast<int>(phase.tasks.size());
        plan.taskCount += phase.taskCount;
        plan.phases.push_back(std::move(phase));
    }

    if (root.contains("warnings") && root["warnings"].is_array())
    {
        for (const auto& warning : root["warnings"])
        {
            plan.warnings.push_back(warning.get<std::string>());
        }
    }

    if (plan.phases.empty())
    {
        plan.error = "runplan.json 里没有任何阶段";
        return plan;
    }

    plan.ok = true;
    return plan;
}

// ---------------------------------------------------------------- 描述

const char* taskTypeLabel(const std::string& type)
{
    if (type == "run")
    {
        return "运行程序";
    }
    if (type == "reg")
    {
        return "导入注册表";
    }
    if (type == "deleteFile")
    {
        return "删除文件";
    }
    if (type == "deleteDir")
    {
        return "删除目录";
    }
    return "未知任务类型";
}

std::string describeWait(const Task& task)
{
    if (task.waitMode == "none")
    {
        return "不等待";
    }
    if (task.waitMode == "timeout")
    {
        return "自定义 " + std::to_string(task.timeoutMs) + "ms（动作：" +
            (task.timeoutAction == "kill" ? "终止" : "继续") + "）";
    }
    return "等待完成";
}

std::string dryRunReport(const Plan& plan)
{
    std::string out;
    out += "运行计划：" + plan.directory + "\n";
    out += "  阶段 " + std::to_string(plan.phases.size()) + " 个，任务 " + std::to_string(plan.taskCount) +
        " 个\n";

    if (!plan.phases.empty())
    {
        out += "  阶段顺序：";
        for (std::size_t i = 0; i < plan.phases.size(); ++i)
        {
            out += plan.phases[i].label + "（" + plan.phases[i].id + "）";
            if (i + 1 < plan.phases.size())
            {
                out += " -> ";
            }
        }
        out += "\n";
    }

    int phaseOrdinal = 0;
    for (const Phase& phase : plan.phases)
    {
        ++phaseOrdinal;

        char header[256] = {};
        std::snprintf(
            header,
            sizeof(header),
            "\n[%d/%zu] %s（%s）  %s — %d 个任务\n",
            phaseOrdinal,
            plan.phases.size(),
            phase.label.c_str(),
            phase.id.c_str(),
            phase.script.c_str(),
            phase.taskCount
        );
        out += header;

        if (phase.tasks.empty())
        {
            out += "      （该阶段没有任务，执行时直接跳过）\n";
            continue;
        }

        int ordinal = 0;
        for (const Task& task : phase.tasks)
        {
            ++ordinal;

            char prefix[64] = {};
            std::snprintf(prefix, sizeof(prefix), "      %d/%d  ", ordinal, phase.taskCount);
            out += prefix;
            out += taskTypeLabel(task.type);
            out += "：" + task.resolvedPath;
            if (task.isCleanupCommand)
            {
                out += "（善后处理）";
            }
            if (task.resolvedFromDriveTable && !task.searchedDrive.empty())
            {
                out += "（经驱动器表解析，命中 " + task.searchedDrive + "）";
            }
            out += "\n";

            out += "            命令：" + task.commandLine + "\n";
            out += "            等待：" + describeWait(task) + "\n";
            out += "            标志：" + task.creationFlagsName + " (" + hex32(task.creationFlags) + ")\n";

            char window[128] = {};
            std::snprintf(
                window,
                sizeof(window),
                "            窗口：%s (wShowWindow=%u)\n",
                task.showWindowName.c_str(),
                static_cast<unsigned>(task.showWindow)
            );
            out += window;
        }
    }

    if (!plan.warnings.empty())
    {
        out += "\n计划生成时的告警：\n";
        for (const std::string& warning : plan.warnings)
        {
            out += "  - " + warning + "\n";
        }
    }

    out += "\n以上为预览（--dry-run）：没有启动任何进程，也没有改动任何文件。\n";
    return out;
}

// ---------------------------------------------------------------- 执行

RunResult execute(const Plan& plan, const Options& options)
{
    RunResult result;
    result.totalTasks = plan.taskCount;

    if (!plan.ok)
    {
        result.error = plan.error;
        return result;
    }

    if (!options.logPath.empty())
    {
        if (!osp::log::openFile(options.logPath))
        {
            result.error = "无法打开日志文件：" + options.logPath + "（" + osp::log::lastError() + "）";
            return result;
        }
    }

    const std::uint64_t startedAt = win7::tickCountMs();
    const osp::win7::OsVersion version = osp::win7::osVersion();

    osp::log::info("osptd 开始执行运行计划：" + plan.planFile);
    osp::log::info(
        "系统：" + osp::win7::describe(version) + "，是否满足最低目标（Win7 SP1）：" +
        (osp::win7::isSupportedTarget(version) ? "是" : "否")
    );
    osp::log::info(
        "阶段 " + std::to_string(plan.phases.size()) + " 个，任务 " + std::to_string(plan.taskCount) +
        " 个，失败即停：" + (options.stopOnError ? "是" : "否")
    );

    bool aborted = false;
    int phaseOrdinal = 0;

    for (const Phase& phase : plan.phases)
    {
        ++phaseOrdinal;
        if (aborted)
        {
            break;
        }

        if (options.sink != nullptr)
        {
            options.sink->phaseStart(phase, phaseOrdinal, static_cast<int>(plan.phases.size()));
        }
        osp::log::info(
            "阶段 " + std::to_string(phaseOrdinal) + "/" + std::to_string(plan.phases.size()) + " 开始：" +
            phase.label + "（" + phase.id + "），" + std::to_string(phase.taskCount) + " 个任务"
        );

        int okCount = 0;
        int failedCount = 0;
        int ordinal = 0;

        for (const Task& task : phase.tasks)
        {
            ++ordinal;
            if (options.sink != nullptr)
            {
                options.sink->taskStart(phase, task, ordinal, phase.taskCount);
            }

            const TaskOutcome outcome = runTask(phase, task);
            result.outcomes.push_back(outcome);
            result.executedTasks += 1;

            osp::log::write(
                outcome.ok ? osp::log::Level::Info : osp::log::Level::Error,
                "[" + phase.id + " #" + std::to_string(task.index) + "] " + taskTypeLabel(task.type) + " " +
                    task.commandLine + " -> " + outcome.waitResult + "（" + outcome.message + "，" +
                    std::to_string(outcome.elapsedMs) + "ms" +
                    (outcome.launched ? "，pid=" + std::to_string(outcome.pid) : "") + "）"
            );

            if (outcome.waitResult == "timeout-killed" || outcome.waitResult == "timeout-continued")
            {
                // 两种超时结果都要留证据
                osp::log::warn(
                    "[" + phase.id + " #" + std::to_string(task.index) + "] 超时处理：" + outcome.waitResult +
                    " —— " + outcome.message
                );
            }

            if (options.sink != nullptr)
            {
                options.sink->taskFinish(phase, task, outcome);
            }

            if (outcome.ok)
            {
                ++okCount;
            }
            else
            {
                ++failedCount;
                result.failedTasks += 1;
                result.abortedAtPhase = phase.id;
                result.abortedAtIndex = task.index;
                osp::log::error(
                    "任务失败，位置：" + phase.id + " #" + std::to_string(task.index) + "：" + outcome.message
                );

                if (options.stopOnError)
                {
                    aborted = true;
                    break;
                }
            }
        }

        if (options.sink != nullptr)
        {
            options.sink->phaseFinish(phase, okCount, failedCount);
        }
        osp::log::info(
            "阶段结束：" + phase.id + " 成功 " + std::to_string(okCount) + " 个，失败 " +
            std::to_string(failedCount) + " 个"
        );
    }

    if (aborted)
    {
        // M5 的中止语义：停干净 + 留下可追溯的反序清单。
        // 真正的「撤销」需要宪章明确各任务类型的可逆性（见 §8 M5 待定项），
        // 这里不假装回滚已经发生。
        for (auto it = result.outcomes.rbegin(); it != result.outcomes.rend(); ++it)
        {
            result.rollbackRecord.push_back(
                "[" + it->phase + " #" + std::to_string(it->index) + "] " + it->commandLine + " -> " +
                it->waitResult
            );
        }
        osp::log::error(
            "执行已中止：阶段 " + result.abortedAtPhase + " #" + std::to_string(result.abortedAtIndex) +
            " 失败，后续任务与阶段未执行"
        );
        osp::log::info(
            "已执行任务反序清单（共 " + std::to_string(result.rollbackRecord.size()) + " 条）已写入报告与日志"
        );
    }

    result.elapsedMs = win7::tickCountMs() - startedAt;
    result.ok = (result.failedTasks == 0);

    osp::log::info(
        "osptd 结束：成功 " + std::to_string(result.executedTasks - result.failedTasks) + " 个，失败 " +
        std::to_string(result.failedTasks) + " 个，耗时 " + std::to_string(result.elapsedMs) + "ms"
    );

    if (!options.logPath.empty())
    {
        osp::log::close();
    }
    return result;
}

std::string toJson(const RunResult& result)
{
    using nlohmann::json;

    json root;
    root["schemaVersion"] = 1;
    root["ok"] = result.ok;
    root["error"] = result.error;
    root["totalTasks"] = result.totalTasks;
    root["executedTasks"] = result.executedTasks;
    root["failedTasks"] = result.failedTasks;
    root["abortedAtPhase"] = result.abortedAtPhase;
    root["abortedAtIndex"] = result.abortedAtIndex;
    root["elapsedMs"] = result.elapsedMs;

    json outcomes = json::array();
    for (const TaskOutcome& outcome : result.outcomes)
    {
        json item;
        item["index"] = outcome.index;
        item["phase"] = outcome.phase;
        item["commandLine"] = outcome.commandLine;
        item["launched"] = outcome.launched;
        item["pid"] = outcome.pid;
        item["exitCode"] = outcome.exitCode;
        item["waitResult"] = outcome.waitResult;
        item["elapsedMs"] = outcome.elapsedMs;
        item["ok"] = outcome.ok;
        item["message"] = outcome.message;
        outcomes.push_back(std::move(item));
    }
    root["outcomes"] = std::move(outcomes);
    root["rollbackRecord"] = result.rollbackRecord;

    return root.dump(2) + "\n";
}

} // namespace osp::deploy

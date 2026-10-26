#include "deploy_runner.hpp"

#include <cstdio>
#include <deque>
#include <vector>

#include <windows.h>

#include "core/win7.hpp"

namespace osp::ui {
namespace {

constexpr int kTailLines = 12;

std::wstring quoteArg(const std::string& text)
{
    // CreateProcess 的命令行需要自己加引号；路径里可能带空格与中文
    return L"\"" + osp::win7::pathFromUtf8(text).wstring() + L"\"";
}

} // namespace

DeployRunner::~DeployRunner()
{
    cancel();
    if (worker_.joinable())
    {
        worker_.join();
    }
}

void DeployRunner::setProgressCallback(ProgressCallback callback)
{
    onProgress_ = std::move(callback);
}

void DeployRunner::setFinishCallback(FinishCallback callback)
{
    onFinish_ = std::move(callback);
}

bool DeployRunner::start(const std::string& osptdPath, const std::string& planDir, std::string& error)
{
    if (running_.load())
    {
        error = "上一次部署还在运行";
        return false;
    }
    if (osptdPath.empty() || planDir.empty())
    {
        error = "osptd 路径或运行计划目录为空";
        return false;
    }
    if (!std::filesystem::exists(osp::win7::pathFromUtf8(osptdPath)))
    {
        error = "找不到 osptd.exe：" + osptdPath;
        return false;
    }

    cancelRequested_.store(false);
    running_.store(true);
    worker_ = std::thread(&DeployRunner::run, this, osptdPath, planDir);
    return true;
}

void DeployRunner::cancel()
{
    cancelRequested_.store(true);
    if (processHandle_ != nullptr)
    {
        ::TerminateProcess(static_cast<HANDLE>(processHandle_), 1);
    }
}

void DeployRunner::wait(int timeoutMs)
{
    if (!worker_.joinable())
    {
        return;
    }
    // std::thread 没有带超时的 join；这里用轮询，超时后仍然 join（调用方通常不超时）
    const DWORD deadline = ::GetTickCount() + static_cast<DWORD>(timeoutMs > 0 ? timeoutMs : 0);
    while (running_.load() && ::GetTickCount() < deadline)
    {
        ::Sleep(20);
    }
    worker_.join();
}

void DeployRunner::run(std::string osptdPath, std::string planDir)
{
    DeployRunSummary summary;

    // 计划目录必须先转绝对路径：下面会把子进程的工作目录设成它，
    // 相对路径在换目录之后会二次拼接（实测变成 build/m10/prog/build/m10/prog）。
    {
        std::error_code ec;
        const std::filesystem::path absolute =
            std::filesystem::absolute(osp::win7::pathFromUtf8(planDir), ec);
        if (!ec)
        {
            planDir = osp::win7::toUtf8Path(absolute);
        }
    }

    SECURITY_ATTRIBUTES attributes = {};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!::CreatePipe(&readPipe, &writePipe, &attributes, 0))
    {
        summary.error = "创建管道失败，GetLastError=" + std::to_string(::GetLastError());
        running_.store(false);
        if (onFinish_)
        {
            onFinish_(summary);
        }
        return;
    }
    ::SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION process = {};

    // 命令行：osptd --no-ui --progress-json --plan "<dir>"
    // --no-ui 保证子进程不弹任何窗口（它是控制台子系统，但 GUI 提示框由它自己决定）
    std::wstring commandLine = quoteArg(osptdPath) + L" --no-ui --progress-json --plan " + quoteArg(planDir);
    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    // 工作目录取计划目录：runplan.json 与阶段脚本都用相对路径引用同目录内容
    const std::wstring workingDir = osp::win7::pathFromUtf8(planDir).wstring();

    const BOOL created = ::CreateProcessW(
        nullptr,
        mutableCommandLine.data(),
        nullptr,
        nullptr,
        TRUE, // 继承 stdout 管道
        CREATE_NO_WINDOW,
        nullptr,
        workingDir.c_str(),
        &startup,
        &process
    );

    if (!created)
    {
        summary.error = "启动 osptd 失败，GetLastError=" + std::to_string(::GetLastError());
        ::CloseHandle(readPipe);
        ::CloseHandle(writePipe);
        running_.store(false);
        if (onFinish_)
        {
            onFinish_(summary);
        }
        return;
    }

    processHandle_ = process.hProcess;
    summary.started = true;
    ::CloseHandle(writePipe); // 我们这一端必须关掉，否则读端永远等不到 EOF

    // 按行读：不做整体缓冲，进度要尽快到界面
    std::string pending;
    std::deque<std::string> tail;
    char buffer[4096];
    bool sawResult = false;

    for (;;)
    {
        DWORD read = 0;
        const BOOL ok = ::ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr);
        if (!ok || read == 0)
        {
            break;
        }
        pending.append(buffer, read);

        std::size_t newline = pending.find('\n');
        while (newline != std::string::npos)
        {
            std::string line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }

            const osp::deploy::ProgressEvent event = osp::deploy::parseProgressLine(line);
            if (event.parsed)
            {
                if (event.event == "result")
                {
                    sawResult = true;
                    summary.ok = event.ok;
                    summary.executed = event.executed;
                    summary.failed = event.failed;
                    summary.elapsedMs = event.elapsedMs;
                }
                if (onProgress_)
                {
                    onProgress_(event);
                }
            }
            else if (!line.empty())
            {
                // 普通人读输出留最后若干行，出问题时能看出卡在哪一步
                tail.push_back(line);
                while (static_cast<int>(tail.size()) > kTailLines)
                {
                    tail.pop_front();
                }
            }
            newline = pending.find('\n');
        }
    }

    // 收尾：还剩最后一行没换行的情况
    if (!pending.empty())
    {
        const osp::deploy::ProgressEvent event = osp::deploy::parseProgressLine(pending);
        if (event.parsed && onProgress_)
        {
            onProgress_(event);
        }
        else if (!event.parsed)
        {
            tail.push_back(pending);
        }
    }

    ::WaitForSingleObject(process.hProcess, 60000);
    DWORD exitCode = 1;
    ::GetExitCodeProcess(process.hProcess, &exitCode);
    summary.exitCode = static_cast<int>(exitCode);

    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    ::CloseHandle(readPipe);
    processHandle_ = nullptr;

    for (const std::string& line : tail)
    {
        summary.tail += line + "\n";
    }

    // 没收到 result 行却退出为 0，说明进度通道断了；按失败报，不能当成成功
    if (!summary.error.empty())
    {
        summary.ok = false;
    }
    else if (exitCode != 0)
    {
        summary.ok = false;
        if (summary.error.empty())
        {
            summary.error = "osptd 退出码 " + std::to_string(exitCode);
        }
    }
    else if (!sawResult)
    {
        summary.ok = false;
        summary.error = "osptd 正常退出但没有回报最终结果（进度通道可能被截断）";
    }

    if (cancelRequested_.load())
    {
        summary.ok = false;
        summary.error = "部署已被用户中止";
    }

    running_.store(false);
    if (onFinish_)
    {
        onFinish_(summary);
    }
}

} // namespace osp::ui

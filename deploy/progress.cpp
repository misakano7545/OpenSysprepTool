#include "progress.hpp"

#include <algorithm>
#include <cstdio>

#include "core/win7.hpp"

namespace osp::deploy {
namespace {

constexpr int kBarWidth = 24;
constexpr int kRecentLines = 3;

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

void writeWide(HANDLE handle, const std::wstring& text)
{
    if (text.empty())
    {
        return;
    }
    DWORD written = 0;
    WriteConsoleW(handle, text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr);
}

std::string trimTo(const std::string& text, std::size_t limit)
{
    if (text.size() <= limit)
    {
        return text;
    }
    if (limit <= 3)
    {
        return text.substr(0, limit);
    }
    return text.substr(0, limit - 3) + "...";
}

} // namespace

std::string formatDuration(std::uint64_t milliseconds)
{
    const std::uint64_t totalSeconds = milliseconds / 1000;
    char buffer[32] = {};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%02llu:%02llu",
        static_cast<unsigned long long>(totalSeconds / 60),
        static_cast<unsigned long long>(totalSeconds % 60)
    );
    return std::string(buffer);
}

std::string progressBar(int percent, int width)
{
    if (width < 4)
    {
        width = 4;
    }
    const int clamped = std::clamp(percent, 0, 100);
    const int filled = (clamped * (width - 2)) / 100;

    std::string bar = "[";
    bar.append(static_cast<std::size_t>(filled), '#');
    bar.append(static_cast<std::size_t>((width - 2) - filled), '-');
    bar += "]";
    return bar;
}

ConsoleProgressPage::ConsoleProgressPage(std::string title, int totalTasks)
    : title_(std::move(title)), totalTasks_(totalTasks), startedAtMs_(osp::win7::tickCountMs())
{
    handle_ = GetStdHandle(STD_OUTPUT_HANDLE);

    DWORD mode = 0;
    // GetConsoleMode 失败 = stdout 不是控制台（重定向或 CI），走降级路径
    console_ = (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) && GetConsoleMode(handle_, &mode) != 0;

    if (!console_)
    {
        return;
    }

    CONSOLE_SCREEN_BUFFER_INFO info = {};
    if (GetConsoleScreenBufferInfo(handle_, &info) != 0)
    {
        origin_ = info.dwCursorPosition;
    }

    CONSOLE_CURSOR_INFO cursor = {};
    if (GetConsoleCursorInfo(handle_, &cursor) != 0)
    {
        cursor.bVisible = FALSE;
        cursorHidden_ = SetConsoleCursorInfo(handle_, &cursor) != 0;
    }
}

ConsoleProgressPage::~ConsoleProgressPage()
{
    if (!console_)
    {
        return;
    }
    // 收尾：把光标放回进度页下方，恢复光标可见
    if (drawnLines_ > 0)
    {
        SetConsoleCursorPosition(handle_, COORD{origin_.X, static_cast<SHORT>(origin_.Y + drawnLines_)});
    }
    if (cursorHidden_)
    {
        CONSOLE_CURSOR_INFO cursor = {};
        if (GetConsoleCursorInfo(handle_, &cursor) != 0)
        {
            cursor.bVisible = TRUE;
            SetConsoleCursorInfo(handle_, &cursor);
        }
    }
}

void ConsoleProgressPage::addRecent(const std::string& line)
{
    recent_.push_back(line);
    while (recent_.size() > static_cast<std::size_t>(kRecentLines))
    {
        recent_.erase(recent_.begin());
    }
}

std::vector<std::string> ConsoleProgressPage::composeLines() const
{
    std::vector<std::string> lines;
    lines.push_back(title_);

    const int percent = totalTasks_ == 0 ? 100 : (doneTasks_ * 100) / totalTasks_;

    char progress[256] = {};
    std::snprintf(
        progress,
        sizeof(progress),
        "%s %3d%%   已用 %s   成功 %d  失败 %d",
        progressBar(percent, kBarWidth).c_str(),
        percent,
        formatDuration(osp::win7::tickCountMs() - startedAtMs_).c_str(),
        doneTasks_ - failedTasks_,
        failedTasks_
    );
    lines.push_back(progress);

    char phase[256] = {};
    std::snprintf(phase, sizeof(phase), "阶段 %d/%d  %s", phaseOrdinal_, phaseTotal_, phaseLabel_.c_str());
    if (!phaseSummary_.empty())
    {
        lines.push_back(std::string(phase) + "  " + phaseSummary_);
    }
    else
    {
        lines.push_back(phase);
    }

    char task[256] = {};
    std::snprintf(
        task, sizeof(task), "任务 %d/%d  %s", taskOrdinal_, taskTotal_, trimTo(taskText_, 60).c_str()
    );
    lines.push_back(task);

    for (const std::string& line : recent_)
    {
        lines.push_back("  " + trimTo(line, 72));
    }

    return lines;
}

void ConsoleProgressPage::render(const std::vector<std::string>& lines)
{
    if (!console_)
    {
        return;
    }

    SetConsoleCursorPosition(handle_, origin_);

    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        std::string padded = lines[i];
        // 补空格覆盖上一帧更长的内容，免得残留
        if (padded.size() < 79)
        {
            padded.append(79 - padded.size(), ' ');
        }
        writeWide(handle_, toWide(padded));
        writeWide(handle_, L"\r\n");
    }

    drawnLines_ = static_cast<int>(lines.size());
}

void ConsoleProgressPage::finishLine()
{
    if (!console_)
    {
        // 降级路径：逐行输出，供日志与 CI 查看
        for (const std::string& line : composeLines())
        {
            std::fputs(line.c_str(), stdout);
            std::fputc('\n', stdout);
        }
        std::fflush(stdout);
    }
}

void ConsoleProgressPage::phaseStart(const Phase& phase, int ordinal, int total)
{
    phaseOrdinal_ = ordinal;
    phaseTotal_ = total;
    phaseLabel_ = phase.label + "（" + phase.id + "）";
    phaseSummary_.clear();
    taskOrdinal_ = 0;
    taskTotal_ = phase.taskCount;
    taskText_.clear();
    addRecent("阶段开始：" + phaseLabel_);
    if (!console_)
    {
        finishLine();
    }
    else
    {
        render(composeLines());
    }
}

void ConsoleProgressPage::taskStart(const Phase& phase, const Task& task, int ordinal, int total)
{
    (void)phase;
    taskOrdinal_ = ordinal;
    taskTotal_ = total;
    taskText_ = std::string(deploy::taskTypeLabel(task.type)) + " " + task.resolvedPath;
    if (!console_)
    {
        finishLine();
    }
    else
    {
        render(composeLines());
    }
}

void ConsoleProgressPage::taskFinish(const Phase& phase, const Task& task, const TaskOutcome& outcome)
{
    (void)phase;
    (void)task;
    ++doneTasks_;
    if (!outcome.ok)
    {
        ++failedTasks_;
        addRecent("失败：" + outcome.message);
    }
    else if (outcome.waitResult == "timeout-killed" || outcome.waitResult == "timeout-continued")
    {
        addRecent("超时：" + outcome.waitResult);
    }
    else
    {
        addRecent(outcome.waitResult + "（" + std::to_string(outcome.elapsedMs) + "ms）");
    }
    if (!console_)
    {
        finishLine();
    }
    else
    {
        render(composeLines());
    }
}

void ConsoleProgressPage::phaseFinish(const Phase& phase, int okCount, int failedCount)
{
    (void)phase;
    char summary[96] = {};
    std::snprintf(summary, sizeof(summary), "（成功 %d，失败 %d）", okCount, failedCount);
    phaseSummary_ = summary;
    addRecent("阶段结束" + phaseSummary_);
    if (!console_)
    {
        finishLine();
    }
    else
    {
        render(composeLines());
    }
}

} // namespace osp::deploy

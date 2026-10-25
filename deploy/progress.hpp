#pragma once

// 单页进度页（宪章 §8 M5 交付之一）
//
// 用 Win32 控制台 API 在固定位置原地重绘：整页只有一块区域，不滚屏。
// 之所以不用 ANSI 转义：Windows 7 的 conhost 不认 VT 序列，会打出一堆乱码，
// 而 Win7 正是本里程碑的目标系统。
//
// 被重定向到文件或 CI 时（GetConsoleMode 失败）自动降级成逐行输出，
// 免得往日志里灌几十次半截重绘。

#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>

#include "core/deploy.hpp"

namespace osp::deploy {

class ConsoleProgressPage : public Sink
{
public:
    ConsoleProgressPage(std::string title, int totalTasks);
    ~ConsoleProgressPage() override;

    ConsoleProgressPage(const ConsoleProgressPage&) = delete;
    ConsoleProgressPage& operator=(const ConsoleProgressPage&) = delete;

    void phaseStart(const Phase& phase, int ordinal, int total) override;
    void taskStart(const Phase& phase, const Task& task, int ordinal, int total) override;
    void taskFinish(const Phase& phase, const Task& task, const TaskOutcome& outcome) override;
    void phaseFinish(const Phase& phase, int okCount, int failedCount) override;

private:
    void addRecent(const std::string& line);
    std::vector<std::string> composeLines() const;
    void render(const std::vector<std::string>& lines);
    void finishLine();

    std::string title_;
    int totalTasks_ = 0;
    int doneTasks_ = 0;
    int failedTasks_ = 0;

    int phaseOrdinal_ = 0;
    int phaseTotal_ = 0;
    std::string phaseLabel_;
    int taskOrdinal_ = 0;
    int taskTotal_ = 0;
    std::string taskText_;
    std::string phaseSummary_;

    std::vector<std::string> recent_;
    std::uint64_t startedAtMs_ = 0;

    bool console_ = false;
    HANDLE handle_ = nullptr;
    COORD origin_ = {0, 0};
    int drawnLines_ = 0;
    bool cursorHidden_ = false;
};

// 把毫秒数格式化成 mm:ss
std::string formatDuration(std::uint64_t milliseconds);

// 画一个定宽进度条，例如 [##########----------]
std::string progressBar(int percent, int width);

} // namespace osp::deploy

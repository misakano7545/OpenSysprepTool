#pragma once

// 部署进度的结构化通道（osptd -> ospt）
//
// 背景：SC 是同进程跑封装，`运行` 页的两条进度条与说明文字直接由工作线程刷新。
// 我们的部署在 `osptd` 里执行，所以需要一条机器可读的通道把进度传回界面。
//
// 为什么用「一行一个 JSON」而不是自定义文本：osptd 的普通输出给人看、会变；
// 进度行是接口，必须稳定且能被单测钉住。按行读、按 JSON 解析，坏行跳过并计数
// （不能因为一行进度把整个部署界面弄崩）。
//
// 字段命名与 core/deploy 的 Sink 接口一一对应：
//   phaseStart  -> event=phaseStart  phase/originalIndex/phaseTotal
//   taskStart   -> event=taskStart   phase/taskIndex/taskTotal/finished/totalTasks
//   taskFinish  -> event=taskFinish  phase/taskIndex/ok/elapsedMs
//   phaseFinish -> event=phaseFinish phase/ok/failed
//   result      -> event=result      ok/executed/failed/elapsedMs

#include <string>

namespace osp::deploy {

struct ProgressEvent
{
    std::string event; // phaseStart / taskStart / taskFinish / phaseFinish / result
    std::string phase;
    int phaseOrdinal = 0;
    int phaseTotal = 0;
    int taskIndex = 0;  // 阶段内第几个任务（1 起）
    int taskTotal = 0;  // 阶段内任务总数
    int finished = 0;   // 全局已完成任务数
    int totalTasks = 0; // 全局任务总数
    bool ok = true;
    int failed = 0;
    int executed = 0;
    std::uint64_t elapsedMs = 0;

    // 解析失败时 ok=false，error 写明原因（"不是 JSON" / "缺少 event"）。
    bool parsed = false;
    std::string error;
};

// 解析一行进度输出。行首尾空白会被忽略；不是进度行（比如普通日志）返回
// parsed=false —— 调用方据此跳过，不能当成错误。
ProgressEvent parseProgressLine(const std::string& line);

// 总体完成百分比（0..100）。totalTasks<=0 时返回 0。
int overallPercent(int finished, int totalTasks);

// 阶段内完成百分比（0..100）。
int phasePercent(int taskIndex, int taskTotal);

} // namespace osp::deploy

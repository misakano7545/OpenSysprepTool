#pragma once

// 部署运行时（宪章 §8 M5）
//
// osptd 不重新解析配置，只消费 ospt --gen-runplan 产出的 run/runplan.json ——
// 路径、命令行、等待策略、创建标志在 M4 就已定下来，这里只负责**执行**它们。
// 单一事实来源的好处是：验收脚本查过的产物，就是运行时真正执行的东西。

#include <cstdint>
#include <string>
#include <vector>

#include "core/config.hpp"

namespace osp::deploy {

// ---------------------------------------------------------------- 计划模型

struct Task
{
    int index = 0;
    std::string type; // run / reg / deleteFile / deleteDir
    std::string sourceTarget;
    std::string resolvedPath;
    std::string searchedDrive;
    bool resolvedFromDriveTable = false;
    bool isCleanupCommand = false; // 「善后处理」追加的删除命令
    std::string commandLine;
    std::string windowStyle;
    std::uint16_t showWindow = 1;
    std::string showWindowName;
    std::string waitStrategy;
    std::uint32_t creationFlags = 0;
    std::string creationFlagsName;
    std::string waitMode; // none / infinite / timeout
    int timeoutMs = 0;
    std::string timeoutAction; // kill / continue
};

struct Phase
{
    std::string id; // preinstall / install / postinstall / logon / desktop
    std::string label;
    std::string script;
    int taskCount = 0;
    std::vector<Task> tasks;
};

struct Plan
{
    bool ok = false;
    std::string error;
    std::string directory; // run/
    std::string planFile;  // run/runplan.json
    int taskCount = 0;
    std::vector<Phase> phases;
    // 部署过程的呈现设置（由 --gen-runplan 从配置写进计划文件）
    std::string presentationModule; // grid / trueTrans / colorBar / image
    std::string presentationBackground;
    std::string presentationTitle;
    std::string presentationImage;
    int presentationSwitchSeconds = 10;
    int presentationResolutionX = 0;
    int presentationResolutionY = 0;
    std::string presentationFontColor; // "#RRGGBB"，空 = 默认
    // 部署校验（SC 的 Cb_Tampering / Btn_DisableVmrun / SA_Rb_hijack_*）
    bool verifyIntegrity = false;
    std::vector<config::DeploySettings::TamperEntry> tamperFiles;
    bool blockVirtualMachine = false;
    std::string hijackMode = "terminate";
    std::string hijackTip;
    bool hijackShowTip = true;
    std::string controllerMd5; // 生成计划时 osptd 自身的 MD5，用于判断主控是否被替换
    std::vector<std::string> warnings;
};

// 部署前闸门的判定结果
struct PreflightResult
{
    bool blocked = false;     // true = 拒绝部署
    std::string reason;       // 拒绝原因（原样给用户看）
    std::string tamperedFile; // 命中的被篡改文件（空 = 不是防篡改导致的）
    bool controllerHijacked = false;
    std::string tip;                  // SC 的「被劫持弹出提示告之用户」
    std::vector<std::string> checked; // 通过的检查项，便于逐条打印
    std::vector<std::string> warnings;
};

// 执行任何任务前先跑这三道闸门：虚拟机检测、文件防篡改、部署主控校验。
// 纯只读，不改目标机。
PreflightResult preflight(const Plan& plan);

// 当前进程有没有可用的交互桌面（窗口站是否可见）。
// 用来决定「能不能弹提示框」：CI / 计划任务里没有可见窗口站，弹框会永久阻塞。
bool interactiveDesktopAvailable();

// 从运行计划目录读 run/runplan.json（UTF-8 路径）
Plan load(const std::string& utf8Directory);

// 任务类型的中文动作描述：运行程序 / 导入注册表 / 删除文件 / 删除目录
const char* taskTypeLabel(const std::string& type);

// --dry-run 的报告：逐阶段逐任务说明「将要执行什么」，不需要任何权限
std::string dryRunReport(const Plan& plan);

// 把该任务的等待策略落到具体语义上，供 dry-run 与日志共用
std::string describeWait(const Task& task);

// ---------------------------------------------------------------- 执行

struct TaskOutcome
{
    int index = 0;
    std::string phase;
    std::string commandLine;
    bool launched = false;
    std::uint32_t pid = 0;
    std::uint32_t exitCode = 0;
    // exited          等待完成，进程正常结束
    // not-waited      不等待，句柄已关
    // timeout-killed  超时且 timeoutAction=kill，进程被终止
    // timeout-continued 超时且 timeoutAction=continue，进程留着继续跑
    // launch-failed   CreateProcess 失败
    std::string waitResult;
    std::uint64_t elapsedMs = 0;
    bool ok = false;
    std::string message;
};

// 进度汇报回调。dry-run 不触发；真实执行时由 deploy/ 的进度页实现。
struct Sink
{
    virtual ~Sink() = default;
    virtual void phaseStart(const Phase& phase, int ordinal, int total)
    {
        (void)phase;
        (void)ordinal;
        (void)total;
    }
    virtual void taskStart(const Phase& phase, const Task& task, int ordinal, int total)
    {
        (void)phase;
        (void)task;
        (void)ordinal;
        (void)total;
    }
    virtual void taskFinish(const Phase& phase, const Task& task, const TaskOutcome& outcome)
    {
        (void)phase;
        (void)task;
        (void)outcome;
    }
    virtual void phaseFinish(const Phase& phase, int okCount, int failedCount)
    {
        (void)phase;
        (void)okCount;
        (void)failedCount;
    }
};

struct Options
{
    bool stopOnError = true; // 失败即中止后续任务与阶段
    std::string logPath;     // 为空则不落盘（日志仍走 sink 与返回值）
    Sink* sink = nullptr;
};

struct RunResult
{
    bool ok = false;
    std::string error;
    int totalTasks = 0;
    int executedTasks = 0;
    int failedTasks = 0;
    std::string abortedAtPhase;
    int abortedAtIndex = 0;
    std::vector<TaskOutcome> outcomes;
    // 中止时留下的「已执行任务」反序清单，供续跑与人工回滚判断。
    // M5 只做到「停干净 + 可追溯」；真正的撤销语义需要宪章明确（见 §8 M5 待定项）。
    std::vector<std::string> rollbackRecord;
    std::uint64_t elapsedMs = 0;
};

RunResult execute(const Plan& plan, const Options& options);

// --report 的内容
std::string toJson(const RunResult& result);

} // namespace osp::deploy

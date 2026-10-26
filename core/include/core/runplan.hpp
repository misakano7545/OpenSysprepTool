#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/config.hpp"

namespace osp::runplan {

// 运行计划调度器：把 config::RunPlanTask 解析成可执行计划。
//
// 维度取值来自需求基线《功能规格》§三：
//   5 阶段 × 4 任务类型 × 4 运行方式 × 3 等待策略 × 4 路径类型 = 960 种组合
//
// 等待策略的映射依据宪章 §8 M4 的两列表（dwCreationFlags + 等待方式）。
// 路径解析不依赖本机实际盘符：驱动器表由调用方提供，本机枚举只是缺省值。

// ---------------------------------------------------------------- 枚举

enum class Phase
{
    PreInstall,  // 部署前
    Install,     // 部署中
    PostInstall, // 部署后
    Logon,       // 登录时
    Desktop      // 进桌面
};

enum class TaskType
{
    Run,        // 运行程序
    ImportReg,  // 导注册表
    RemoveFile, // 删除文件（配置字符串仍是 deleteFile）
    RemoveDir   // 删除目录（配置字符串仍是 deleteDir）
};

enum class WindowStyle
{
    Normal,    // 正常
    Hidden,    // 隐藏
    Minimized, // 最小化
    Maximized  // 最大化
};

enum class WaitStrategy
{
    WaitExit,   // 等待完成  -> dwCreationFlags=0，WaitForSingleObject(INFINITE)
    NoWait,     // 不等待    -> CREATE_NEW_PROCESS_GROUP，立即 CloseHandle
    WaitTimeout // 自定义秒数 -> dwCreationFlags=0，WaitForSingleObject(timeoutMs)
};

enum class PathKind
{
    SystemPath, // 系统路径
    UsbDrive,   // 外置 U 盘
    CdRom,      // 外置光盘
    AllDrives   // 所有盘符
};

inline constexpr int kPhaseCount = 5;
inline constexpr int kTaskTypeCount = 4;
inline constexpr int kWindowStyleCount = 4;
inline constexpr int kWaitStrategyCount = 3;
inline constexpr int kPathKindCount = 4;
inline constexpr int kCombinationCount =
    kPhaseCount * kTaskTypeCount * kWindowStyleCount * kWaitStrategyCount * kPathKindCount;

// 全部 5 个阶段，按 Phase 枚举顺序
const std::vector<Phase>& allPhases();
const std::vector<TaskType>& allTaskTypes();
const std::vector<WindowStyle>& allWindowStyles();
const std::vector<WaitStrategy>& allWaitStrategies();
const std::vector<PathKind>& allPathKinds();

// 字符串 <-> 枚举。解析失败时 error 填入原因并返回 false。
const char* phaseId(Phase phase);
const char* phaseLabel(Phase phase);
const char* taskTypeId(TaskType type);
const char* windowStyleId(WindowStyle style);
const char* waitStrategyId(WaitStrategy strategy);
const char* pathKindId(PathKind kind);

bool parsePhase(const std::string& text, Phase& out, std::string& error);
bool parseTaskType(const std::string& text, TaskType& out, std::string& error);
bool parseWindowStyle(const std::string& text, WindowStyle& out, std::string& error);
bool parseWaitStrategy(const std::string& text, WaitStrategy& out, std::string& error);
bool parsePathKind(const std::string& text, PathKind& out, std::string& error);

// ---------------------------------------------------------------- 映射表

// 宪章 §8 M4 的等待策略两列表
struct WaitMapping
{
    std::uint32_t creationFlags;   // CreateProcess 的 dwCreationFlags
    const char* creationFlagsName; // 宏名；标志为 0 时是 "0"
    const char* waitMode;          // none / infinite / timeout
    std::string waitDescription;   // 参照实现说明
};

WaitMapping waitMapping(WaitStrategy strategy, int timeoutMs);

// 运行方式 -> STARTUPINFO.wShowWindow
std::uint16_t showWindowValue(WindowStyle style);
const char* showWindowName(WindowStyle style);

// ---------------------------------------------------------------- 驱动器表

struct DriveEntry
{
    std::string root;        // "C:\\"
    std::string volumeLabel; // 卷标，可能为空
    std::string driveType;   // fixed / removable / cdrom / network / ramdisk / unknown
    bool systemDrive = false;
};

struct DriveTable
{
    std::string source; // file / local
    std::vector<DriveEntry> drives;
};

// 枚举本机驱动器（source 置为 local）
DriveTable enumerateLocalDrives();

// 从 JSON 文本读取驱动器表（source 置为 file）；失败返回 false
bool parseDriveTable(const std::string& json, DriveTable& out, std::string& error);

// 从「计划文件」里取驱动器表：顶层 driveTable 对象或顶层 drives 数组。
// 计划文件里本就没有该字段时返回 false 且不填 error；字段存在但格式错时填 error 并返回 false。
bool driveTableFromPlan(const std::string& planJson, DriveTable& out, std::string& error);

// 按路径类型挑选候选盘符。顺序稳定，便于断言。
std::vector<const DriveEntry*> selectDrives(const DriveTable& table, PathKind kind);
// 卷标匹配的候选：先按卷标过滤，卷标为空则退回按驱动器类型
std::vector<const DriveEntry*>
selectDrives(const DriveTable& table, PathKind kind, const std::string& expectedVolumeLabel);

// ---------------------------------------------------------------- 解析结果

struct ResolvedTask
{
    int index = 0; // 阶段内序号，从 1 起
    TaskType type = TaskType::Run;
    std::string sourceTarget;            // 配置里的原始 target
    std::string resolvedPath;            // 解析后的路径；allDrives 时为该候选盘符下的路径
    bool resolvedFromDriveTable = false; // 是否经驱动器表解析
    std::string searchedDrive;           // 命中的盘符，未命中为空
    std::string commandLine;             // 最终命令行
    bool isCleanupCommand = false;       // 是否为「善后处理」追加的删除命令
    WindowStyle windowStyle = WindowStyle::Normal;
    std::uint16_t showWindow = 1;
    WaitStrategy waitStrategy = WaitStrategy::WaitExit;
    std::uint32_t creationFlags = 0;
    std::string creationFlagsName;
    std::string waitMode;
    int timeoutMs = 0;
    std::string timeoutAction; // kill / continue
};

struct PhasePlan
{
    Phase phase = Phase::PreInstall;
    std::string scriptName; // 例如 "postinstall.cmd"
    std::vector<ResolvedTask> tasks;
};

// 部署过程的呈现设置：osptd 用它在目标机上画进度页。
// 放在计划文件里，是为了让 osptd 仍然只消费 runplan.json 这一份事实来源。
struct DeployChecks
{
    // SC 的 Cb_Tampering：部署时校验被锁定文件的 MD5，不匹配就提示并中止
    bool verifyIntegrity = false;
    std::vector<config::DeploySettings::TamperEntry> tamperFiles;
    // SC 的 Btn_DisableVmrun：禁止在虚拟机里部署
    bool blockVirtualMachine = false;
    // SC 的 SA_Rb_hijack_type1/type2：部署主控被劫持时继续还是终止
    std::string hijackMode = "terminate";
    std::string hijackTip;
    // 部署主控（osptd.exe）的 MD5，生成计划时写入
    std::string controllerMd5;
};

struct Presentation
{
    std::string module;     // grid / trueTrans / colorBar / image
    std::string background; // single / multi / systemDefault
    std::string titleText;
    std::string backgroundImage; // 单图/多图时的图片路径或目录
    int imageSwitchSeconds = 10;
    int resolutionX = 0; // 0 = 不改分辨率
    int resolutionY = 0;
    std::string fontColor; // "#RRGGBB"，空 = 默认色
};

struct PlanResult
{
    bool ok = false;
    std::string error;
    DriveTable driveTable;
    std::vector<PhasePlan> phases; // 固定 5 个，按 Phase 顺序
    Presentation presentation;
    DeployChecks checks;
    std::vector<std::string> warnings;
};

// 构建计划。任一任务无法解析（阶段/类型/运行方式/等待策略/路径未知，
// 或路径类型在驱动器表中找不到候选）即返回 ok=false 并给出 error。
PlanResult build(const osp::config::Config& cfg, const DriveTable& drives);

// run/runplan.json 的内容
std::string toJson(const PlanResult& plan);

// 某阶段的 .cmd 批处理内容
std::string phaseScript(const PlanResult& plan, const PhasePlan& phase);

// 把计划落盘：<dir>/runplan.json + 五个阶段脚本。目录不存在会创建。
// CLI 的 --gen-runplan 与界面「运行部署」共用这一份逻辑 —— 两条路径各写一遍
// 迟早会分叉（之前就是），所以收进 core。
bool writePlanDirectory(const PlanResult& plan, const std::string& utf8Dir, std::string& error);

} // namespace osp::runplan

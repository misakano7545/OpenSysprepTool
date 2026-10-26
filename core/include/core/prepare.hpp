#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/config.hpp"

// 封装前设置应用（M9）
//
// 解决的问题：配置界面里有一批字段只被 JSON 存下来，从来没有人执行。
// 本模块把它们翻译成**显式动作**，先 --dry-run 预览，再 --execute 落地，
// 并把可回滚的动作写进 journal，出问题时能 --rollback 还原。
//
// 边界：
//   - 只做当前系统（封装母盘）上的机械动作：注册表、命令、目录、文件移动。
//   - 需要离线加载 Default User hive 的设置（我的文档、IE 主页）不在本模块做，
//     在 deferred 里写明原因；不假装做了。
//   - 需要部署运行时判定的设置（禁止虚拟机部署、文件防篡改）也不在这里做。
//   - 删除临时文件 / sfc /purgecache 是不可回滚的，动作上标 irreversible。

namespace osp::prepare {

enum class ActionKind
{
    RegistrySet,
    RegistryDelete,
    RunCommand,
    MakeDirectory,
    CopyTree,
    MoveTree,
    DeleteTree,
    // BCD 的启动菜单超时：执行时先读原值再写，原值进回滚日志
    BcdTimeout,
    // 删整个注册表键（值集合无法逐条还原，所以不进 journal、标为不可回滚）
    RegistryDeleteKey,
};

const char* actionKindId(ActionKind kind);

struct Action
{
    std::string id;          // 稳定标识，如 optimization.closeNumLock
    std::string group;       // 系统优化 / 首次进桌面 / 部署设置 / 封装清理
    std::string title;       // 人类可读
    std::string sourceField; // 对应 config 字段路径
    ActionKind kind = ActionKind::RegistrySet;

    // RegistrySet / RegistryDelete
    std::string hive;      // HKLM / HKCU / HKU\.DEFAULT
    std::string key;       // 不含根键
    std::string valueName; // 空 = 默认值
    std::string valueType; // sz / expandSz / dword / qword / multiSz / binary
    std::string text;      // sz / expandSz
    std::uint32_t dword = 0;
    std::uint64_t qword = 0;
    std::vector<std::string> multi;
    std::string bytesHex; // binary，十六进制

    // RunCommand
    std::string program;
    std::string arguments;

    // CreateDirectory / CopyTree / MoveTree / DeleteTree
    std::string path;
    std::string destination;

    bool requiresReboot = false;
    bool irreversible = false;
    // 命令类动作的撤销命令（如 bcdedit /set ... 写回原值）。
    // 非空时执行后会写进 journal，--rollback 逆序跑它。
    std::string undoCommand;
    std::string note;
};

// 配置里有、但本里程碑不执行的项，逐条给出原因。
struct Deferred
{
    std::string sourceField;
    std::string reason;
};

struct Options
{
    std::string systemDrive;        // 默认 %SystemDrive%
    std::string userProfile;        // 默认 %USERPROFILE%
    std::string publicProfile;      // 默认 %PUBLIC%
    std::string windowsDir;         // 默认 %SystemRoot%
    std::string tempDir;            // 默认 %TEMP%
    std::string defaultUserProfile; // 默认 <systemDrive>\Users\Default
};

// 从环境变量取默认值；空字段会被填上。
Options defaultOptions();

struct Plan
{
    bool ok = false;
    std::string error;
    std::vector<Action> actions;
    std::vector<Deferred> deferred;
    std::vector<std::string> warnings;
    std::vector<std::string> blockers; // 非空 = 拒绝 --execute
};

// 纯计算：读配置、拼动作，不碰注册表、不碰文件系统（除了读环境变量）。
Plan build(const config::Config& cfg, const Options& options);

std::string dryRunReport(const Plan& plan);
std::string toJson(const Plan& plan);

// ---------------------------------------------------------------- 回滚日志

struct JournalEntry
{
    std::string actionId;
    std::string kind; // registry / move / copy / createDir
    std::string hive;
    std::string key;
    std::string valueName;
    bool existed = false;
    std::string valueType;
    std::string text;
    std::uint32_t dword = 0;
    std::uint64_t qword = 0;
    std::vector<std::string> multi;
    std::string bytesHex;
    std::string from;
    std::string to;
    std::string path;
    std::string undoCommand;
};

struct Journal
{
    int schemaVersion = 1;
    std::string createdAt;
    std::vector<JournalEntry> entries;
};

std::string journalToJson(const Journal& journal);
bool journalFromJson(const std::string& text, Journal& journal, std::string& error);

struct ExecResult
{
    bool ok = false;
    std::string error;
    std::vector<std::string> log;
    Journal journal;
    std::vector<std::string> warnings;
    std::size_t applied = 0;
    std::size_t failed = 0;
};

// 按顺序执行。遇到失败即停止（已执行的动作留在 journal 里，可回滚）。
ExecResult execute(const Plan& plan);

// 只执行给定的动作，供单元测试与自检使用。
ExecResult executeActions(const std::vector<Action>& actions);

// 逆序回滚。dryRun=true 时只列将要做的还原动作。
ExecResult rollback(const Journal& journal, bool dryRun);

// 只碰 HKCU\Software\OpenSysprepTool\SelfTest 的自检；不需要管理员。
bool selfTest(std::string& report, std::string& error);

} // namespace osp::prepare

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
    // 写进「离线加载的用户 hive」（Users\Default\NTUSER.DAT）：
    // 用于让新用户继承 我的文档/收藏夹 位置与 IE 主页。执行时临时 RegLoadKey，写完即卸。
    HiveSet,
    // 单个文件复制（path → destination）：任务栏固定的 .lnk 要落到
    // 「默认用户」的 User Pinned\TaskBar 下，CopyTree 会连目录层级一起搬，
    // 目标文件名也定不下来，所以单独一种。
    FileCopy,
    // 删单个文件（path）。文件不存在时算成功，不算失败。
    FileDelete,
    // 在默认用户配置目录里静默创建一个宽带（PPPoE）连接：
    // 由 Windows 的 RasSetEntryProperties 写进临时 pbk，再落到 destination。
    PppoeCreate,
};

const char* actionKindId(ActionKind kind);

struct Action
{
    std::string id;          // 稳定标识，如 optimization.closeNumLock
    std::string group;       // 系统优化 / 首次进桌面 / 部署设置 / 封装清理
    std::string title;       // 人类可读
    std::string sourceField; // 对应 config 字段路径
    ActionKind kind = ActionKind::RegistrySet;

    // HiveSet：要离线挂载的 hive 文件（如 <系统盘>\Users\Default\NTUSER.DAT）
    // 与挂载后使用的临时名（HKU\<hiveName>）
    std::string hiveFile;
    std::string hiveName = "OSPT_DEFAULT";

    // RegistrySet / RegistryDelete / HiveSet
    std::string hive;      // HKLM / HKCU / HKU\.DEFAULT
    std::string key;       // 不含根键
    std::string valueName; // 空 = 默认值
    std::string valueType; // sz / expandSz / dword / qword / multiSz / binary
    std::string text;      // sz / expandSz
    std::uint32_t dword = 0;
    std::uint64_t qword = 0;
    std::vector<std::string> multi;
    std::string bytesHex; // binary，十六进制

    // PppoeCreate
    std::string connName;
    std::string connUser;
    std::string connPassword;

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
    // 清理类命令允许非 0 退出码：net stop 一个本来没跑的服务会返回 3521，
    // 这不是失败。置 true 时只记日志与告警，不中止。
    bool tolerateNonZeroExit = false;
    // 写入被拒时降级为告警而不中止。目前只用于 IE 主页：
    // Win10/11 上 Software\Microsoft\Internet Explorer\Main 带拒绝写入的 ACL
    // （实测 RegSetValueEx 返回 ERROR_ACCESS_DENIED，reg.exe 甚至返回 0 却没写进去），
    // 这是系统行为，不该让整份封装计划失败。
    bool tolerateFailure = false;
    std::string note;
};

// 配置里有、但本里程碑不执行的项，逐条给出原因。
struct Deferred
{
    std::string sourceField;
    std::string reason;
};

struct PagefileCandidate
{
    std::string drive; // "D:"
    std::uint64_t freeBytes = 0;
};

struct Options
{
    std::string systemDrive;        // 默认 %SystemDrive%
    std::string userProfile;        // 默认 %USERPROFILE%
    std::string publicProfile;      // 默认 %PUBLIC%
    std::string windowsDir;         // 默认 %SystemRoot%
    std::string tempDir;            // 默认 %TEMP%
    std::string defaultUserProfile; // 默认 <systemDrive>\Users\Default
    // 智能转移虚拟内存时的候选分区。空 = 现场枚举（只取固定磁盘）。
    // 做成可注入是为了让「选剩余最大分区」这条规则能被单测钉住，
    // 否则断言就依赖跑测试那台机器插了几个盘、各剩多少。
    std::vector<PagefileCandidate> pagefileCandidates;

    // 自动设置 IP 用的网卡名（SC 是设置在目标机当前网卡上）。
    // 空 = 现场探测「有默认网关且已连接」的那个适配器。
    std::string ipInterface;
    // 随机/网段模式的选择种子。0 = 真随机；非 0 = 同种子同结果，单测据此钉住行为。
    std::uint64_t ipRandomSeed = 0;
    // 物理内存 MB，供「虚拟内存按物理内存倍数」计算。0 = 现场取 GlobalMemoryStatusEx。
    // 做成可注入同样是为了让倍数换算能被单测钉住，不依赖跑测试那台机器的内存大小。
    int physicalMemoryMb = 0;
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

// 虚拟内存大小字段（PagingFiles 的 `<initial> <maximum>` 两段）：
//   mode=system     -> "0 0"，由系统管理（SC 不选两个单选时的默认）
//   mode=multiplier -> 物理内存 MB × 倍数
//   mode=fixed      -> 配置里的固定 MB
// 返回空字符串表示「配置不完整，调用方应报错而不是猜一个值」。
std::string pageFileSizeFields(const config::Optimization& opt, int physicalMemoryMb);

// 纯计算：读配置、拼动作，不碰注册表、不碰文件系统（除了读环境变量）。
Plan build(const config::Config& cfg, const Options& options);

std::string dryRunReport(const Plan& plan);
std::string toJson(const Plan& plan);

// ---------------------------------------------------------------- 回滚日志

struct JournalEntry
{
    std::string actionId;
    std::string kind; // registry / hiveRegistry / move / copy / createDir
    std::string hive;
    // kind == "hiveRegistry" 时用它：回滚要重新离线挂载这个文件再写回
    std::string hiveFile;
    std::string hiveName;
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

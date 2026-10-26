#pragma once

#include <cstdint>
#include <string>
#include <vector>

// M2 系统体检：只依赖 Win32 API 与注册表（杀软状态经 WMI COM 的 root\SecurityCenter2）。
// 所有采集函数都不抛异常；失败项写进 warnings 并保持字段为默认值。

namespace osp::sysinfo {

// ---------------------------------------------------------------- 系统版本
struct OsInfo
{
    std::string productName;    // 注册表 ProductName，注意 Win11 上仍可能显示 "Windows 10"
    std::string editionId;      // 如 Professional
    std::string displayVersion; // 如 24H2
    std::string buildLab;       // BuildLabEx
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t build = 0;
    std::uint32_t revision = 0; // UBR
    std::string version;        // "10.0.26100.6899"
    std::string architecture;   // x64 / x86 / arm64 / unknown
    bool is64BitOs = false;
    bool isServer = false;
};

// ---------------------------------------------------------------- sysprep
struct SysprepInfo
{
    bool available = false;                // sysprep.exe 存在且可读
    std::string exePath;                   // 绝对路径
    std::string imageState;                // HKLM\...\Setup\State\ImageState，如 IMAGE_STATE_COMPLETE
    bool sysprepPending = false;           // 已封装且处于待 OOBE/待部署状态（不可逆风险）
    bool oobeInProgress = false;           // HKLM\SYSTEM\Setup\OOBEInProgress
    std::uint32_t cleanupState = 0;        // HKLM\SYSTEM\Setup\Status\SysprepStatus（可能不存在）
    std::uint32_t generalizationState = 0; // 注意：7 是装完系统的默认值，不能单独作为判据
    std::string note;
};

// ---------------------------------------------------------------- 分区
struct PartitionInfo
{
    std::string drive;       // "C:"
    std::string volumeLabel; // 卷标，可能为空
    std::string fileSystem;  // NTFS / FAT32 ...
    std::string driveType;   // fixed / removable / cdrom / network / ramdisk / unknown
    std::uint64_t totalBytes = 0;
    std::uint64_t freeBytes = 0;
    std::uint64_t usedBytes = 0;
    double usedPercent = 0.0;
    bool systemDrive = false;
};

// ---------------------------------------------------------------- 必备文件
struct RequiredFile
{
    std::string path;
    bool exists = false;
    std::uint64_t sizeBytes = 0;
};

// ---------------------------------------------------------------- 克隆版检测
struct CloneIndicator
{
    std::string name;
    bool detected = false;
    std::string detail;
};

// ---------------------------------------------------------------- 杀软
struct AntivirusProduct
{
    std::string displayName;
    std::uint32_t productState = 0;
    bool enabled = false;
    bool upToDate = false;
};

struct AntivirusInfo
{
    bool queried = false; // WMI 查询是否成功执行
    std::string error;    // queried=false 时的原因
    std::vector<AntivirusProduct> products;
    bool anyEnabled = false;
};

// ---------------------------------------------------------------- 启动项
struct StartupItem
{
    std::string scope; // 注册表键路径或 "StartupFolder:<dir>"
    std::string name;
    std::string command;
};

// ---------------------------------------------------------------- 体检项

// 一项体检结论。severity 决定它会不会阻断封装：
//   critical —— 阻断（sysprep 会失败或让数据不可恢复）
//   warning  —— 提示（会随镜像带走，但能继续）
//   info     —— 只是记录（例如待重命名文件，实测在大量健康机器上存在）
struct Check
{
    std::string id;
    std::string severity; // critical / warning / info
    std::string title;
    std::string message;    // 检测到问题时的一句话
    std::string resolution; // 怎么处理
    std::string evidence;   // 命中的路径 / 注册表值 / 原始输出
    bool detected = false;  // 是否命中问题
    bool evaluated = true;  // false = 本次没查（例如需要管理员）
};

// ---------------------------------------------------------------- 汇总
struct CheckupReport
{
    std::string generatedAtUtc; // "2026-10-24T13:20:00Z"
    std::string hostName;
    OsInfo os;
    SysprepInfo sysprep;
    std::vector<PartitionInfo> partitions;
    std::vector<RequiredFile> requiredFiles;
    std::vector<CloneIndicator> cloneIndicators;
    bool isLikelyClone = false;
    AntivirusInfo antivirus;
    std::vector<StartupItem> startupItems;
    std::vector<Check> checks;
    std::vector<std::string> warnings;
};

// 采集全量体检报告。永不抛异常；单项失败写入 warnings。
CheckupReport collect();

// severity == critical 且 detected 的项。--sysprep 直接拿它当 blockers，
// 不再各写一套判断。
std::vector<const Check*> blockingChecks(const CheckupReport& report);

// 序列化为 JSON 文本（2 空格缩进）。
std::string toJson(const CheckupReport& report);

// 人类可读摘要，供 --checkup 不带 --json 时打印。
std::string toSummaryText(const CheckupReport& report);

} // namespace osp::sysinfo

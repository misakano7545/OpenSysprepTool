#pragma once

// 模式 B —— SRS 注册表绑定注入（宪章 §8 M6）
//
// 输入形态：裸 `.sys` + `Drv.reg` / `Drv.pesrs`，**没有 .inf 也没有 .cat**。
// 这是 SC 驱动集的实际形态，DISM 完全用不上，只能走离线 hive 注入。
//
// 五步（宪章 §8 M6）：
//   1. RegLoadKey        挂载 <目标>\Windows\System32\config\SYSTEM 到临时键
//   2. 复制              <name>.sys → <目标>\Windows\System32\drivers\
//   3. RegSetValueEx     ControlSet00x\Services\<name>  Type / Start / ImagePath
//   4. RegSetValueEx     ControlSet00x\Control\CriticalDeviceDatabase\<硬件ID>
//                        ClassGUID / Service
//   5. RegUnLoadKey      卸载
//
// `--dry-run` 只读文件、只算路径，**绝不调用 RegLoadKey、绝不动任何 hive**。

#include <string>
#include <vector>

namespace osp::srsinject {

// ---------------------------------------------------------------- .reg 解析

enum class RegType
{
    String,       // "值"           REG_SZ
    ExpandString, // hex(2):        REG_EXPAND_SZ
    Dword,        // dword:         REG_DWORD
    Qword,        // hex(b):        REG_QWORD
    Binary,       // hex:           REG_BINARY
    MultiString,  // hex(7):        REG_MULTI_SZ
    Deleted       // =-
};

const char* regTypeId(RegType type);

struct RegValue
{
    std::string name; // 空字符串代表默认值（@=）
    RegType type = RegType::String;
    std::string rawData; // 原样保留，用于核对
    std::string display; // 人类可读（hex(2) 会解码成字符串）
    // 原始字节。dword/hex/hex(2)/hex(7)/hex(b) 都有确定字节；
    // 字符串型（REG_SZ）在写入时按 UTF-16LE 编码。
    std::vector<std::uint8_t> bytes;
    std::string text; // 字符串型的字面值
};

struct RegKey
{
    std::string path; // 原始路径，含 hive 前缀
    bool deleted = false;
    std::vector<RegValue> values;
};

struct RegFile
{
    bool ok = false;
    std::string error;
    std::string path;
    std::string encoding; // utf-16le / utf-16be / utf-8
    std::vector<RegKey> keys;
};

// 解析 .reg / .drv.pesrs。自动识别 BOM 与编码。
RegFile parseRegFile(const std::string& utf8Path);

// 把原始键路径挂到临时挂载键下面：
//   HKEY_LOCAL_MACHINE\SYSTEM\<rest>  -> <mount>\<rest>
//   HKEY_USERS\<任意>\<rest>          -> <mount>\<rest>
// 返回值 second 为 false 表示这条键不属于 SYSTEM hive（例如 HKCU），无法离线注入。
struct MountedKey
{
    std::string relative;    // ControlSet001\Services\pciide
    std::string apiPath;     // OSPT_SRS\ControlSet001\Services\pciide（交给 RegSetValueEx）
    std::string displayPath; // HKLM\OSPT_SRS\ControlSet001\Services\pciide（打印用）
    bool injectable = false;
};

MountedKey mountKeyOf(const std::string& rawPath, const std::string& mountKey);

// ---------------------------------------------------------------- 包与计划

struct SrsPackage
{
    std::string dir;
    std::string regFile;
    bool usedPesrs = false;
    std::string controlSet;                // 001
    std::vector<std::string> sysFiles;     // 绝对路径
    std::vector<std::string> serviceNames; // 本包声明的服务名
    std::size_t serviceCount = 0;
    std::size_t cddCount = 0;
    std::vector<std::string> foreignKeys; // 不属于 SYSTEM hive、被跳过的键
};

struct CopyOp
{
    std::string from;
    std::string to;
};

struct RegistryOp
{
    std::string apiPath;     // 交给注册表 API 的路径
    std::string displayPath; // 打印用的可读路径
    std::string kind;        // services / cdd
    std::string subject;     // 服务名或硬件 ID
    std::vector<RegValue> values;
    bool inferred = false; // 值由工具推断（包内没声明）
};

struct Plan
{
    bool ok = false;
    std::string error;
    std::string image;
    std::string mountKey;
    std::string hivePath; // <image>\Windows\System32\config\SYSTEM
    std::string driversDir;
    bool imageLooksLikeWindows = false;
    bool hiveExists = false;

    std::vector<SrsPackage> packages;
    std::vector<CopyOp> copyOps;
    std::vector<RegistryOp> serviceOps;
    std::vector<RegistryOp> cddOps;
    std::vector<RegistryOp> otherOps;
    std::vector<std::string> warnings;

    std::size_t sysFileCount = 0;
    std::size_t registryOpCount = 0;
};

// 默认挂载键名。SC 自己用的是 DCSRS；这里用自己的名字，避免与它撞。
inline constexpr const char* kDefaultMountKey = "OSPT_SRS";

// 只做静态计算。dryRun 参数只影响告警措辞，不影响是否真的去挂载 —— 本函数从不挂载。
Plan plan(const std::string& image, const std::string& srsDir, const std::string& mountKey);

std::string dryRunReport(const Plan& plan);
std::string toJson(const Plan& plan);

// 执行用的五步文案，dry-run 与真实执行共用，保证「预告 == 实际」
std::vector<std::string> executionSteps(const Plan& plan);

// 真实执行五步。需要管理员权限（RegLoadKey 要 SeBackup/SeRestore）与已挂载的映像。
// log 里按顺序记录每一步的实际动作，便于与 dry-run 的预告逐条对照。
// 注意：这条路径在本项目的开发环境里**未经验证**（缺管理员权限与真实映像），
// 见 tests/integration/verify_drivers.py 里的说明。
bool execute(const Plan& plan, std::vector<std::string>& log, std::string& error);

} // namespace osp::srsinject

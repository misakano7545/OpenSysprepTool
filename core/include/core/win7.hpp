#pragma once

// Windows 7 SP1 兼容层（宪章 §8 M5.1）
//
// 三道闸：
//   1. 编译期 —— WINVER / _WIN32_WINNT / NTDDI_VERSION 定在 Win7 SP1，
//      SDK 头会直接拒绝 Win8+ 的 API，这是最强的一道
//   2. 导入表 —— 不得静态导入 Win8+ API；确需时只能 GetProcAddress 动态取，
//      留 Win7 回退路径，并在 dynamicApiWhitelist() 里登记
//   3. 运行库 —— 静态 /MT，产物不得依赖 VCRUNTIME140 / MSVCP140 / ucrtbase /
//      api-ms-win-*（微软已明确 v14 Redistributable 只支持 Win10/11）
//
// 第 1 条在下面用 static_assert 钉死；第 2、3 条由 tests/integration/
// verify_win7_compat.py 读 PE 核对，并与本模块输出的自述值交叉比对。

#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------- 第一道闸

// 这三个宏没定义时，SDK 头按「最新系统」给声明，Win8+ 的 API 会静默编过，
// 那道闸就形同不存在。宁可编译失败。
#if !defined(WINVER) || !defined(_WIN32_WINNT) || !defined(NTDDI_VERSION)
#error "必须定义 WINVER / _WIN32_WINNT / NTDDI_VERSION（Windows 7 SP1 是第一道闸）"
#endif

namespace osp::win7 {

inline constexpr std::uint32_t kWinVer = WINVER;
inline constexpr std::uint32_t kWin32WinNt = _WIN32_WINNT;
inline constexpr std::uint32_t kNtddiVersion = NTDDI_VERSION;

inline constexpr std::uint32_t kWindows7 = 0x0601;
inline constexpr std::uint32_t kWindows7Sp1Ntddi = 0x06010000;

static_assert(kWinVer == kWindows7, "WINVER 必须是 0x0601（Windows 7 SP1）");
static_assert(kWin32WinNt == kWindows7, "_WIN32_WINNT 必须是 0x0601（Windows 7 SP1）");
static_assert(kNtddiVersion == kWindows7Sp1Ntddi, "NTDDI_VERSION 必须是 0x06010000");

// PE 的 MajorSubsystemVersion.MinorSubsystemVersion。
// 由顶层 CMakeLists 的 /SUBSYSTEM:CONSOLE,6.01 定死，闸门脚本读 PE 核对。
// 放在这里是为了让「期望值」和「实测值」能被同一个脚本比对。
inline constexpr int kExpectedSubsystemMajor = 6;
inline constexpr int kExpectedSubsystemMinor = 1;

// 运行库禁止出现的动态依赖（闸门脚本同样按这份名单断言）
// 注意：不要加 inline —— 声明成 inline 会让它在定义它的 TU 里变成
// 「未使用的 inline 函数」，编译器不生成实体符号，其它 TU 链接时报 LNK2019。
const std::vector<const char*>& forbiddenRuntimeDlls();

// ---------------------------------------------------------------- 运行期系统

struct OsVersion
{
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t build = 0;
    std::uint32_t platformId = 0;
    std::uint32_t servicePackMajor = 0;
    std::uint32_t servicePackMinor = 0;
    std::string productName;    // 取自注册表，读不到时为空
    bool queriedFromNt = false; // RtlGetVersion 是否可用
};

// 用 RtlGetVersion（ntdll，XP 起就有）读真实版本。
// 不用 GetVersionExW：/W4 下被标为弃用，且不带 manifest 时会在 Win8.1+ 撒谎。
OsVersion osVersion();

// 是否满足最低目标系统（Windows 7 SP1）。低于此版本不是支持目标。
bool isSupportedTarget(const OsVersion& version);

// 供日志与 --win7-compat 的人类可读描述
std::string describe(const OsVersion& version);

// ---------------------------------------------------------------- 第二道闸

// 动态解析的 Win8+ API 登记项。
// 空列表也是合法状态 —— 那说明本工具压根没用到 Win8+ 的 API。
struct DynamicApi
{
    const char* module;   // "kernel32.dll"
    const char* proc;     // "GetSystemTimePreciseAsFileTime"
    const char* since;    // 该 API 从哪个系统开始提供
    const char* purpose;  // 本工具为什么需要它
    const char* fallback; // Win7 上的回退路径
};

const std::vector<DynamicApi>& dynamicApiWhitelist();

// 该 API 是否已登记（模块名与函数名均不区分大小写）
bool isWhitelisted(const char* module, const char* proc);

// GetProcAddress + 缓存。只在进程已加载的模块里找（kernel32 / ntdll 恒在），
// 不 LoadLibrary —— 免得引入 KB2533623 相关的搜索路径问题。
// 找不到返回 nullptr；绝不回退成静态导入。
void* resolve(const char* module, const char* proc);

// 本次进程里解析成功的 API 名（"module!proc"），供 --win7-compat 输出
std::vector<std::string> resolvedProcs();

// ---------------------------------------------------------------- 时间

// GetSystemTimePreciseAsFileTime（Win8+）优先，缺失时回退 GetSystemTimeAsFileTime。
// 返回 FILETIME 的 100ns 计数。
std::uint64_t fileTimeNow();

// 单调时钟。GetTickCount64 是 Vista+，Win7 可用。
std::uint64_t tickCountMs();

// 把 fileTimeNow() 的 100ns 计数格式化成带毫秒的本地时间字符串
std::string formatFileTime(std::uint64_t fileTime);

// ---------------------------------------------------------------- WOW64 文件系统重定向
//
// 32 位进程在 64 位 Windows 上访问 %SystemRoot%\System32 会被静默重定向到
// SysWOW64 —— 而 sysprep.exe / dism.exe / bcdedit.exe 都是 64 位专有工具，
// 在 SysWOW64 里根本不存在。不关掉重定向就探测，报出来的「文件不存在」是假的。
//
// Wow64DisableWow64FsRedirection / Wow64RevertWow64FsRedirection 只存在于
// **64 位 Windows** 的 kernel32 里。32 位系统上静态导入会让进程直接加载失败，
// 所以这两个只能动态取；取不到正说明系统本来就没有重定向，什么都不用做。
class ScopedWow64FsRedirection
{
public:
    ScopedWow64FsRedirection();
    ~ScopedWow64FsRedirection();

    ScopedWow64FsRedirection(const ScopedWow64FsRedirection&) = delete;
    ScopedWow64FsRedirection& operator=(const ScopedWow64FsRedirection&) = delete;

    // 重定向是否真的被关掉了（32 位系统上恒为 false）
    bool disabled() const
    {
        return disabled_;
    }

    // 这两个 API 在本机是否可用（即：是不是 64 位 Windows）
    static bool available();

    // 当前进程是否跑在 WOW64 下（32 位进程 + 64 位系统）
    static bool processIsWow64();

private:
    void* savedValue_ = nullptr;
    bool disabled_ = false;
};

struct NativeFileInfo
{
    bool exists = false;
    std::uint64_t sizeBytes = 0;
};

// 在关掉 WOW64 重定向的前提下探测文件。调用方不必自己关心重定向。
NativeFileInfo probeFileNative(const std::wstring& path);

} // namespace osp::win7

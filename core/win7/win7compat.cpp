#include "core/win7.hpp"

#include <windows.h>
#include <shellapi.h>

#include <winreg.h>

#include <cstdio>
#include <mutex>
#include <unordered_map>

namespace osp::win7 {
namespace {

// RTL_OSVERSIONINFOW 的布局。自带一份，免得为一处调用去拉 winternl.h。
struct OsVersionInfoW
{
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    WCHAR szCSDVersion[128];
};

// RtlGetVersion 是 ntdll 里的原生 API，XP 起就有，但不是导入库函数 —— 只能动态取。
// 它不在 Win8+ 名单里（Win7 也能用），所以不进 dynamicApiWhitelist()。
using RtlGetVersionFn = LONG(WINAPI*)(OsVersionInfoW*);
// Win8+ 独有：动态取，取不到走 GetSystemTimeAsFileTime。
using GetSystemTimePreciseAsFileTimeFn = VOID(WINAPI*)(LPFILETIME);

struct ProcCache
{
    std::mutex mutex;
    std::unordered_map<std::string, void*> procs; // "module!proc"（小写）-> 地址
    std::vector<std::string> resolved;            // 解析成功的原始大小写形式
};

ProcCache& procCache()
{
    static ProcCache cache;
    return cache;
}

std::string toLower(const std::string& text)
{
    std::string out = text;
    for (char& ch : out)
    {
        if (ch >= 'A' && ch <= 'Z')
        {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}

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

std::string fromWide(const std::wstring& text)
{
    if (text.empty())
    {
        return std::string();
    }
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr
    );
    if (size <= 0)
    {
        return std::string();
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr
    );
    return out;
}

// 读 HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion 的 ProductName。
// RegGetValueW 是 Vista+，Win7 可用。读不到就留空，不影响判定。
std::string readProductName()
{
    const wchar_t* path = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    wchar_t buffer[256] = {};
    DWORD size = sizeof(buffer) - sizeof(wchar_t);
    const LSTATUS status =
        RegGetValueW(HKEY_LOCAL_MACHINE, path, L"ProductName", RRF_RT_REG_SZ, nullptr, buffer, &size);
    if (status != ERROR_SUCCESS)
    {
        return std::string();
    }
    return fromWide(std::wstring(buffer));
}

} // namespace

const std::vector<const char*>& forbiddenRuntimeDlls()
{
    static const std::vector<const char*> kDlls{
        "vcruntime140.dll",
        "vcruntime140_1.dll",
        "msvcp140.dll",
        "ucrtbase.dll",
    };
    return kDlls;
}

OsVersion osVersion()
{
    OsVersion version;

    static const auto rtlGetVersion =
        reinterpret_cast<RtlGetVersionFn>(resolve("ntdll.dll", "RtlGetVersion"));

    if (rtlGetVersion != nullptr)
    {
        OsVersionInfoW info = {};
        info.dwOSVersionInfoSize = sizeof(info);
        if (rtlGetVersion(&info) == 0) // STATUS_SUCCESS
        {
            version.major = info.dwMajorVersion;
            version.minor = info.dwMinorVersion;
            version.build = info.dwBuildNumber;
            version.platformId = info.dwPlatformId;
            version.queriedFromNt = true;

            const std::wstring servicePack(info.szCSDVersion);
            version.servicePackMajor =
                servicePack.empty() ? 0 : static_cast<std::uint32_t>(servicePack.back() - L'0');
        }
    }

    version.productName = readProductName();
    return version;
}

bool isSupportedTarget(const OsVersion& version)
{
    if (version.major != 6)
    {
        return version.major > 6; // Win10/11 也在支持范围内
    }
    if (version.minor > 1)
    {
        return true; // Win8 / 8.1
    }
    if (version.minor < 1)
    {
        return false; // Vista 及更早
    }
    return version.servicePackMajor >= 1; // Win7 必须是 SP1
}

std::string describe(const OsVersion& version)
{
    char buffer[256] = {};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%u.%u build %u (SP %u)%s",
        version.major,
        version.minor,
        version.build,
        version.servicePackMajor,
        version.queriedFromNt ? "" : " [RtlGetVersion 不可用]"
    );

    std::string text(buffer);
    if (!version.productName.empty())
    {
        text += " " + version.productName;
    }
    return text;
}

const std::vector<DynamicApi>& dynamicApiWhitelist()
{
    // 只有确实会出现在产物里、且系统要求高于 Windows 7 的 API 才登记在这里。
    // verify_win7_compat.py 会双向断言：
    //   - 产物里出现的每个 Win8+ 名字都必须能在这份名单里找到出处
    //   - 名单里的每一条又必须真的在产物里出现（防止名单烂掉）
    static const std::vector<DynamicApi> kApis{
        {"kernel32.dll",
         "GetSystemTimePreciseAsFileTime",
         "Windows 8",
         "本工具主动解析：部署日志与进度页需要亚微秒级时间戳，用于阶段耗时统计",
         "GetSystemTimeAsFileTime()（Vista+，精度约 15.6ms，足以支撑进度显示）"},
        // 下面这条不是本工具的代码，是 VS2022 STL 内部的行为：
        // std::filesystem::temp_directory_path() 会先试 GetTempPath2W，
        // 取不到就回退 GetTempPathW。两种情况下功能都正常，所以只需登记。
        {"kernel32.dll",
         "GetTempPath2W",
         "Windows 10 2004",
         "MSVC STL 内部：std::filesystem::temp_directory_path() 优先使用更安全的版本",
         "STL 自动回退 GetTempPathW()（XP+），本工具无需干预"},
    };
    return kApis;
}

bool isWhitelisted(const char* module, const char* proc)
{
    if (module == nullptr || proc == nullptr)
    {
        return false;
    }
    const std::string wantedModule = toLower(module);
    const std::string wantedProc = toLower(proc);
    for (const DynamicApi& api : dynamicApiWhitelist())
    {
        if (wantedModule == toLower(api.module) && wantedProc == toLower(api.proc))
        {
            return true;
        }
    }
    return false;
}

void* resolve(const char* module, const char* proc)
{
    if (module == nullptr || proc == nullptr || *module == '\0' || *proc == '\0')
    {
        return nullptr;
    }

    ProcCache& cache = procCache();
    const std::string key = toLower(module) + "!" + toLower(proc);

    std::lock_guard<std::mutex> guard(cache.mutex);
    const auto found = cache.procs.find(key);
    if (found != cache.procs.end())
    {
        return found->second;
    }

    void* address = nullptr;
    // 只在已加载的模块里找。kernel32 与 ntdll 在进程起来时就已加载，
    // 所以这里不会 LoadLibrary —— 也就避开了 KB2533623 的搜索路径问题。
    const std::wstring wideModule = toWide(module);
    HMODULE handle = wideModule.empty() ? nullptr : GetModuleHandleW(wideModule.c_str());
    if (handle != nullptr)
    {
        address = reinterpret_cast<void*>(GetProcAddress(handle, proc));
    }

    cache.procs.emplace(key, address);
    if (address != nullptr)
    {
        cache.resolved.push_back(std::string(module) + "!" + proc);
    }
    return address;
}

std::vector<std::string> resolvedProcs()
{
    ProcCache& cache = procCache();
    std::lock_guard<std::mutex> guard(cache.mutex);
    return cache.resolved;
}

bool isElevated()
{
    // TokenElevation 查询 Vista+ 就有，静态导入不违反 Win7 闸门。
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) == 0)
    {
        return false;
    }

    TOKEN_ELEVATION elevation = {};
    DWORD returned = 0;
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &returned);
    CloseHandle(token);

    return ok != 0 && elevation.TokenIsElevated != 0;
}

std::uint64_t fileTimeNow()
{
    FILETIME now = {};

    static const auto precise = reinterpret_cast<GetSystemTimePreciseAsFileTimeFn>(
        resolve("kernel32.dll", "GetSystemTimePreciseAsFileTime")
    );

    if (precise != nullptr)
    {
        precise(&now);
    }
    else
    {
        // Win7 及更早的回退路径
        GetSystemTimeAsFileTime(&now);
    }

    return (static_cast<std::uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
}

std::uint64_t tickCountMs()
{
    return static_cast<std::uint64_t>(GetTickCount64());
}

std::string formatFileTime(std::uint64_t fileTime)
{
    FILETIME utc = {};
    utc.dwLowDateTime = static_cast<DWORD>(fileTime & 0xFFFFFFFFull);
    utc.dwHighDateTime = static_cast<DWORD>(fileTime >> 32);

    FILETIME local = {};
    SYSTEMTIME systemTime = {};
    if (FileTimeToLocalFileTime(&utc, &local) == 0 || FileTimeToSystemTime(&local, &systemTime) == 0)
    {
        return "0000-00-00 00:00:00.000";
    }

    // FILETIME 的 100ns 计数，取毫秒部分
    const std::uint32_t millis = static_cast<std::uint32_t>((fileTime / 10000ull) % 1000ull);

    char buffer[40] = {};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%04u-%02u-%02u %02u:%02u:%02u.%03u",
        systemTime.wYear,
        systemTime.wMonth,
        systemTime.wDay,
        systemTime.wHour,
        systemTime.wMinute,
        systemTime.wSecond,
        millis
    );
    return std::string(buffer);
}

// ---------------------------------------------------------------- WOW64 文件系统重定向

namespace {

// 只存在于 64 位 Windows 的 kernel32 里 —— 32 位系统上静态导入会让进程加载失败。
using Wow64DisableFsRedirectionFn = BOOL(WINAPI*)(PVOID*);
using Wow64RevertFsRedirectionFn = BOOL(WINAPI*)(PVOID);

Wow64DisableFsRedirectionFn wow64DisableFsRedirection()
{
    static const auto fn = reinterpret_cast<Wow64DisableFsRedirectionFn>(
        resolve("kernel32.dll", "Wow64DisableWow64FsRedirection")
    );
    return fn;
}

Wow64RevertFsRedirectionFn wow64RevertFsRedirection()
{
    static const auto fn =
        reinterpret_cast<Wow64RevertFsRedirectionFn>(resolve("kernel32.dll", "Wow64RevertWow64FsRedirection")
        );
    return fn;
}

} // namespace

bool ScopedWow64FsRedirection::available()
{
    return wow64DisableFsRedirection() != nullptr && wow64RevertFsRedirection() != nullptr;
}

bool ScopedWow64FsRedirection::processIsWow64()
{
    // IsWow64Process 自 XP SP2 起就在 kernel32 里（32 位系统上也有，返回 FALSE），
    // 所以这里可以静态调用。
    BOOL result = FALSE;
    if (IsWow64Process(GetCurrentProcess(), &result) == 0)
    {
        return false;
    }
    return result != FALSE;
}

ScopedWow64FsRedirection::ScopedWow64FsRedirection()
{
    const Wow64DisableFsRedirectionFn disable = wow64DisableFsRedirection();
    if (disable == nullptr || wow64RevertFsRedirection() == nullptr)
    {
        // 32 位 Windows：本来就没有重定向，什么都不用做
        return;
    }

    PVOID saved = nullptr;
    if (disable(&saved) != 0)
    {
        savedValue_ = saved;
        disabled_ = true;
    }
}

ScopedWow64FsRedirection::~ScopedWow64FsRedirection()
{
    if (!disabled_)
    {
        return;
    }
    const Wow64RevertFsRedirectionFn revert = wow64RevertFsRedirection();
    if (revert != nullptr)
    {
        revert(savedValue_);
    }
}

NativeFileInfo probeFileNative(const std::wstring& path)
{
    NativeFileInfo info;

    ScopedWow64FsRedirection guard; // 只对本作用域生效，离开即还原

    WIN32_FILE_ATTRIBUTE_DATA attributes = {};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes) == 0)
    {
        return info;
    }

    info.exists = true;
    info.sizeBytes = (static_cast<std::uint64_t>(attributes.nFileSizeHigh) << 32) |
        static_cast<std::uint64_t>(attributes.nFileSizeLow);
    return info;
}

std::vector<std::string> utf8CommandLineArguments()
{
    // Windows 的 main(argc, argv) 给的是 **ANSI** 编码：中文系统上是 GBK，英文系统上是 CP1252，
    // 都是随代码页漂移的。本工具要处理 D:\迅雷下载\... 这类路径，所以统一从
    // GetCommandLineW 取宽字符命令行，再转成 UTF-8 交给内部逻辑。
    //
    // 注意：CommandLineToArgvW 的第二个参数**不能传 nullptr** —— 传了会返回 NULL 并置
    // ERROR_INVALID_PARAMETER(87)，调用方拿到空数组还以为「命令行是空的」，
    // 于是悄悄退回 ANSI argv。必须在栈上收个数。
    std::vector<std::string> arguments;
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wide == nullptr || count <= 0)
    {
        if (wide != nullptr)
        {
            LocalFree(wide);
        }
        return arguments;
    }
    arguments.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        arguments.push_back(fromWide(std::wstring(wide[i])));
    }
    LocalFree(wide);
    return arguments;
}

std::filesystem::path pathFromUtf8(const std::string& utf8Text)
{
    // 必须走 u8string：path(std::string) 会按 ANSI 代码页解释，中文路径就废了。
    //
    // 但 path(u8string) 在 MSVC 上遇到**非法 UTF-8** 会抛 filesystem_error，
    // 而这类输入是真实存在的（GBK 编码的批处理、旧 INI、第三方工具拼出来的路径）。
    // 抛出去没人接就是 abort()。所以这里先严格校验 UTF-8，不合规就按 ANSI 代码页解释，
    // 再不行才退回原字节。宁可路径对不上让调用方报「打不开文件」，也不能整个进程倒掉。
    if (!utf8Text.empty())
    {
        const int wideLen = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, utf8Text.c_str(), static_cast<int>(utf8Text.size()), nullptr, 0
        );
        if (wideLen <= 0)
        {
            // 不是合法 UTF-8：按当前 ANSI 代码页解释（GBK 系统上正好是用户本意）
            const int ansiLen = MultiByteToWideChar(
                CP_ACP, 0, utf8Text.c_str(), static_cast<int>(utf8Text.size()), nullptr, 0
            );
            if (ansiLen > 0)
            {
                std::wstring wide(static_cast<std::size_t>(ansiLen), L'\0');
                MultiByteToWideChar(
                    CP_ACP, 0, utf8Text.c_str(), static_cast<int>(utf8Text.size()), wide.data(), ansiLen
                );
                return std::filesystem::path(wide);
            }
        }
    }

    const std::u8string u8(reinterpret_cast<const char8_t*>(utf8Text.data()), utf8Text.size());
    try
    {
        return std::filesystem::path(u8);
    }
    catch (const std::exception&)
    {
        // 最后兜底：按 ANSI 走一遍，失败就返回空路径，由调用方按「打开失败」处理
        const int ansiLen =
            MultiByteToWideChar(CP_ACP, 0, utf8Text.c_str(), static_cast<int>(utf8Text.size()), nullptr, 0);
        if (ansiLen > 0)
        {
            std::wstring wide(static_cast<std::size_t>(ansiLen), L'\0');
            MultiByteToWideChar(
                CP_ACP, 0, utf8Text.c_str(), static_cast<int>(utf8Text.size()), wide.data(), ansiLen
            );
            return std::filesystem::path(wide);
        }
        return std::filesystem::path();
    }
}

std::string toUtf8Path(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

} // namespace osp::win7

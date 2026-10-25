#include "core/win7.hpp"

#include <windows.h>

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

} // namespace osp::win7

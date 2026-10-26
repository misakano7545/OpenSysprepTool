#include "core/sysinfo.hpp"

#include <windows.h>
#include <winternl.h>
#include <comdef.h>
#include <lm.h>
#include <wbemidl.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cwchar>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/dism.hpp"
#include "core/win7.hpp"

namespace osp::sysinfo {
namespace {

// ---------------------------------------------------------------- 小工具

std::string toUtf8(const wchar_t* text)
{
    if (text == nullptr || *text == L'\0')
    {
        return {};
    }
    const int need = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (need <= 1)
    {
        return {};
    }
    std::string out(static_cast<std::size_t>(need - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), need, nullptr, nullptr);
    return out;
}

std::string toUtf8(const std::wstring& text)
{
    return toUtf8(text.c_str());
}

std::wstring winDir()
{
    wchar_t buffer[MAX_PATH * 2] = {};
    const UINT len = GetWindowsDirectoryW(buffer, static_cast<UINT>(std::size(buffer)));
    if (len == 0 || len >= std::size(buffer))
    {
        return L"C:\\Windows";
    }
    return std::wstring(buffer, len);
}

// ---------------------------------------------------------------- 注册表

bool readRegString(
    HKEY root, const wchar_t* subKey, const wchar_t* value, std::wstring& out, REGSAM extra = 0
)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ | extra, &key) != ERROR_SUCCESS)
    {
        return false;
    }
    wchar_t buffer[2048] = {};
    DWORD bytes = sizeof(buffer) - sizeof(wchar_t);
    DWORD type = 0;
    const LONG rc = RegQueryValueExW(key, value, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &bytes);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
    {
        return false;
    }
    buffer[bytes / sizeof(wchar_t)] = L'\0';
    out.assign(buffer);
    return true;
}

bool readRegDword(
    HKEY root, const wchar_t* subKey, const wchar_t* value, std::uint32_t& out, REGSAM extra = 0
)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ | extra, &key) != ERROR_SUCCESS)
    {
        return false;
    }
    DWORD data = 0;
    DWORD bytes = sizeof(data);
    DWORD type = 0;
    const LONG rc = RegQueryValueExW(key, value, nullptr, &type, reinterpret_cast<LPBYTE>(&data), &bytes);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || type != REG_DWORD)
    {
        return false;
    }
    out = data;
    return true;
}

std::wstring envExpand(const std::wstring& raw)
{
    wchar_t buffer[MAX_PATH * 2] = {};
    const DWORD len = ExpandEnvironmentStringsW(raw.c_str(), buffer, static_cast<DWORD>(std::size(buffer)));
    if (len == 0 || len > std::size(buffer))
    {
        return raw;
    }
    return std::wstring(buffer);
}

// ---------------------------------------------------------------- COM RAII

template <typename T> class ComPtr
{
public:
    ComPtr() = default;
    ~ComPtr()
    {
        reset();
    }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    T** put()
    {
        reset();
        return &m_ptr;
    }
    T* get() const
    {
        return m_ptr;
    }
    T* operator->() const
    {
        return m_ptr;
    }
    explicit operator bool() const
    {
        return m_ptr != nullptr;
    }

    void reset()
    {
        if (m_ptr != nullptr)
        {
            m_ptr->Release();
            m_ptr = nullptr;
        }
    }

private:
    T* m_ptr = nullptr;
};

class CoInitScope
{
public:
    CoInitScope()
    {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        m_shouldUninit = SUCCEEDED(hr);
        m_ok = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    }
    ~CoInitScope()
    {
        if (m_shouldUninit)
        {
            CoUninitialize();
        }
    }
    CoInitScope(const CoInitScope&) = delete;
    CoInitScope& operator=(const CoInitScope&) = delete;

    bool ok() const
    {
        return m_ok;
    }

private:
    bool m_ok = false;
    bool m_shouldUninit = false;
};

// ---------------------------------------------------------------- 系统版本

void fillOsInfo(OsInfo& os, std::vector<std::string>& warnings)
{
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOEXW);

    RTL_OSVERSIONINFOEXW vi = {};
    vi.dwOSVersionInfoSize = sizeof(vi);

    bool got = false;
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll != nullptr)
    {
        const auto fn =
            reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion"))
            );
        if (fn != nullptr && fn(&vi) == 0)
        {
            got = true;
        }
    }

    if (got)
    {
        os.major = vi.dwMajorVersion;
        os.minor = vi.dwMinorVersion;
        os.build = vi.dwBuildNumber;
        os.isServer = vi.wProductType != VER_NT_WORKSTATION;
    }
    else
    {
        // 不退回已弃用的 GetVersionExW（/W4 下会报 C4996，且未 manifest 时会撒谎）。
        warnings.push_back("RtlGetVersion 调用失败，版本回退到注册表 CurrentVersion");
        std::uint32_t major = 0;
        std::uint32_t minor = 0;
        std::wstring buildText;
        if (readRegDword(
                HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                L"CurrentMajorVersionNumber",
                major
            ))
        {
            os.major = major;
        }
        if (readRegDword(
                HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                L"CurrentMinorVersionNumber",
                minor
            ))
        {
            os.minor = minor;
        }
        if (readRegString(
                HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                L"CurrentBuild",
                buildText
            ))
        {
            os.build = static_cast<std::uint32_t>(_wtoi(buildText.c_str()));
        }
        // InstallationType 是 REG_SZ："Client" / "Server" / "Server Core"
        std::wstring installationType;
        if (readRegString(
                HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                L"InstallationType",
                installationType
            ))
        {
            os.isServer = installationType.rfind(L"Server", 0) == 0;
        }
    }

    std::uint32_t ubr = 0;
    if (readRegDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"UBR", ubr))
    {
        os.revision = ubr;
    }

    char version[64] = {};
    std::snprintf(version, sizeof(version), "%u.%u.%u.%u", os.major, os.minor, os.build, os.revision);
    os.version = version;

    std::wstring value;
    if (readRegString(
            HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName", value
        ))
    {
        os.productName = toUtf8(value);
    }
    if (readRegString(
            HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"EditionID", value
        ))
    {
        os.editionId = toUtf8(value);
    }
    if (readRegString(
            HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion", value
        ))
    {
        os.displayVersion = toUtf8(value);
    }
    if (readRegString(
            HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"BuildLabEx", value
        ))
    {
        os.buildLab = toUtf8(value);
    }

    SYSTEM_INFO si = {};
    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture)
    {
    case PROCESSOR_ARCHITECTURE_AMD64:
        os.architecture = "x64";
        os.is64BitOs = true;
        break;
    case PROCESSOR_ARCHITECTURE_INTEL:
        os.architecture = "x86";
        os.is64BitOs = false;
        break;
    case PROCESSOR_ARCHITECTURE_ARM64:
        os.architecture = "arm64";
        os.is64BitOs = true;
        break;
    default:
        os.architecture = "unknown";
        break;
    }
}

// ---------------------------------------------------------------- sysprep

void fillSysprepInfo(SysprepInfo& info, std::vector<std::string>& warnings)
{
    const std::wstring exe = winDir() + L"\\System32\\Sysprep\\sysprep.exe";
    info.exePath = toUtf8(exe);

    // 32 位进程在 64 位 Windows 上查 System32 会被静默重定向到 SysWOW64，
    // 而 sysprep.exe 是 64 位专有工具，在 SysWOW64 里不存在。
    // 不关掉重定向就探测，会把「其实有」报成「不存在」。
    const win7::NativeFileInfo probe = win7::probeFileNative(exe);
    if (probe.exists)
    {
        info.available = true;
    }
    else
    {
        warnings.push_back("sysprep.exe 不存在: " + info.exePath);
    }

    std::wstring imageState;
    if (readRegString(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Setup\\State",
            L"ImageState",
            imageState
        ))
    {
        info.imageState = toUtf8(imageState);
    }

    std::uint32_t oobe = 0;
    info.oobeInProgress =
        readRegDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\Setup", L"OOBEInProgress", oobe) && oobe != 0;

    std::uint32_t cleanup = 0;
    std::uint32_t generalization = 0;
    // CleanupState 仅作参考输出，不参与判定
    readRegDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\Setup\\Status\\SysprepStatus", L"CleanupState", cleanup);
    const bool hasGeneralization = readRegDword(
        HKEY_LOCAL_MACHINE, L"SYSTEM\\Setup\\Status\\SysprepStatus", L"GeneralizationState", generalization
    );
    info.cleanupState = cleanup;
    info.generalizationState = generalization;

    // GeneralizationState == 7 是所有正常装完的系统都有的默认值，不能作为"已封装"判据。
    // CleanupState 的取值语义在各版本间不一致（0/1/2/3 含义随 Windows 版本变化），
    // 同样不能单独判定，只作为参考值输出。
    // 真正无歧义的"已封装待部署"标志只有两个：
    //   1) ImageState 进入 *_RESEAL_TO_OOBE（sysprep 已执行，等待重启进 OOBE）
    //   2) OOBEInProgress == 1
    const bool resealToOobe = info.imageState.find("_RESEAL_TO_OOBE") != std::string::npos;

    info.sysprepPending = resealToOobe || info.oobeInProgress;

    if (info.sysprepPending)
    {
        info.note = "封装已执行，系统处于待 OOBE/待部署状态，再次 sysprep 可能失败";
    }
    else if (info.imageState == "IMAGE_STATE_COMPLETE")
    {
        info.note = "正常完成态（ImageState=IMAGE_STATE_COMPLETE），可执行封装";
    }
    else if (info.imageState.empty())
    {
        info.note = "未读到 ImageState，按正常态处理";
    }
    else
    {
        info.note = "ImageState=" + info.imageState + "，未见待部署标记";
    }

    if (!hasGeneralization)
    {
        // 记录但不影响判定：部分精简系统会缺这个键
        warnings.push_back("SysprepStatus\\GeneralizationState 不存在（不影响判定）");
    }
}

// ---------------------------------------------------------------- 分区

const char* driveTypeName(UINT type)
{
    switch (type)
    {
    case DRIVE_FIXED:
        return "fixed";
    case DRIVE_REMOVABLE:
        return "removable";
    case DRIVE_CDROM:
        return "cdrom";
    case DRIVE_REMOTE:
        return "network";
    case DRIVE_RAMDISK:
        return "ramdisk";
    default:
        return "unknown";
    }
}

void fillPartitions(std::vector<PartitionInfo>& out, std::vector<std::string>& warnings)
{
    const std::wstring windows = winDir();
    const wchar_t systemLetter = windows.size() >= 2 ? static_cast<wchar_t>(towupper(windows[0])) : L'C';

    wchar_t drives[512] = {};
    const DWORD len = GetLogicalDriveStringsW(static_cast<DWORD>(std::size(drives) - 1), drives);
    if (len == 0)
    {
        warnings.push_back("GetLogicalDriveStrings 失败，无法枚举分区");
        return;
    }

    for (const wchar_t* p = drives; *p != L'\0'; p += wcslen(p) + 1)
    {
        const UINT type = GetDriveTypeW(p);
        if (type == DRIVE_NO_ROOT_DIR)
        {
            continue;
        }

        PartitionInfo info;
        info.driveType = driveTypeName(type);

        wchar_t letter[3] = {p[0], L':', L'\0'};
        info.drive = toUtf8(letter);
        info.systemDrive = (towupper(p[0]) == systemLetter);

        ULARGE_INTEGER freeForCaller = {};
        ULARGE_INTEGER total = {};
        ULARGE_INTEGER freeTotal = {};
        if (GetDiskFreeSpaceExW(p, &freeForCaller, &total, &freeTotal) != 0)
        {
            info.totalBytes = total.QuadPart;
            info.freeBytes = freeTotal.QuadPart;
            info.usedBytes = info.totalBytes - info.freeBytes;
            if (info.totalBytes > 0)
            {
                info.usedPercent =
                    static_cast<double>(info.usedBytes) * 100.0 / static_cast<double>(info.totalBytes);
            }
        }
        else
        {
            warnings.push_back("无法读取容量: " + info.drive);
        }

        wchar_t label[MAX_PATH + 1] = {};
        wchar_t fsName[MAX_PATH + 1] = {};
        if (GetVolumeInformationW(
                p,
                label,
                static_cast<DWORD>(std::size(label)),
                nullptr,
                nullptr,
                nullptr,
                fsName,
                static_cast<DWORD>(std::size(fsName))
            ) != 0)
        {
            info.volumeLabel = toUtf8(label);
            info.fileSystem = toUtf8(fsName);
        }

        out.push_back(std::move(info));
    }
}

// ---------------------------------------------------------------- 必备文件

void fillRequiredFiles(std::vector<RequiredFile>& out, std::vector<std::string>& warnings)
{
    const std::wstring windows = winDir();
    const std::wstring candidates[] = {
        windows + L"\\System32\\Sysprep\\sysprep.exe",
        windows + L"\\System32\\dism.exe",
        windows + L"\\System32\\bcdedit.exe",
        windows + L"\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
    };

    for (const std::wstring& path : candidates)
    {
        RequiredFile file;
        file.path = toUtf8(path);

        // 同上：这些工具都在 64 位 System32 里，探测前先关掉 WOW64 重定向
        const win7::NativeFileInfo probe = win7::probeFileNative(path);
        if (probe.exists)
        {
            file.exists = true;
            file.sizeBytes = probe.sizeBytes;
        }
        else
        {
            warnings.push_back("必备文件缺失: " + file.path);
        }
        out.push_back(std::move(file));
    }
}

// ---------------------------------------------------------------- 克隆版检测

void fillCloneIndicators(
    std::vector<CloneIndicator>& out,
    bool& likelyClone,
    const SysprepInfo& sysprep,
    std::vector<std::string>& warnings
)
{
    CloneIndicator generalized;
    generalized.name = "封装已执行待部署";
    generalized.detected = sysprep.sysprepPending;
    generalized.detail = "ImageState=" + (sysprep.imageState.empty() ? "unknown" : sysprep.imageState) +
        " OOBEInProgress=" + (sysprep.oobeInProgress ? "1" : "0") +
        " CleanupState=" + std::to_string(sysprep.cleanupState);
    out.push_back(generalized);

    const std::wstring pantherUnattend = winDir() + L"\\Panther\\unattend.xml";
    CloneIndicator panther;
    panther.name = "Panther 无人值守文件残留";
    panther.detected = GetFileAttributesW(pantherUnattend.c_str()) != INVALID_FILE_ATTRIBUTES;
    panther.detail = toUtf8(pantherUnattend);
    out.push_back(panther);

    const std::wstring setupErr = winDir() + L"\\System32\\Sysprep\\Panther\\setuperr.log";
    CloneIndicator setuperr;
    setuperr.name = "Sysprep 日志残留";
    setuperr.detected = win7::probeFileNative(setupErr).exists;
    setuperr.detail = toUtf8(setupErr);
    out.push_back(setuperr);

    wchar_t host[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD hostLen = static_cast<DWORD>(std::size(host));
    std::string hostName;
    if (GetComputerNameW(host, &hostLen) != 0)
    {
        hostName = toUtf8(std::wstring(host, hostLen));
    }
    else
    {
        warnings.push_back("GetComputerName 失败");
    }

    const bool defaultName = hostName.rfind("DESKTOP-", 0) == 0 || hostName.rfind("WIN-", 0) == 0;
    CloneIndicator name;
    name.name = "计算机名为系统默认随机名";
    name.detected = defaultName;
    name.detail = hostName.empty() ? "读取失败" : hostName;
    out.push_back(name);

    // 只有前三项属于"已封装/克隆"的强信号，主机名仅作参考。
    likelyClone = generalized.detected || panther.detected || setuperr.detected;
}

// ---------------------------------------------------------------- 杀软（WMI）

void fillAntivirusInfo(AntivirusInfo& info, std::vector<std::string>& warnings)
{
    CoInitScope com;
    if (!com.ok())
    {
        info.error = "COM 初始化失败";
        warnings.push_back("杀软状态: " + info.error);
        return;
    }

    // 已初始化过安全设置时返回 RPC_E_TOO_LATE，忽略即可。
    CoInitializeSecurity(
        nullptr,
        -1,
        nullptr,
        nullptr,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE,
        nullptr
    );

    ComPtr<IWbemLocator> locator;
    HRESULT hr = CoCreateInstance(
        CLSID_WbemLocator,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_IWbemLocator,
        reinterpret_cast<void**>(locator.put())
    );
    if (FAILED(hr))
    {
        info.error = "CoCreateInstance(WbemLocator) 失败";
        warnings.push_back("杀软状态: " + info.error);
        return;
    }

    ComPtr<IWbemServices> service;
    hr = locator->ConnectServer(
        _bstr_t(L"ROOT\\SecurityCenter2"), nullptr, nullptr, nullptr, 0, nullptr, nullptr, service.put()
    );
    if (FAILED(hr))
    {
        info.error = "ROOT\\SecurityCenter2 不可用（Server 系统或旧版 Windows 无此命名空间）";
        warnings.push_back("杀软状态: " + info.error);
        return;
    }

    hr = CoSetProxyBlanket(
        service.get(),
        RPC_C_AUTHN_WINNT,
        RPC_C_AUTHZ_NONE,
        nullptr,
        RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE
    );
    if (FAILED(hr))
    {
        info.error = "CoSetProxyBlanket 失败";
        warnings.push_back("杀软状态: " + info.error);
        return;
    }

    ComPtr<IEnumWbemClassObject> enumerator;
    hr = service->ExecQuery(
        _bstr_t(L"WQL"),
        _bstr_t(L"SELECT displayName, productState, timestamp FROM "
                L"AntiVirusProduct"),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr,
        enumerator.put()
    );
    if (FAILED(hr))
    {
        info.error = "AntiVirusProduct 查询失败";
        warnings.push_back("杀软状态: " + info.error);
        return;
    }

    info.queried = true;

    for (;;)
    {
        ULONG returned = 0;
        ComPtr<IWbemClassObject> object;
        const HRESULT next = enumerator->Next(WBEM_INFINITE, 1, object.put(), &returned);
        if (FAILED(next) || returned == 0)
        {
            break;
        }

        _variant_t nameVar;
        if (SUCCEEDED(object->Get(L"displayName", 0, &nameVar, nullptr, nullptr)) && nameVar.vt == VT_BSTR)
        {
            AntivirusProduct product;
            product.displayName = toUtf8(nameVar.bstrVal);

            _variant_t stateVar;
            if (SUCCEEDED(object->Get(L"productState", 0, &stateVar, nullptr, nullptr)))
            {
                product.productState = static_cast<std::uint32_t>(stateVar.lVal);
                // 位语义：0x1000 表示实时防护开启，0x10 表示特征库为最新。
                product.enabled = (product.productState & 0x1000u) != 0;
                product.upToDate = (product.productState & 0x10u) != 0;
                info.anyEnabled = info.anyEnabled || product.enabled;
            }
            info.products.push_back(std::move(product));
        }
    }

    if (info.products.empty())
    {
        info.error = "未注册任何杀毒产品（Windows Defender 可能已被第三方接管或禁用）";
    }
}

// ---------------------------------------------------------------- 启动项

void readRunKey(
    HKEY root, const wchar_t* subKey, const char* scopeLabel, REGSAM extra, std::vector<StartupItem>& out
)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ | extra, &key) != ERROR_SUCCESS)
    {
        return;
    }

    for (DWORD index = 0;; ++index)
    {
        wchar_t name[512] = {};
        DWORD nameLen = static_cast<DWORD>(std::size(name));
        wchar_t data[4096] = {};
        DWORD dataBytes = sizeof(data) - sizeof(wchar_t);
        DWORD type = 0;

        const LONG rc = RegEnumValueW(
            key, index, name, &nameLen, nullptr, &type, reinterpret_cast<LPBYTE>(data), &dataBytes
        );
        if (rc == ERROR_NO_MORE_ITEMS)
        {
            break;
        }
        if (rc != ERROR_SUCCESS)
        {
            continue;
        }
        if (type != REG_SZ && type != REG_EXPAND_SZ)
        {
            continue;
        }

        data[dataBytes / sizeof(wchar_t)] = L'\0';

        StartupItem item;
        item.scope = scopeLabel;
        item.name = toUtf8(std::wstring(name, nameLen));
        item.command = toUtf8(envExpand(data));
        out.push_back(std::move(item));
    }

    RegCloseKey(key);
}

void readStartupFolder(const std::wstring& dir, std::vector<StartupItem>& out)
{
    WIN32_FIND_DATAW fd = {};
    const std::wstring pattern = dir + L"\\*";
    const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE)
    {
        return;
    }

    do
    {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            continue;
        }
        StartupItem item;
        item.scope = "StartupFolder";
        item.name = toUtf8(fd.cFileName);
        item.command = toUtf8(dir + L"\\" + fd.cFileName);
        out.push_back(std::move(item));
    } while (FindNextFileW(find, &fd) != 0);

    FindClose(find);
}

void fillStartupItems(std::vector<StartupItem>& out)
{
    readRunKey(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
        "HKLM\\Run",
        KEY_WOW64_64KEY,
        out
    );
    readRunKey(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce",
        "HKLM\\RunOnce",
        KEY_WOW64_64KEY,
        out
    );
    readRunKey(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Run",
        "HKLM\\Wow6432Node\\Run",
        0,
        out
    );
    readRunKey(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", "HKCU\\Run", 0, out);
    readRunKey(
        HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce", "HKCU\\RunOnce", 0, out
    );

    std::wstring appData;
    if (readRegString(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\"
            L"Shell Folders",
            L"Startup",
            appData
        ))
    {
        readStartupFolder(envExpand(appData), out);
    }

    wchar_t programData[MAX_PATH * 2] = {};
    if (GetEnvironmentVariableW(L"ProgramData", programData, static_cast<DWORD>(std::size(programData))) > 0)
    {
        readStartupFolder(
            std::wstring(programData) + L"\\Microsoft\\Windows\\Start Menu\\Programs\\StartUp", out
        );
    }
}

// ---------------------------------------------------------------- 时间

std::string nowUtcIso()
{
    SYSTEMTIME st = {};
    GetSystemTime(&st);
    char buffer[32] = {};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%04u-%02u-%02uT%02u:%02u:%02uZ",
        st.wYear,
        st.wMonth,
        st.wDay,
        st.wHour,
        st.wMinute,
        st.wSecond
    );
    return buffer;
}

std::string formatBytes(std::uint64_t bytes)
{
    constexpr double kKiB = 1024.0;
    constexpr double kMiB = kKiB * 1024.0;
    constexpr double kGiB = kMiB * 1024.0;

    char buffer[64] = {};
    const double value = static_cast<double>(bytes);
    if (value >= kGiB)
    {
        std::snprintf(buffer, sizeof(buffer), "%.2f GiB", value / kGiB);
    }
    else if (value >= kMiB)
    {
        std::snprintf(buffer, sizeof(buffer), "%.2f MiB", value / kMiB);
    }
    else
    {
        std::snprintf(buffer, sizeof(buffer), "%.0f B", value);
    }
    return buffer;
}

} // namespace

// ---------------------------------------------------------------- 采集

// ---------------------------------------------------------------- 体检项（M10）

// 这些检查来自 docs/开源参考调研.md §2.1：SysprepPreparator 把检查做成
// CCP（Compatibility Checker Provider），每项返回 Compatible + Severity + Resolution。
// 严重级别和修复建议本身就是产物 —— --sysprep 直接消费 critical 项当 blockers。

namespace {

bool fileExists(const std::wstring& path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

Check makeCheck(
    const char* id,
    const char* severity,
    const char* title,
    bool detected,
    std::string message,
    std::string resolution,
    std::string evidence
)
{
    Check check;
    check.id = id;
    check.severity = severity;
    check.title = title;
    check.detected = detected;
    check.message = std::move(message);
    check.resolution = std::move(resolution);
    check.evidence = std::move(evidence);
    return check;
}

// 待处理的服务操作：CBS 或 Windows Update 挂起的更新会让 generalize 失败。
void checkPendingServicing(std::vector<Check>& checks)
{
    const std::wstring win = winDir();

    std::vector<std::string> hits;

    const std::wstring pendingXml = win + L"\\WinSxS\\pending.xml";
    if (fileExists(pendingXml))
    {
        hits.push_back("%WINDIR%\\WinSxS\\pending.xml");
    }

    std::uint32_t value = 0;
    if (readRegDword(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\RebootPending",
            L"",
            value
        ))
    {
        hits.push_back("CBS\\RebootPending");
    }
    if (readRegDword(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\RebootRequired",
            L"",
            value
        ))
    {
        hits.push_back("WindowsUpdate\\RebootRequired");
    }

    std::string evidence;
    for (const std::string& hit : hits)
    {
        if (!evidence.empty())
        {
            evidence += "；";
        }
        evidence += hit;
    }

    checks.push_back(makeCheck(
        "pendingServicing",
        "critical",
        "待处理的服务操作",
        !hits.empty(),
        "系统里有挂起的更新/组件操作，generalize 会失败",
        "先重启让挂起的操作完成，再重新体检；必要时 dism /online /cleanup-image /startcomponentcleanup",
        evidence
    ));
}

// 待重命名文件：实测在大量健康机器上存在，sysprep 不会因此失败 —— 只记录，不阻断。
void checkPendingFileRename(std::vector<Check>& checks)
{
    std::wstring value;
    const bool found = readRegString(
        HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\Session Manager",
        L"PendingFileRenameOperations",
        value
    );
    checks.push_back(makeCheck(
        "pendingFileRename",
        "info",
        "待重命名文件",
        found && !value.empty(),
        "存在待重启后重命名的文件（健康机器上也很常见）",
        "不阻断封装；如需清空请重启一次",
        found ? "PendingFileRenameOperations 非空" : ""
    ));
}

// 判断 parent 命名空间下是否登记了名为 name 的子命名空间。
//
// 为什么先探存在性再连：连接一个不存在的 WMI 命名空间**不会立刻失败**，
// 在本机实测要卡满 5 秒（连 ROOT\CIMV2\Security\MicrosoftVolumeEncryption 用时 5024 ms，
// 同一台机器上连 ROOT\CIMV2 只要 3 ms）。体检与界面渲染都不该为一个外部服务的
// 坏状态白等，所以先用 __Namespace 判存在性，不存在就直接按「未加密」收工。
// 注意 Windows 把命名空间存在 WMI 仓库（OBJECTS.DATA）里，不是注册表，
// 所以只能这么问，没有更便宜的权威途径。
bool wmiSubNamespaceExists(IWbemServices* parent, const wchar_t* name)
{
    if (parent == nullptr || name == nullptr)
    {
        return false;
    }

    ComPtr<IEnumWbemClassObject> enumerator;
    if (FAILED(parent->ExecQuery(
            _bstr_t(L"WQL"),
            _bstr_t(L"SELECT Name FROM __Namespace"),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr,
            enumerator.put()
        )))
    {
        return false;
    }

    for (;;)
    {
        ULONG returned = 0;
        ComPtr<IWbemClassObject> object;
        if (FAILED(enumerator->Next(0, 1, object.put(), &returned)) || returned == 0)
        {
            return false;
        }

        _variant_t nameVar;
        if (SUCCEEDED(object->Get(L"Name", 0, &nameVar, nullptr, nullptr)) && nameVar.vt == VT_BSTR &&
            nameVar.bstrVal != nullptr && std::wcscmp(nameVar.bstrVal, name) == 0)
        {
            return true;
        }
    }
}

// BitLocker：必须是「完全解密」，挂起不够 —— 加密卷上 sysprep 可能让数据不可恢复。
void checkBitLocker(std::vector<Check>& checks)
{
    Check check = makeCheck(
        "bitlocker",
        "critical",
        "BitLocker 加密状态",
        false,
        "系统卷仍处于加密或加密挂起状态",
        "先 manage-bde -off C: 并等完全解密（manage-bde -status 显示 Fully Decrypted）再封装",
        ""
    );

    CoInitScope com;
    if (!com.ok())
    {
        check.evaluated = false;
        check.evidence = "COM 初始化失败，未检查";
        checks.push_back(std::move(check));
        return;
    }
    CoInitializeSecurity(
        nullptr,
        -1,
        nullptr,
        nullptr,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE,
        nullptr
    );

    ComPtr<IWbemLocator> locator;
    if (FAILED(CoCreateInstance(
            CLSID_WbemLocator,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_IWbemLocator,
            reinterpret_cast<void**>(locator.put())
        )))
    {
        check.evaluated = false;
        check.evidence = "WbemLocator 创建失败，未检查";
        checks.push_back(std::move(check));
        return;
    }

    ComPtr<IWbemServices> cimv2;
    if (FAILED(locator->ConnectServer(
            _bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, nullptr, 0, nullptr, nullptr, cimv2.put()
        )))
    {
        check.evaluated = false;
        check.evidence = "连不上 ROOT\\CIMV2，未检查";
        checks.push_back(std::move(check));
        return;
    }

    const bool hasSecurity = wmiSubNamespaceExists(cimv2.get(), L"Security");
    // 先用 __Namespace 探存在性再连：直接连缺失的命名空间会白等 5 秒（见探针注释）
    if (!hasSecurity)
    {
        check.evaluated = true;
        check.evidence = "ROOT\\CIMV2\\Security 不存在，按未加密处理";
        checks.push_back(std::move(check));
        return;
    }

    ComPtr<IWbemServices> security;
    if (FAILED(locator->ConnectServer(
            _bstr_t(L"ROOT\\CIMV2\\Security"), nullptr, nullptr, nullptr, 0, nullptr, nullptr, security.put()
        )) ||
        !wmiSubNamespaceExists(security.get(), L"MicrosoftVolumeEncryption"))
    {
        // 没有这个命名空间的系统（精简版 / Win7 家庭版 / 未启用 BitLocker 组件）视为未加密
        check.evaluated = true;
        check.evidence = "ROOT\\CIMV2\\Security\\MicrosoftVolumeEncryption 不可用，按未加密处理";
        checks.push_back(std::move(check));
        return;
    }

    ComPtr<IWbemServices> service;
    if (FAILED(locator->ConnectServer(
            _bstr_t(L"ROOT\\CIMV2\\Security\\MicrosoftVolumeEncryption"),
            nullptr,
            nullptr,
            nullptr,
            0,
            nullptr,
            nullptr,
            service.put()
        )))
    {
        // 探针说有、连却失败：如实报「未检查」，不要当成未加密
        check.evaluated = false;
        check.evidence = "ROOT\\CIMV2\\Security\\MicrosoftVolumeEncryption 连接失败，未检查";
        checks.push_back(std::move(check));
        return;
    }
    CoSetProxyBlanket(
        service.get(),
        RPC_C_AUTHN_WINNT,
        RPC_C_AUTHZ_NONE,
        nullptr,
        RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE
    );

    ComPtr<IEnumWbemClassObject> enumerator;
    if (FAILED(service->ExecQuery(
            _bstr_t(L"WQL"),
            _bstr_t(L"SELECT DriveLetter, ConversionStatus, ProtectionStatus FROM Win32_EncryptableVolume"),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr,
            enumerator.put()
        )))
    {
        check.evaluated = false;
        check.evidence = "Win32_EncryptableVolume 查询失败，未检查";
        checks.push_back(std::move(check));
        return;
    }

    std::string evidence;
    for (;;)
    {
        ULONG returned = 0;
        ComPtr<IWbemClassObject> object;
        if (FAILED(enumerator->Next(WBEM_INFINITE, 1, object.put(), &returned)) || returned == 0)
        {
            break;
        }

        _variant_t conversion;
        _variant_t protection;
        _variant_t letter;
        const long conversionValue =
            SUCCEEDED(object->Get(L"ConversionStatus", 0, &conversion, nullptr, nullptr)) ? conversion.lVal
                                                                                          : 0;
        const long protectionValue =
            SUCCEEDED(object->Get(L"ProtectionStatus", 0, &protection, nullptr, nullptr)) ? protection.lVal
                                                                                          : 0;
        std::string name = "系统卷";
        if (SUCCEEDED(object->Get(L"DriveLetter", 0, &letter, nullptr, nullptr)) && letter.vt == VT_BSTR)
        {
            name = toUtf8(letter.bstrVal);
        }

        // ConversionStatus：0 = FullyDecrypted，1 = FullyEncrypted，2 = EncryptionInProgress，
        // 3 = DecryptionInProgress，4 = EncryptionPaused，5 = DecryptionPaused
        const bool encrypted = conversionValue != 0;
        if (encrypted || protectionValue != 0)
        {
            if (!evidence.empty())
            {
                evidence += "；";
            }
            evidence += name + " ConversionStatus=" + std::to_string(conversionValue) +
                " ProtectionStatus=" + std::to_string(protectionValue);
        }
    }

    check.detected = !evidence.empty();
    check.evidence = evidence;
    checks.push_back(std::move(check));
}

// 域加入：generalize 会破坏域信任。
void checkDomainJoin(std::vector<Check>& checks)
{
    Check check = makeCheck(
        "domainJoin",
        "warning",
        "已加入域",
        false,
        "本机已加入域，封装后目标机需要重新加域，原信任关系会失效",
        "确认目标机加域方式（unattend 的 UnattendedJoin 或部署后手工），并准备域账号",
        ""
    );

    LPWSTR name = nullptr;
    NETSETUP_JOIN_STATUS status = NetSetupUnknownStatus;
    const DWORD rc = NetGetJoinInformation(nullptr, &name, &status);
    if (rc == NERR_Success)
    {
        if (status == NetSetupDomainName)
        {
            check.detected = true;
            check.evidence = "NetGetJoinInformation: " + toUtf8(name);
        }
        else
        {
            check.evidence = "NetGetJoinInformation: 工作组";
        }
        if (name != nullptr)
        {
            NetApiBufferFree(name);
        }
    }
    else
    {
        check.evaluated = false;
        check.evidence = "NetGetJoinInformation 失败，错误码 " + std::to_string(rc);
    }
    checks.push_back(std::move(check));
}

// 就地升级痕迹：会随镜像带进目标机。
void checkInPlaceUpgrade(std::vector<Check>& checks)
{
    const std::wstring win = winDir();
    std::string evidence;

    const std::wstring windowsOld = win.substr(0, win.find_last_of(L'\\')) + L"\\Windows.old";
    if (fileExists(windowsOld))
    {
        evidence = "存在 Windows.old";
    }

    HKEY setup = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\Setup", 0, KEY_READ, &setup) == ERROR_SUCCESS)
    {
        wchar_t name[256] = {};
        DWORD index = 0;
        for (;;)
        {
            DWORD length = static_cast<DWORD>(std::size(name));
            if (RegEnumKeyExW(setup, index, name, &length, nullptr, nullptr, nullptr, nullptr) !=
                ERROR_SUCCESS)
            {
                break;
            }
            ++index;
            if (std::wstring(name).rfind(L"Source OS", 0) == 0)
            {
                if (!evidence.empty())
                {
                    evidence += "；";
                }
                evidence += "HKLM\\SYSTEM\\Setup\\" + toUtf8(std::wstring(name, length));
            }
        }
        RegCloseKey(setup);
    }

    checks.push_back(makeCheck(
        "inPlaceUpgrade",
        "warning",
        "就地升级痕迹",
        !evidence.empty(),
        "镜像来自就地升级，会带上旧系统残留（体积与兼容性都有影响）",
        "确认是否需要清理；Windows.old 可在封装前删除以减小镜像",
        evidence
    ));
}

// 无人值守残留：下次 Setup 可能重跑这份应答文件。
void checkUnattendResidue(std::vector<Check>& checks)
{
    const std::wstring win = winDir();
    const std::wstring systemDrive = win.substr(0, 2) + L"\\";

    const std::pair<const wchar_t*, const char*> candidates[] = {
        {(win + L"\\Panther\\unattend.xml").c_str(), "%WINDIR%\\Panther\\unattend.xml"},
        {(win + L"\\System32\\Sysprep\\unattend.xml").c_str(), "%WINDIR%\\System32\\Sysprep\\unattend.xml"},
        {(systemDrive + L"unattend.xml").c_str(), "系统盘根目录 unattend.xml"},
    };

    std::string evidence;
    for (const auto& candidate : candidates)
    {
        if (fileExists(candidate.first))
        {
            if (!evidence.empty())
            {
                evidence += "；";
            }
            evidence += candidate.second;
        }
    }

    checks.push_back(makeCheck(
        "unattendResidue",
        "warning",
        "无人值守文件残留",
        !evidence.empty(),
        "系统里还有旧的应答文件，下一次 Setup 可能重跑它",
        "封装前确认这些文件是否该保留；本工具生成的 unattend 由 --sysprep 指定路径",
        evidence
    ));
}

// 系统卷脏位：建议先 chkdsk。fsutil 需要管理员，未提权时标记为未检查。
void checkDirtyBit(std::vector<Check>& checks)
{
    Check check = makeCheck(
        "dirtyBit",
        "warning",
        "系统卷脏位",
        false,
        "系统卷被标记为脏，建议先跑 chkdsk",
        "chkdsk C: /f 后重启",
        ""
    );

    wchar_t systemDir[MAX_PATH] = {};
    if (GetSystemDirectoryW(systemDir, static_cast<UINT>(std::size(systemDir))) == 0)
    {
        check.evaluated = false;
        check.evidence = "取不到 System32 路径，未检查";
        checks.push_back(std::move(check));
        return;
    }
    const std::string fsutil = toUtf8(std::wstring(systemDir)) + "\\fsutil.exe";
    const std::string drive =
        std::string(1, static_cast<char>(toupper(static_cast<unsigned char>(systemDir[0]))));

    std::uint32_t code = 0;
    std::string output;
    std::string error;
    if (!dism::runCommandCaptured("\"" + fsutil + "\" dirty query " + drive + ":", code, output, error))
    {
        check.evaluated = false;
        check.evidence = "无法运行 fsutil：" + error;
        checks.push_back(std::move(check));
        return;
    }
    if (code != 0)
    {
        check.evaluated = false;
        check.evidence = "fsutil 退出码 " + std::to_string(code) + "（通常是没有管理员权限）";
        checks.push_back(std::move(check));
        return;
    }

    // 英文输出 "Volume - C: is Dirty"，中文输出「卷 - C: 是脏的」。
    // 只认 ASCII 关键字，避免编码问题。
    const bool dirty =
        output.find("is Dirty") != std::string::npos || output.find("Dirty") != std::string::npos;
    check.detected = dirty;
    check.evidence = dirty ? "fsutil dirty query 报告脏位已置" : "fsutil dirty query 报告干净";
    checks.push_back(std::move(check));
}

// 已安装但未 provision 给所有用户的 AppX 会让 generalize 报 0x80073cf2。
// 精确判定要靠 Get-AppxPackage -AllUsers（PowerShell），这里只报「存在已注册的
// AppX 包」并明确写成只读提示 —— 不对 Win10/11 每台机器都刷一条 warning。
void checkStagedAppx(std::vector<Check>& checks)
{
    Check check = makeCheck(
        "appxPackages",
        "info",
        "AppX 包（只读提示）",
        false,
        "系统里有已注册的 AppX 包；若有包只装给了某个用户而未 provision 给所有用户，"
        "generalize 会报 0x80073cf2",
        "在目标机上用 Get-AppxPackage -AllUsers 核对，或先移除这些包再封装",
        ""
    );

    HKEY packages = nullptr;
    if (RegOpenKeyExW(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Appx\\AppxAllUserStore\\Applications",
            0,
            KEY_READ,
            &packages
        ) != ERROR_SUCCESS)
    {
        check.evaluated = false;
        check.evidence = "AppxAllUserStore\\Applications 不存在（Win7 或无 Store 的系统），未检查";
        checks.push_back(std::move(check));
        return;
    }

    std::size_t packageCount = 0;
    wchar_t name[512] = {};
    DWORD index = 0;
    for (;;)
    {
        DWORD length = static_cast<DWORD>(std::size(name));
        if (RegEnumKeyExW(packages, index, name, &length, nullptr, nullptr, nullptr, nullptr) !=
            ERROR_SUCCESS)
        {
            break;
        }
        ++index;
        // 只数形如 <name>_<version>_<arch>__<publisherId> 的包键，跳过 <SID> 子键
        if (std::wstring(name, length).find(L'_') != std::wstring::npos)
        {
            ++packageCount;
        }
    }
    RegCloseKey(packages);

    check.detected = packageCount > 0;
    check.evidence = "AppxAllUserStore\\Applications 下 " + std::to_string(packageCount) + " 个包";
    checks.push_back(std::move(check));
}

void fillChecks(CheckupReport& report)
{
    checkPendingServicing(report.checks);
    checkPendingFileRename(report.checks);
    checkBitLocker(report.checks);
    checkDomainJoin(report.checks);
    checkInPlaceUpgrade(report.checks);
    checkUnattendResidue(report.checks);
    checkDirtyBit(report.checks);
    checkStagedAppx(report.checks);
    checkTweaks(report.checks);
}

} // namespace

std::vector<const Check*> blockingChecks(const CheckupReport& report)
{
    std::vector<const Check*> blocking;
    for (const Check& check : report.checks)
    {
        if (check.severity == "critical" && check.detected)
        {
            blocking.push_back(&check);
        }
    }
    return blocking;
}

namespace {

CheckupReport collectImpl()
{
    CheckupReport report;
    report.generatedAtUtc = nowUtcIso();

    wchar_t host[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD hostLen = static_cast<DWORD>(std::size(host));
    if (GetComputerNameW(host, &hostLen) != 0)
    {
        report.hostName = toUtf8(std::wstring(host, hostLen));
    }
    else
    {
        report.warnings.push_back("GetComputerName 失败");
    }

    fillOsInfo(report.os, report.warnings);
    fillSysprepInfo(report.sysprep, report.warnings);
    fillPartitions(report.partitions, report.warnings);
    fillRequiredFiles(report.requiredFiles, report.warnings);
    fillCloneIndicators(report.cloneIndicators, report.isLikelyClone, report.sysprep, report.warnings);
    fillAntivirusInfo(report.antivirus, report.warnings);
    fillStartupItems(report.startupItems);
    fillChecks(report);

    return report;
}

} // namespace

CheckupReport collect()
{
    return collectImpl();
}

// ---------------------------------------------------------------- JSON

std::string toJson(const CheckupReport& report)
{
    using nlohmann::json;

    json root;
    root["schemaVersion"] = 1;
    root["generatedAtUtc"] = report.generatedAtUtc;
    root["hostName"] = report.hostName;

    json& os = root["os"];
    os["productName"] = report.os.productName;
    os["editionId"] = report.os.editionId;
    os["displayVersion"] = report.os.displayVersion;
    os["buildLab"] = report.os.buildLab;
    os["major"] = report.os.major;
    os["minor"] = report.os.minor;
    os["build"] = report.os.build;
    os["revision"] = report.os.revision;
    os["version"] = report.os.version;
    os["architecture"] = report.os.architecture;
    os["is64BitOs"] = report.os.is64BitOs;
    os["isServer"] = report.os.isServer;

    json& sysprep = root["sysprep"];
    sysprep["available"] = report.sysprep.available;
    sysprep["exePath"] = report.sysprep.exePath;
    sysprep["imageState"] = report.sysprep.imageState;
    sysprep["sysprepPending"] = report.sysprep.sysprepPending;
    sysprep["oobeInProgress"] = report.sysprep.oobeInProgress;
    sysprep["cleanupState"] = report.sysprep.cleanupState;
    sysprep["generalizationState"] = report.sysprep.generalizationState;
    sysprep["note"] = report.sysprep.note;

    json partitions = json::array();
    for (const PartitionInfo& p : report.partitions)
    {
        json item;
        item["drive"] = p.drive;
        item["volumeLabel"] = p.volumeLabel;
        item["fileSystem"] = p.fileSystem;
        item["driveType"] = p.driveType;
        item["totalBytes"] = p.totalBytes;
        item["freeBytes"] = p.freeBytes;
        item["usedBytes"] = p.usedBytes;
        item["usedPercent"] = p.usedPercent;
        item["systemDrive"] = p.systemDrive;
        partitions.push_back(std::move(item));
    }
    root["partitions"] = std::move(partitions);

    json files = json::array();
    for (const RequiredFile& f : report.requiredFiles)
    {
        json item;
        item["path"] = f.path;
        item["exists"] = f.exists;
        item["sizeBytes"] = f.sizeBytes;
        files.push_back(std::move(item));
    }
    root["requiredFiles"] = std::move(files);

    json indicators = json::array();
    for (const CloneIndicator& c : report.cloneIndicators)
    {
        json item;
        item["name"] = c.name;
        item["detected"] = c.detected;
        item["detail"] = c.detail;
        indicators.push_back(std::move(item));
    }
    root["cloneIndicators"] = std::move(indicators);
    root["isLikelyClone"] = report.isLikelyClone;

    json& av = root["antivirus"];
    av["queried"] = report.antivirus.queried;
    av["error"] = report.antivirus.error;
    av["anyEnabled"] = report.antivirus.anyEnabled;
    json products = json::array();
    for (const AntivirusProduct& p : report.antivirus.products)
    {
        json item;
        item["displayName"] = p.displayName;
        item["productState"] = p.productState;
        item["enabled"] = p.enabled;
        item["upToDate"] = p.upToDate;
        products.push_back(std::move(item));
    }
    av["products"] = std::move(products);

    json startup = json::array();
    for (const StartupItem& s : report.startupItems)
    {
        json item;
        item["scope"] = s.scope;
        item["name"] = s.name;
        item["command"] = s.command;
        startup.push_back(std::move(item));
    }
    root["startupItems"] = std::move(startup);

    json checks = json::array();
    for (const Check& check : report.checks)
    {
        json item;
        item["id"] = check.id;
        item["severity"] = check.severity;
        item["title"] = check.title;
        item["detected"] = check.detected;
        item["evaluated"] = check.evaluated;
        item["message"] = check.message;
        item["resolution"] = check.resolution;
        item["evidence"] = check.evidence;
        item["autoFix"] = check.autoFix;
        checks.push_back(std::move(item));
    }
    root["checks"] = std::move(checks);

    json blocking = json::array();
    for (const Check* check : blockingChecks(report))
    {
        blocking.push_back(check->id);
    }
    root["blocking"] = std::move(blocking);

    root["warnings"] = report.warnings;

    return root.dump(2);
}

// ---------------------------------------------------------------- 文本摘要

std::string toSummaryText(const CheckupReport& report)
{
    std::string text;
    const auto line = [&text](const std::string& s) {
        text += s;
        text += '\n';
    };

    line("=== 系统体检 ===");
    line("生成时间: " + report.generatedAtUtc + "   主机: " + report.hostName);
    line("");

    line("[系统]");
    line("  产品名    : " + report.os.productName + " (" + report.os.editionId + ")");
    line("  版本      : " + report.os.version + "  " + report.os.displayVersion);
    line("  架构      : " + report.os.architecture + (report.os.is64BitOs ? " (64 位)" : " (32 位)"));
    line("  服务器版  : " + std::string(report.os.isServer ? "是" : "否"));
    line("");

    line("[sysprep]");
    line("  可用      : " + std::string(report.sysprep.available ? "是" : "否"));
    line("  路径      : " + report.sysprep.exePath);
    line("  ImageState: " + (report.sysprep.imageState.empty() ? "(未读到)" : report.sysprep.imageState));
    line("  待部署    : " + std::string(report.sysprep.sysprepPending ? "是" : "否"));
    line("  说明      : " + report.sysprep.note);
    line("");

    line("[分区]");
    for (const PartitionInfo& p : report.partitions)
    {
        char percent[32] = {};
        std::snprintf(percent, sizeof(percent), "%.1f%%", p.usedPercent);
        line(
            "  " + p.drive + " " + p.fileSystem + " " + p.driveType + "  总 " + formatBytes(p.totalBytes) +
            "  可用 " + formatBytes(p.freeBytes) + "  已用 " + percent + (p.systemDrive ? "  [系统盘]" : "")
        );
    }
    line("");

    line("[必备文件]");
    for (const RequiredFile& f : report.requiredFiles)
    {
        line(std::string("  ") + (f.exists ? "OK   " : "缺失 ") + f.path);
    }
    line("");

    line("[克隆版检测]  判定: " + std::string(report.isLikelyClone ? "疑似克隆/已封装" : "正常"));
    for (const CloneIndicator& c : report.cloneIndicators)
    {
        line(std::string("  ") + (c.detected ? "[命中] " : "[正常] ") + c.name + "  (" + c.detail + ")");
    }
    line("");

    line("[杀软]");
    if (!report.antivirus.queried)
    {
        line("  无法查询: " + report.antivirus.error);
    }
    else if (report.antivirus.products.empty())
    {
        line("  未发现已注册的杀毒产品");
    }
    else
    {
        for (const AntivirusProduct& p : report.antivirus.products)
        {
            line(
                "  " + p.displayName + (p.enabled ? "  [实时防护开启]" : "  [未开启]") +
                (p.upToDate ? "  [库最新]" : "  [库过期]")
            );
        }
    }
    line("");

    line("[启动项]  共 " + std::to_string(report.startupItems.size()) + " 项");
    for (const StartupItem& s : report.startupItems)
    {
        line("  " + s.scope + "  " + s.name + " = " + s.command);
    }

    // 体检项按 severity 排序输出：critical 在最前，其次是 warning，info 最后。
    // 阻断项单独点名，因为它们决定 --sysprep 能不能跑。
    if (!report.checks.empty())
    {
        line("");
        line("[体检项]");
        const char* order[] = {"critical", "warning", "info"};
        for (const char* severity : order)
        {
            for (const Check& check : report.checks)
            {
                if (check.severity != severity)
                {
                    continue;
                }
                std::string mark = "[未查] ";
                if (check.evaluated)
                {
                    mark = check.detected ? "[命中] " : "[正常] ";
                }
                line("  " + mark + check.title + "  (" + check.severity + ")");
                if (check.detected && !check.message.empty())
                {
                    line("        " + check.message);
                }
                if (check.detected && !check.resolution.empty())
                {
                    line("        处理: " + check.resolution);
                }
                if (!check.evidence.empty())
                {
                    line("        依据: " + check.evidence);
                }
            }
        }

        const std::vector<const Check*> blocking = blockingChecks(report);
        line("");
        if (blocking.empty())
        {
            line("阻断项: 无（可以封装）");
        }
        else
        {
            line("阻断项: " + std::to_string(blocking.size()) + " 项，封装前必须解决");
            for (const Check* check : blocking)
            {
                line("  - " + check->title + "：" + check->message);
            }
        }
    }

    if (!report.warnings.empty())
    {
        line("");
        line("[警告]");
        for (const std::string& w : report.warnings)
        {
            line("  - " + w);
        }
    }

    return text;
}

} // namespace osp::sysinfo

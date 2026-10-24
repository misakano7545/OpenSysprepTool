#include "core/sysinfo.hpp"

#include <windows.h>
#include <winternl.h>
#include <comdef.h>
#include <wbemidl.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace osp::sysinfo {
namespace {

// ---------------------------------------------------------------- 小工具

std::string toUtf8(const wchar_t* text)
{
    if (text == nullptr || *text == L'\0') {
        return {};
    }
    const int need = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (need <= 1) {
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
    if (len == 0 || len >= std::size(buffer)) {
        return L"C:\\Windows";
    }
    return std::wstring(buffer, len);
}

// ---------------------------------------------------------------- 注册表

bool readRegString(HKEY root, const wchar_t* subKey, const wchar_t* value, std::wstring& out,
                   REGSAM extra = 0)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ | extra, &key) != ERROR_SUCCESS) {
        return false;
    }
    wchar_t buffer[2048] = {};
    DWORD bytes = sizeof(buffer) - sizeof(wchar_t);
    DWORD type = 0;
    const LONG rc = RegQueryValueExW(key, value, nullptr, &type,
                                     reinterpret_cast<LPBYTE>(buffer), &bytes);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return false;
    }
    buffer[bytes / sizeof(wchar_t)] = L'\0';
    out.assign(buffer);
    return true;
}

bool readRegDword(HKEY root, const wchar_t* subKey, const wchar_t* value, std::uint32_t& out,
                  REGSAM extra = 0)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ | extra, &key) != ERROR_SUCCESS) {
        return false;
    }
    DWORD data = 0;
    DWORD bytes = sizeof(data);
    DWORD type = 0;
    const LONG rc = RegQueryValueExW(key, value, nullptr, &type,
                                     reinterpret_cast<LPBYTE>(&data), &bytes);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || type != REG_DWORD) {
        return false;
    }
    out = data;
    return true;
}

std::wstring envExpand(const std::wstring& raw)
{
    wchar_t buffer[MAX_PATH * 2] = {};
    const DWORD len = ExpandEnvironmentStringsW(raw.c_str(), buffer,
                                                static_cast<DWORD>(std::size(buffer)));
    if (len == 0 || len > std::size(buffer)) {
        return raw;
    }
    return std::wstring(buffer);
}

// ---------------------------------------------------------------- COM RAII

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    T** put()
    {
        reset();
        return &m_ptr;
    }
    T* get() const { return m_ptr; }
    T* operator->() const { return m_ptr; }
    explicit operator bool() const { return m_ptr != nullptr; }

    void reset()
    {
        if (m_ptr != nullptr) {
            m_ptr->Release();
            m_ptr = nullptr;
        }
    }

private:
    T* m_ptr = nullptr;
};

class CoInitScope {
public:
    CoInitScope()
    {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        m_shouldUninit = SUCCEEDED(hr);
        m_ok = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    }
    ~CoInitScope()
    {
        if (m_shouldUninit) {
            CoUninitialize();
        }
    }
    CoInitScope(const CoInitScope&) = delete;
    CoInitScope& operator=(const CoInitScope&) = delete;

    bool ok() const { return m_ok; }

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
    if (ntdll != nullptr) {
        const auto fn = reinterpret_cast<RtlGetVersionFn>(
            reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
        if (fn != nullptr && fn(&vi) == 0) {
            got = true;
        }
    }

    if (got) {
        os.major = vi.dwMajorVersion;
        os.minor = vi.dwMinorVersion;
        os.build = vi.dwBuildNumber;
        os.isServer = vi.wProductType != VER_NT_WORKSTATION;
    } else {
        // 不退回已弃用的 GetVersionExW（/W4 下会报 C4996，且未 manifest 时会撒谎）。
        warnings.push_back("RtlGetVersion 调用失败，版本回退到注册表 CurrentVersion");
        std::uint32_t major = 0;
        std::uint32_t minor = 0;
        std::wstring buildText;
        if (readRegDword(HKEY_LOCAL_MACHINE,
                         L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                         L"CurrentMajorVersionNumber", major)) {
            os.major = major;
        }
        if (readRegDword(HKEY_LOCAL_MACHINE,
                         L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                         L"CurrentMinorVersionNumber", minor)) {
            os.minor = minor;
        }
        if (readRegString(HKEY_LOCAL_MACHINE,
                          L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuild",
                          buildText)) {
            os.build = static_cast<std::uint32_t>(_wtoi(buildText.c_str()));
        }
        // InstallationType 是 REG_SZ："Client" / "Server" / "Server Core"
        std::wstring installationType;
        if (readRegString(HKEY_LOCAL_MACHINE,
                          L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                          L"InstallationType", installationType)) {
            os.isServer = installationType.rfind(L"Server", 0) == 0;
        }
    }

    std::uint32_t ubr = 0;
    if (readRegDword(HKEY_LOCAL_MACHINE,
                     L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"UBR", ubr)) {
        os.revision = ubr;
    }

    char version[64] = {};
    std::snprintf(version, sizeof(version), "%u.%u.%u.%u", os.major, os.minor, os.build,
                  os.revision);
    os.version = version;

    std::wstring value;
    if (readRegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                      L"ProductName", value)) {
        os.productName = toUtf8(value);
    }
    if (readRegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                      L"EditionID", value)) {
        os.editionId = toUtf8(value);
    }
    if (readRegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                      L"DisplayVersion", value)) {
        os.displayVersion = toUtf8(value);
    }
    if (readRegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                      L"BuildLabEx", value)) {
        os.buildLab = toUtf8(value);
    }

    SYSTEM_INFO si = {};
    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
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

    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(exe.c_str(), GetFileExInfoStandard, &fad) != 0) {
        info.available = true;
    } else {
        warnings.push_back("sysprep.exe 不存在: " + info.exePath);
    }

    std::wstring imageState;
    if (readRegString(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Setup\\State", L"ImageState",
                      imageState)) {
        info.imageState = toUtf8(imageState);
    }

    std::uint32_t oobe = 0;
    info.oobeInProgress = readRegDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\Setup", L"OOBEInProgress",
                                       oobe) &&
                          oobe != 0;

    std::uint32_t cleanup = 0;
    std::uint32_t generalization = 0;
    const bool hasCleanup = readRegDword(HKEY_LOCAL_MACHINE,
                                         L"SYSTEM\\Setup\\Status\\SysprepStatus", L"CleanupState",
                                         cleanup);
    const bool hasGeneralization = readRegDword(HKEY_LOCAL_MACHINE,
                                                L"SYSTEM\\Setup\\Status\\SysprepStatus",
                                                L"GeneralizationState", generalization);
    info.cleanupState = cleanup;
    info.generalizationState = generalization;

    // GeneralizationState == 7 是所有正常装完的系统都有的默认值，不能作为"已封装"判据。
    // 真正的"已封装待部署"标志是 ImageState 进入 *_RESEAL_TO_OOBE、OOBE 进行中，
    // 或 CleanupState 非 0（清理阶段未完成）。
    const bool resealToOobe = info.imageState.find("_RESEAL_TO_OOBE") != std::string::npos;
    const bool cleanupPending = hasCleanup && cleanup != 0;

    info.sysprepPending = resealToOobe || info.oobeInProgress || cleanupPending;

    if (info.sysprepPending) {
        info.note = "封装已执行，系统处于待 OOBE/待部署状态，再次 sysprep 可能失败";
    } else if (info.imageState == "IMAGE_STATE_COMPLETE") {
        info.note = "正常完成态（ImageState=IMAGE_STATE_COMPLETE），可执行封装";
    } else if (info.imageState.empty()) {
        info.note = "未读到 ImageState，按正常态处理";
    } else {
        info.note = "ImageState=" + info.imageState + "，未见待部署标记";
    }

    if (!hasGeneralization) {
        // 记录但不影响判定：部分精简系统会缺这个键
        warnings.push_back("SysprepStatus\\GeneralizationState 不存在（不影响判定）");
    }
}

// ---------------------------------------------------------------- 分区

const char* driveTypeName(UINT type)
{
    switch (type) {
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
    const wchar_t systemLetter =
        windows.size() >= 2 ? static_cast<wchar_t>(towupper(windows[0])) : L'C';

    wchar_t drives[512] = {};
    const DWORD len = GetLogicalDriveStringsW(static_cast<DWORD>(std::size(drives) - 1), drives);
    if (len == 0) {
        warnings.push_back("GetLogicalDriveStrings 失败，无法枚举分区");
        return;
    }

    for (const wchar_t* p = drives; *p != L'\0'; p += wcslen(p) + 1) {
        const UINT type = GetDriveTypeW(p);
        if (type == DRIVE_NO_ROOT_DIR) {
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
        if (GetDiskFreeSpaceExW(p, &freeForCaller, &total, &freeTotal) != 0) {
            info.totalBytes = total.QuadPart;
            info.freeBytes = freeTotal.QuadPart;
            info.usedBytes = info.totalBytes - info.freeBytes;
            if (info.totalBytes > 0) {
                info.usedPercent =
                    static_cast<double>(info.usedBytes) * 100.0 / static_cast<double>(info.totalBytes);
            }
        } else {
            warnings.push_back("无法读取容量: " + info.drive);
        }

        wchar_t label[MAX_PATH + 1] = {};
        wchar_t fsName[MAX_PATH + 1] = {};
        if (GetVolumeInformationW(p, label, static_cast<DWORD>(std::size(label)), nullptr, nullptr,
                                  nullptr, fsName, static_cast<DWORD>(std::size(fsName))) != 0) {
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

    for (const std::wstring& path : candidates) {
        RequiredFile file;
        file.path = toUtf8(path);

        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad) != 0) {
            file.exists = true;
            file.sizeBytes =
                (static_cast<std::uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
        } else {
            warnings.push_back("必备文件缺失: " + file.path);
        }
        out.push_back(std::move(file));
    }
}

// ---------------------------------------------------------------- 克隆版检测

void fillCloneIndicators(std::vector<CloneIndicator>& out, bool& likelyClone,
                         const SysprepInfo& sysprep, std::vector<std::string>& warnings)
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
    panther.detected =
        GetFileAttributesW(pantherUnattend.c_str()) != INVALID_FILE_ATTRIBUTES;
    panther.detail = toUtf8(pantherUnattend);
    out.push_back(panther);

    const std::wstring setupErr = winDir() + L"\\System32\\Sysprep\\Panther\\setuperr.log";
    CloneIndicator setuperr;
    setuperr.name = "Sysprep 日志残留";
    setuperr.detected = GetFileAttributesW(setupErr.c_str()) != INVALID_FILE_ATTRIBUTES;
    setuperr.detail = toUtf8(setupErr);
    out.push_back(setuperr);

    wchar_t host[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD hostLen = static_cast<DWORD>(std::size(host));
    std::string hostName;
    if (GetComputerNameW(host, &hostLen) != 0) {
        hostName = toUtf8(std::wstring(host, hostLen));
    } else {
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
    if (!com.ok()) {
        info.error = "COM 初始化失败";
        warnings.push_back("杀软状态: " + info.error);
        return;
    }

    // 已初始化过安全设置时返回 RPC_E_TOO_LATE，忽略即可。
    CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
                         RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);

    ComPtr<IWbemLocator> locator;
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWbemLocator,
                                  reinterpret_cast<void**>(locator.put()));
    if (FAILED(hr)) {
        info.error = "CoCreateInstance(WbemLocator) 失败";
        warnings.push_back("杀软状态: " + info.error);
        return;
    }

    ComPtr<IWbemServices> service;
    hr = locator->ConnectServer(_bstr_t(L"ROOT\\SecurityCenter2"), nullptr, nullptr, nullptr, 0,
                                nullptr, nullptr, service.put());
    if (FAILED(hr)) {
        info.error = "ROOT\\SecurityCenter2 不可用（Server 系统或旧版 Windows 无此命名空间）";
        warnings.push_back("杀软状态: " + info.error);
        return;
    }

    hr = CoSetProxyBlanket(service.get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                           RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr,
                           EOAC_NONE);
    if (FAILED(hr)) {
        info.error = "CoSetProxyBlanket 失败";
        warnings.push_back("杀软状态: " + info.error);
        return;
    }

    ComPtr<IEnumWbemClassObject> enumerator;
    hr = service->ExecQuery(_bstr_t(L"WQL"),
                            _bstr_t(L"SELECT displayName, productState, timestamp FROM "
                                    L"AntiVirusProduct"),
                            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
                            enumerator.put());
    if (FAILED(hr)) {
        info.error = "AntiVirusProduct 查询失败";
        warnings.push_back("杀软状态: " + info.error);
        return;
    }

    info.queried = true;

    for (;;) {
        ULONG returned = 0;
        ComPtr<IWbemClassObject> object;
        const HRESULT next = enumerator->Next(WBEM_INFINITE, 1, object.put(), &returned);
        if (FAILED(next) || returned == 0) {
            break;
        }

        _variant_t nameVar;
        if (SUCCEEDED(object->Get(L"displayName", 0, &nameVar, nullptr, nullptr)) &&
            nameVar.vt == VT_BSTR) {
            AntivirusProduct product;
            product.displayName = toUtf8(nameVar.bstrVal);

            _variant_t stateVar;
            if (SUCCEEDED(object->Get(L"productState", 0, &stateVar, nullptr, nullptr))) {
                product.productState = static_cast<std::uint32_t>(stateVar.lVal);
                // 位语义：0x1000 表示实时防护开启，0x10 表示特征库为最新。
                product.enabled = (product.productState & 0x1000u) != 0;
                product.upToDate = (product.productState & 0x10u) != 0;
                info.anyEnabled = info.anyEnabled || product.enabled;
            }
            info.products.push_back(std::move(product));
        }
    }

    if (info.products.empty()) {
        info.error = "未注册任何杀毒产品（Windows Defender 可能已被第三方接管或禁用）";
    }
}

// ---------------------------------------------------------------- 启动项

void readRunKey(HKEY root, const wchar_t* subKey, const char* scopeLabel, REGSAM extra,
                std::vector<StartupItem>& out)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ | extra, &key) != ERROR_SUCCESS) {
        return;
    }

    for (DWORD index = 0;; ++index) {
        wchar_t name[512] = {};
        DWORD nameLen = static_cast<DWORD>(std::size(name));
        wchar_t data[4096] = {};
        DWORD dataBytes = sizeof(data) - sizeof(wchar_t);
        DWORD type = 0;

        const LONG rc = RegEnumValueW(key, index, name, &nameLen, nullptr, &type,
                                      reinterpret_cast<LPBYTE>(data), &dataBytes);
        if (rc == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (rc != ERROR_SUCCESS) {
            continue;
        }
        if (type != REG_SZ && type != REG_EXPAND_SZ) {
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
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
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
    readRunKey(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
               "HKLM\\Run", KEY_WOW64_64KEY, out);
    readRunKey(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce",
               "HKLM\\RunOnce", KEY_WOW64_64KEY, out);
    readRunKey(HKEY_LOCAL_MACHINE,
               L"SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Run",
               "HKLM\\Wow6432Node\\Run", 0, out);
    readRunKey(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", "HKCU\\Run",
               0, out);
    readRunKey(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce",
               "HKCU\\RunOnce", 0, out);

    std::wstring appData;
    if (readRegString(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\"
                                        L"Shell Folders",
                      L"Startup", appData)) {
        readStartupFolder(envExpand(appData), out);
    }

    wchar_t programData[MAX_PATH * 2] = {};
    if (GetEnvironmentVariableW(L"ProgramData", programData,
                                static_cast<DWORD>(std::size(programData))) > 0) {
        readStartupFolder(std::wstring(programData) +
                              L"\\Microsoft\\Windows\\Start Menu\\Programs\\StartUp",
                          out);
    }
}

// ---------------------------------------------------------------- 时间

std::string nowUtcIso()
{
    SYSTEMTIME st = {};
    GetSystemTime(&st);
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02uZ", st.wYear, st.wMonth,
                  st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buffer;
}

std::string formatBytes(std::uint64_t bytes)
{
    constexpr double kKiB = 1024.0;
    constexpr double kMiB = kKiB * 1024.0;
    constexpr double kGiB = kMiB * 1024.0;

    char buffer[64] = {};
    const double value = static_cast<double>(bytes);
    if (value >= kGiB) {
        std::snprintf(buffer, sizeof(buffer), "%.2f GiB", value / kGiB);
    } else if (value >= kMiB) {
        std::snprintf(buffer, sizeof(buffer), "%.2f MiB", value / kMiB);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.0f B", value);
    }
    return buffer;
}

} // namespace

// ---------------------------------------------------------------- 采集

CheckupReport collect()
{
    CheckupReport report;
    report.generatedAtUtc = nowUtcIso();

    wchar_t host[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD hostLen = static_cast<DWORD>(std::size(host));
    if (GetComputerNameW(host, &hostLen) != 0) {
        report.hostName = toUtf8(std::wstring(host, hostLen));
    } else {
        report.warnings.push_back("GetComputerName 失败");
    }

    fillOsInfo(report.os, report.warnings);
    fillSysprepInfo(report.sysprep, report.warnings);
    fillPartitions(report.partitions, report.warnings);
    fillRequiredFiles(report.requiredFiles, report.warnings);
    fillCloneIndicators(report.cloneIndicators, report.isLikelyClone, report.sysprep,
                        report.warnings);
    fillAntivirusInfo(report.antivirus, report.warnings);
    fillStartupItems(report.startupItems);

    return report;
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
    for (const PartitionInfo& p : report.partitions) {
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
    for (const RequiredFile& f : report.requiredFiles) {
        json item;
        item["path"] = f.path;
        item["exists"] = f.exists;
        item["sizeBytes"] = f.sizeBytes;
        files.push_back(std::move(item));
    }
    root["requiredFiles"] = std::move(files);

    json indicators = json::array();
    for (const CloneIndicator& c : report.cloneIndicators) {
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
    for (const AntivirusProduct& p : report.antivirus.products) {
        json item;
        item["displayName"] = p.displayName;
        item["productState"] = p.productState;
        item["enabled"] = p.enabled;
        item["upToDate"] = p.upToDate;
        products.push_back(std::move(item));
    }
    av["products"] = std::move(products);

    json startup = json::array();
    for (const StartupItem& s : report.startupItems) {
        json item;
        item["scope"] = s.scope;
        item["name"] = s.name;
        item["command"] = s.command;
        startup.push_back(std::move(item));
    }
    root["startupItems"] = std::move(startup);

    root["warnings"] = report.warnings;

    return root.dump(2);
}

// ---------------------------------------------------------------- 文本摘要

std::string toSummaryText(const CheckupReport& report)
{
    std::string text;
    const auto line = [&text](const std::string& s) { text += s; text += '\n'; };

    line("=== 系统体检 ===");
    line("生成时间: " + report.generatedAtUtc + "   主机: " + report.hostName);
    line("");

    line("[系统]");
    line("  产品名    : " + report.os.productName + " (" + report.os.editionId + ")");
    line("  版本      : " + report.os.version + "  " + report.os.displayVersion);
    line("  架构      : " + report.os.architecture +
         (report.os.is64BitOs ? " (64 位)" : " (32 位)"));
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
    for (const PartitionInfo& p : report.partitions) {
        char percent[32] = {};
        std::snprintf(percent, sizeof(percent), "%.1f%%", p.usedPercent);
        line("  " + p.drive + " " + p.fileSystem + " " + p.driveType + "  总 " +
             formatBytes(p.totalBytes) + "  可用 " + formatBytes(p.freeBytes) + "  已用 " +
             percent + (p.systemDrive ? "  [系统盘]" : ""));
    }
    line("");

    line("[必备文件]");
    for (const RequiredFile& f : report.requiredFiles) {
        line(std::string("  ") + (f.exists ? "OK   " : "缺失 ") + f.path);
    }
    line("");

    line("[克隆版检测]  判定: " + std::string(report.isLikelyClone ? "疑似克隆/已封装" : "正常"));
    for (const CloneIndicator& c : report.cloneIndicators) {
        line(std::string("  ") + (c.detected ? "[命中] " : "[正常] ") + c.name + "  (" +
             c.detail + ")");
    }
    line("");

    line("[杀软]");
    if (!report.antivirus.queried) {
        line("  无法查询: " + report.antivirus.error);
    } else if (report.antivirus.products.empty()) {
        line("  未发现已注册的杀毒产品");
    } else {
        for (const AntivirusProduct& p : report.antivirus.products) {
            line("  " + p.displayName + (p.enabled ? "  [实时防护开启]" : "  [未开启]") +
                 (p.upToDate ? "  [库最新]" : "  [库过期]"));
        }
    }
    line("");

    line("[启动项]  共 " + std::to_string(report.startupItems.size()) + " 项");
    for (const StartupItem& s : report.startupItems) {
        line("  " + s.scope + "  " + s.name + " = " + s.command);
    }

    if (!report.warnings.empty()) {
        line("");
        line("[警告]");
        for (const std::string& w : report.warnings) {
            line("  - " + w);
        }
    }

    return text;
}

} // namespace osp::sysinfo

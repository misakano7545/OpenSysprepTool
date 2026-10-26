#include "core/prepare.hpp"

#include "core/ini.hpp"
#include "core/netcfg.hpp"
#include "core/pppoe.hpp"
#include "core/tweaks.hpp"

#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <sddl.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>

#include "core/dism.hpp"
#include "core/regio.hpp"
#include "core/win7.hpp"

namespace osp::prepare {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

// ---------------------------------------------------------------- 字符串与路径

std::string trim(const std::string& text)
{
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0)
    {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
    {
        --end;
    }
    return text.substr(begin, end - begin);
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

std::string toUtf8(const std::wstring& text)
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
    std::string utf8(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr
    );
    return utf8;
}

std::string env(const char* name)
{
    const std::wstring wideName = toWide(name);
    if (wideName.empty())
    {
        return std::string();
    }
    DWORD size = GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
    if (size == 0)
    {
        return std::string();
    }
    std::wstring value(size, L'\0');
    const DWORD written = GetEnvironmentVariableW(wideName.c_str(), value.data(), size);
    if (written == 0)
    {
        return std::string();
    }
    value.resize(written);
    return toUtf8(value);
}

std::string joinPath(const std::string& root, const std::string& tail)
{
    if (root.empty())
    {
        return tail;
    }
    if (tail.empty())
    {
        return root;
    }
    if (root.back() == '\\' || root.back() == '/')
    {
        return root + tail;
    }
    return root + "\\" + tail;
}

std::string normalizeDir(std::string path)
{
    path = trim(path);
    std::replace(path.begin(), path.end(), '/', '\\');
    if (path.size() == 2 && path[1] == ':')
    {
        path += '\\';
    }
    if (!path.empty() && path.back() != '\\')
    {
        path += '\\';
    }
    return path;
}

bool hasDriveLetter(const std::string& path)
{
    return path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) != 0 && path[1] == ':';
}

std::string lowerAscii(const std::string& text)
{
    std::string out = text;
    for (char& ch : out)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

// 现场枚举可放页面文件的固定磁盘（排除可移动盘与光驱：
// 把 pagefile 放到 U 盘上会让系统在拔盘后直接起不来）。
std::vector<PagefileCandidate> enumeratePagefileCandidates(const std::string& systemDrive)
{
    std::vector<PagefileCandidate> candidates;
    const DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i)
    {
        if ((mask & (1u << i)) == 0)
        {
            continue;
        }
        const std::string drive = std::string(1, static_cast<char>('A' + i)) + ":";
        // DRIVE_FIXED 才考虑；DRIVE_REMOVABLE / DRIVE_CDROM / DRIVE_NETWORK 一律跳过
        if (GetDriveTypeW((std::wstring(drive.begin(), drive.end()) + L"\\").c_str()) != DRIVE_FIXED)
        {
            continue;
        }
        ULARGE_INTEGER freeForCaller = {};
        if (GetDiskFreeSpaceExW(
                (std::wstring(drive.begin(), drive.end()) + L"\\").c_str(), &freeForCaller, nullptr, nullptr
            ) == 0)
        {
            continue;
        }
        PagefileCandidate candidate;
        candidate.drive = drive;
        candidate.freeBytes = freeForCaller.QuadPart;
        candidates.push_back(candidate);
    }
    // 系统盘不参与「智能转移」：原本就在那里，搬一圈没意义
    const std::string system = lowerAscii(systemDrive);
    candidates.erase(
        std::remove_if(
            candidates.begin(),
            candidates.end(),
            [&system](const PagefileCandidate& item) {
                return lowerAscii(item.drive) == system;
            }
        ),
        candidates.end()
    );
    return candidates;
}

// 智能转移：选剩余空间最大的那个分区。并列时按盘符排序取前者，保证结果可复现。
// 返回空表示没有可用分区（只剩系统盘，或都被排除）。
std::string pickPagefileDrive(const std::vector<PagefileCandidate>& candidates)
{
    const PagefileCandidate* best = nullptr;
    for (const PagefileCandidate& item : candidates)
    {
        if (best == nullptr || item.freeBytes > best->freeBytes ||
            (item.freeBytes == best->freeBytes && item.drive < best->drive))
        {
            best = &item;
        }
    }
    return best == nullptr ? std::string() : best->drive;
}

fs::path pathFromUtf8(const std::string& text)
{
    const std::u8string u8(reinterpret_cast<const char8_t*>(text.data()), text.size());
    return fs::path(u8);
}

std::string toUtf8Path(const fs::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::string quoteIfNeeded(const std::string& program)
{
    if (program.empty())
    {
        return program;
    }
    if (program.front() == '"' && program.back() == '"')
    {
        return program;
    }
    if (program.find(' ') == std::string::npos && program.find('\t') == std::string::npos)
    {
        return program;
    }
    return "\"" + program + "\"";
}

std::string hexEncode(const std::vector<std::uint8_t>& bytes)
{
    static const char* digits = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (std::uint8_t byte : bytes)
    {
        text.push_back(digits[(byte >> 4) & 0x0F]);
        text.push_back(digits[byte & 0x0F]);
    }
    return text;
}

std::vector<std::uint8_t> hexDecode(const std::string& text)
{
    std::vector<std::uint8_t> bytes;
    if (text.size() % 2 != 0)
    {
        return bytes;
    }
    const auto digit = [](char ch) -> int {
        if (ch >= '0' && ch <= '9')
        {
            return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f')
        {
            return ch - 'a' + 10;
        }
        if (ch >= 'A' && ch <= 'F')
        {
            return ch - 'A' + 10;
        }
        return -1;
    };
    for (std::size_t i = 0; i + 1 < text.size(); i += 2)
    {
        const int high = digit(text[i]);
        const int low = digit(text[i + 1]);
        if (high < 0 || low < 0)
        {
            return std::vector<std::uint8_t>();
        }
        bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return bytes;
}

std::string nowText()
{
    return win7::formatFileTime(win7::fileTimeNow());
}

// 当前用户 SID 的字符串形式（S-1-5-21-...）。清空回收站要按 SID 定位子目录。
std::string currentUserSid()
{
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) == 0)
    {
        return std::string();
    }

    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (size == 0)
    {
        CloseHandle(token);
        return std::string();
    }

    std::vector<std::uint8_t> buffer(size, 0);
    if (GetTokenInformation(token, TokenUser, buffer.data(), size, &size) == 0)
    {
        CloseHandle(token);
        return std::string();
    }
    CloseHandle(token);

    auto* user = reinterpret_cast<TOKEN_USER*>(buffer.data());
    LPWSTR sidText = nullptr;
    if (ConvertSidToStringSidW(user->User.Sid, &sidText) == 0)
    {
        return std::string();
    }
    const std::string text = toUtf8(std::wstring(sidText));
    LocalFree(sidText);
    return text;
}

// ---------------------------------------------------------------- 读命令输出

// 「先读旧值再改」的场合（bcdedit 的启动菜单超时）需要捕获 stdout，
// 统一走 core/dism 的 runCommandCaptured，避免两处各写一份 CreateProcess + 管道。

// bcdedit /enum {bootmgr} 里的启动菜单超时。中文系统写作「超时」，英文写作 timeout。
bool parseBcdTimeout(const std::string& text, int& seconds)
{
    const char* keys[] = {"timeout", "Timeout", "TIMEOUT", "超时"};
    std::size_t begin = 0;
    while (begin <= text.size())
    {
        std::size_t end = text.find('\n', begin);
        if (end == std::string::npos)
        {
            end = text.size();
        }
        const std::string line = trim(text.substr(begin, end - begin));
        begin = end + 1;

        for (const char* key : keys)
        {
            const std::size_t keyLength = std::strlen(key);
            if (line.size() <= keyLength || line.compare(0, keyLength, key) != 0)
            {
                continue;
            }
            const std::string rest = trim(line.substr(keyLength));
            if (rest.empty())
            {
                continue;
            }
            bool numeric = true;
            for (char ch : rest)
            {
                if (std::isdigit(static_cast<unsigned char>(ch)) == 0)
                {
                    numeric = false;
                    break;
                }
            }
            if (numeric)
            {
                seconds = std::atoi(rest.c_str());
                return true;
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------- 动作辅助

std::string actionValueText(const Action& action)
{
    if (action.valueType == "dword")
    {
        return std::to_string(action.dword);
    }
    if (action.valueType == "qword")
    {
        return std::to_string(action.qword);
    }
    if (action.valueType == "multiSz")
    {
        std::string text;
        for (const std::string& item : action.multi)
        {
            if (!text.empty())
            {
                text += "|";
            }
            text += item;
        }
        return text;
    }
    if (action.valueType == "binary")
    {
        return action.bytesHex;
    }
    return action.text;
}

Action makeRegistryAction(
    ActionKind kind,
    std::string id,
    std::string group,
    std::string title,
    std::string sourceField,
    std::string hive,
    std::string key,
    std::string valueName,
    std::string valueType
)
{
    Action action;
    action.kind = kind;
    action.id = std::move(id);
    action.group = std::move(group);
    action.title = std::move(title);
    action.sourceField = std::move(sourceField);
    action.hive = std::move(hive);
    action.key = std::move(key);
    action.valueName = std::move(valueName);
    action.valueType = std::move(valueType);
    return action;
}

// 找「有默认网关且已连接」的网卡名（GetAdaptersAddresses 的 FriendlyName）。
// 为什么选这个判据：SC 的自动 IP 是设给正在用的那块网卡，没有网关的适配器
// （虚拟网卡、断开的无线）设了也没意义。
std::string activeAdapterName()
{
    ULONG size = 16 * 1024;
    std::vector<std::uint8_t> buffer(size);
    auto* addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    // 必须带 INCLUDE_GATEWAYS，否则 FirstGatewayAddress 恒为 nullptr，
    // 看起来「所有网卡都没有网关」——这个坑不实测会一直返回空。
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER |
        GAA_FLAG_INCLUDE_GATEWAYS;
    ULONG status = GetAdaptersAddresses(AF_INET, flags, nullptr, addresses, &size);
    if (status == ERROR_BUFFER_OVERFLOW)
    {
        buffer.resize(size);
        addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        status = GetAdaptersAddresses(AF_INET, flags, nullptr, addresses, &size);
    }
    if (status != NO_ERROR)
    {
        return std::string();
    }

    for (auto* item = addresses; item != nullptr; item = item->Next)
    {
        if (item->OperStatus != IfOperStatusUp || item->FirstGatewayAddress == nullptr)
        {
            continue;
        }
        if (item->FriendlyName != nullptr && item->FriendlyName[0] != L'\0')
        {
            return toUtf8(std::wstring(item->FriendlyName));
        }
    }
    return std::string();
}

// 读某块网卡当前的 IPv4 地址（随机模式用来决定落在哪个 /24）
std::uint32_t currentAdapterAddress(const std::string& adapterName)
{
    ULONG size = 16 * 1024;
    std::vector<std::uint8_t> buffer(size);
    auto* addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER |
        GAA_FLAG_INCLUDE_GATEWAYS;
    ULONG status = GetAdaptersAddresses(AF_INET, flags, nullptr, addresses, &size);
    if (status == ERROR_BUFFER_OVERFLOW)
    {
        buffer.resize(size);
        addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        status = GetAdaptersAddresses(AF_INET, flags, nullptr, addresses, &size);
    }
    if (status != NO_ERROR)
    {
        return 0;
    }

    for (auto* item = addresses; item != nullptr; item = item->Next)
    {
        if (item->FriendlyName == nullptr || toUtf8(std::wstring(item->FriendlyName)) != adapterName)
        {
            continue;
        }
        for (auto* unicast = item->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next)
        {
            if (unicast->Address.lpSockaddr == nullptr || unicast->Address.lpSockaddr->sa_family != AF_INET)
            {
                continue;
            }
            const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
            return ntohl(ipv4->sin_addr.s_addr);
        }
    }
    return 0;
}

// 取路径末段（兼容 / 与 \）
std::string baseName(const std::string& utf8Path)
{
    const std::size_t pos = utf8Path.find_last_of("\\/");
    return pos == std::string::npos ? utf8Path : utf8Path.substr(pos + 1);
}

// oem2.inf 排在 oem10.inf 前面（纯字典序会把 oem10 放到 oem2 之前，读起来很乱）
bool naturalLess(const std::string& left, const std::string& right)
{
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < left.size() && j < right.size())
    {
        if (std::isdigit(static_cast<unsigned char>(left[i])) &&
            std::isdigit(static_cast<unsigned char>(right[j])))
        {
            std::size_t iEnd = i;
            std::size_t jEnd = j;
            while (iEnd < left.size() && std::isdigit(static_cast<unsigned char>(left[iEnd])))
            {
                ++iEnd;
            }
            while (jEnd < right.size() && std::isdigit(static_cast<unsigned char>(right[jEnd])))
            {
                ++jEnd;
            }
            const long long a = std::stoll(left.substr(i, iEnd - i));
            const long long b = std::stoll(right.substr(j, jEnd - j));
            if (a != b)
            {
                return a < b;
            }
            i = iEnd;
            j = jEnd;
            continue;
        }
        if (left[i] != right[j])
        {
            return left[i] < right[j];
        }
        ++i;
        ++j;
    }
    return left.size() < right.size();
}

// 第三方驱动包（pnputil 的「发布名称」就是 %SystemRoot%\INF\oem*.inf）
struct DriverPackage
{
    std::string publishedName; // oem12.inf
    std::string originalName;  // 原始 inf 名（未知时为空）
    std::string provider;
    std::string className;
    std::string version;
};

// 枚举非微软的第三方驱动包。
//
// 为什么解析 INF 而不解析 `pnputil /enum-drivers` 的输出：那个输出是**本地化**的，
// 中文系统写着「发布名称:」、英文系统写着 "Published Name:"，CI 机器是 1252 的
// 英文系统，靠标签匹配必然在某一侧失灵。oemNN.inf 的文件名与 [Version] 段里的
// Provider/Class/DriverVer 是语言无关的，而且我们已经有 INI 解析器。
std::vector<DriverPackage> enumerateThirdPartyDrivers(std::vector<std::string>& skipped)
{
    std::vector<DriverPackage> packages;
    const std::string infDir =
        joinPath(env("SystemRoot").empty() ? std::string("C:\\Windows") : env("SystemRoot"), "INF");

    WIN32_FIND_DATAW data = {};
    const std::wstring pattern = osp::win7::pathFromUtf8(joinPath(infDir, "oem*.inf")).wstring();
    HANDLE handle = ::FindFirstFileW(pattern.c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return packages;
    }
    do
    {
        const std::string name = toUtf8(std::wstring(data.cFileName));
        const osp::ini::Document document = osp::ini::loadFile(joinPath(infDir, name));
        if (!document.ok)
        {
            continue; // INF 解析不出来就跳过，不阻断整份计划
        }
        // INF 里的 Provider 常写成 %ManufacturerName%，真名在 [Strings] 段里。
        // 不展开的话微软自家包也会显示成 %ManufacturerName%，靠它过滤 Microsoft 会失灵。
        const auto expand = [&document](std::string text) {
            const std::string::size_type begin = text.find('%');
            const std::string::size_type end = text.rfind('%');
            if (begin == std::string::npos || end == begin || end - begin < 2)
            {
                return text;
            }
            const std::string key = text.substr(begin + 1, end - begin - 1);
            const std::string resolved = document.get("Strings", key);
            return resolved.empty() ? text : resolved;
        };

        DriverPackage package;
        package.publishedName = name;
        package.provider = expand(document.get("Version", "Provider"));
        package.className = document.get("Version", "Class");
        package.version = document.get("Version", "DriverVer");
        // 微软自带的（in-box）包不卸：卸了会把系统驱动弄坏
        if (package.provider.find("Microsoft") != std::string::npos)
        {
            continue;
        }
        // 存储控制器/磁盘类绝不能卸：删掉之后目标机直接起不来（0x7B 蓝屏）。
        // 这一类必须留给 sysprep 的 PnP 重新识别，不能为了「干净」把引导搭进去。
        static const char* const kNeverUninstall[] = {
            "SCSIAdapter",
            "HDC",
            "Volume",
            "DiskDrive",
            "CDROM",
            "FloppyDisk",
            "StorageVolume",
        };
        bool storageClass = false;
        for (const char* blocked : kNeverUninstall)
        {
            if (package.className == blocked)
            {
                storageClass = true;
                break;
            }
        }
        if (storageClass)
        {
            skipped.push_back(
                package.publishedName + "（" + package.className + "，存储类，卸载会导致无法引导）"
            );
            continue;
        }
        packages.push_back(std::move(package));
    } while (::FindNextFileW(handle, &data));
    ::FindClose(handle);

    std::sort(packages.begin(), packages.end(), [](const DriverPackage& a, const DriverPackage& b) {
        return naturalLess(a.publishedName, b.publishedName);
    });
    return packages;
}

// 文件是否存在。用 GetFileAttributesW 而不是 std::filesystem::exists：
// 后者在权限不足时返回 false，会把「读不到」说成「不存在」。
bool fileExists(const std::string& utf8Path)
{
    const std::filesystem::path path = osp::win7::pathFromUtf8(utf8Path);
    return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// 当前用户的 IE 主页。SC 的「同步封装前 IE 主页」就是把它搬到默认配置里去。
std::string currentUserIeHomePage()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(
            HKEY_CURRENT_USER, L"Software\\Microsoft\\Internet Explorer\\Main", 0, KEY_READ, &key
        ) != ERROR_SUCCESS)
    {
        return std::string();
    }
    wchar_t buffer[2048] = {};
    DWORD bytes = sizeof(buffer);
    DWORD type = 0;
    std::string result;
    if (RegGetValueW(key, nullptr, L"Start Page", RRF_RT_REG_SZ, &type, buffer, &bytes) == ERROR_SUCCESS)
    {
        result = toUtf8(std::wstring(buffer));
    }
    RegCloseKey(key);
    return result;
}

regio::Value valueOf(const Action& action)
{
    regio::Value value;
    std::string parseError;
    regio::parseValueType(action.valueType, value.type, parseError);
    if (value.type == regio::ValueType::Dword)
    {
        value.dword = action.dword;
    }
    else if (value.type == regio::ValueType::Qword)
    {
        value.qword = action.qword;
    }
    else if (value.type == regio::ValueType::MultiString)
    {
        value.multi = action.multi;
    }
    else if (value.type == regio::ValueType::Binary)
    {
        value.bytes = hexDecode(action.bytesHex);
    }
    else
    {
        value.text = action.text;
    }
    return value;
}

JournalEntry journalOfValue(const Action& action, const regio::Value& previous, bool existed)
{
    JournalEntry entry;
    entry.actionId = action.id;
    entry.kind = "registry";
    entry.hive = action.hive;
    entry.key = action.key;
    entry.valueName = action.valueName;
    entry.existed = existed;
    if (!existed)
    {
        return entry;
    }
    entry.valueType = regio::valueTypeId(previous.type);
    entry.text = previous.text;
    entry.dword = previous.dword;
    entry.qword = previous.qword;
    entry.multi = previous.multi;
    entry.bytesHex = hexEncode(previous.bytes);
    return entry;
}

bool registrySet(const Action& action, ExecResult& result, std::string& error)
{
    regio::Hive hive;
    if (!regio::parseHive(action.hive, hive, error))
    {
        return false;
    }
    regio::Key key = regio::create(hive, action.key, error);
    if (!key.valid())
    {
        return false;
    }

    regio::Value previous;
    bool existed = false;
    if (!regio::readValue(key, action.valueName, previous, existed, error))
    {
        return false;
    }
    result.journal.entries.push_back(journalOfValue(action, previous, existed));

    if (!regio::writeValue(key, action.valueName, valueOf(action), error))
    {
        return false;
    }

    result.log.push_back(
        "RegSetValueEx  " + action.hive + "\\" + action.key + "  \"" + action.valueName + "\" (" +
        action.valueType + ") = " + actionValueText(action)
    );
    return true;
}

bool registryDelete(const Action& action, ExecResult& result, std::string& error)
{
    regio::Hive hive;
    if (!regio::parseHive(action.hive, hive, error))
    {
        return false;
    }
    regio::Key key = regio::open(hive, action.key, true, error);
    if (!key.valid())
    {
        // 键不存在 = 值也不存在，删掉是幂等的
        if (error.find("GetLastError=2") != std::string::npos)
        {
            error.clear();
            result.log.push_back("RegDeleteValue  " + action.hive + "\\" + action.key + "（键不存在，跳过）");
            return true;
        }
        return false;
    }

    regio::Value previous;
    bool existed = false;
    if (!regio::readValue(key, action.valueName, previous, existed, error))
    {
        return false;
    }
    result.journal.entries.push_back(journalOfValue(action, previous, existed));
    if (!regio::deleteValue(key, action.valueName, error))
    {
        return false;
    }
    result.log.push_back(
        "RegDeleteValue  " + action.hive + "\\" + action.key + "  \"" + action.valueName + "\""
    );
    return true;
}

bool runCommand(const Action& action, ExecResult& result, std::string& error)
{
    std::string commandLine = quoteIfNeeded(action.program);
    if (!action.arguments.empty())
    {
        commandLine += " " + action.arguments;
    }

    std::uint32_t code = 0;
    if (!dism::runCommand(commandLine, code, error))
    {
        return false;
    }
    result.log.push_back("执行  " + commandLine + "  → 退出码 " + std::to_string(code));
    if (code != 0)
    {
        if (!action.tolerateNonZeroExit)
        {
            error = "命令退出码非 0（" + std::to_string(code) + "）：" + commandLine;
            return false;
        }
        result.warnings.push_back("命令退出码 " + std::to_string(code) + "（已容忍）：" + commandLine);
    }

    if (!action.undoCommand.empty())
    {
        JournalEntry entry;
        entry.actionId = action.id;
        entry.kind = "command";
        entry.undoCommand = action.undoCommand;
        result.journal.entries.push_back(entry);
    }
    return true;
}

bool createDirectory(const Action& action, ExecResult& result, std::string& error)
{
    const fs::path path = pathFromUtf8(action.path);
    std::error_code ec;
    if (fs::exists(path, ec))
    {
        if (!fs::is_directory(path, ec))
        {
            error = "目标已存在且不是目录：" + action.path;
            return false;
        }
        result.log.push_back("目录已存在  " + action.path);
        return true;
    }

    fs::create_directories(path, ec);
    if (ec)
    {
        error = "创建目录失败（" + action.path + "）：" + ec.message();
        return false;
    }
    JournalEntry entry;
    entry.actionId = action.id;
    entry.kind = "createDir";
    entry.path = action.path;
    result.journal.entries.push_back(entry);
    result.log.push_back("创建目录  " + action.path);
    return true;
}

bool copyFileIfMissing(
    const fs::path& source,
    const fs::path& target,
    const Action& action,
    ExecResult& result,
    std::string& error
)
{
    std::error_code ec;
    if (fs::exists(target, ec))
    {
        result.warnings.push_back("目标已存在，跳过：" + toUtf8Path(target));
        return true;
    }
    fs::copy_file(source, target, fs::copy_options::none, ec);
    if (ec)
    {
        error = "复制失败（" + toUtf8Path(source) + " → " + toUtf8Path(target) + "）：" + ec.message();
        return false;
    }
    JournalEntry entry;
    entry.actionId = action.id;
    entry.kind = "copy";
    entry.path = toUtf8Path(target);
    result.journal.entries.push_back(entry);
    return true;
}

bool copyTree(const Action& action, ExecResult& result, std::string& error)
{
    const fs::path source = pathFromUtf8(action.path);
    const fs::path destination = pathFromUtf8(action.destination);

    std::error_code ec;
    if (!fs::exists(source, ec))
    {
        result.warnings.push_back("源目录不存在，跳过：" + action.path);
        return true;
    }
    if (!fs::is_directory(source, ec))
    {
        error = "源不是目录：" + action.path;
        return false;
    }

    if (!fs::exists(destination, ec))
    {
        fs::create_directories(destination, ec);
        if (ec)
        {
            error = "创建目标目录失败（" + action.destination + "）：" + ec.message();
            return false;
        }
        JournalEntry entry;
        entry.actionId = action.id;
        entry.kind = "createDir";
        entry.path = action.destination;
        result.journal.entries.push_back(entry);
    }

    std::size_t copied = 0;
    fs::recursive_directory_iterator it(source, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec))
    {
        if (ec)
        {
            result.warnings.push_back("遍历源目录时跳过：" + ec.message());
            ec.clear();
            continue;
        }
        const fs::path relative = fs::relative(it->path(), source, ec);
        if (ec)
        {
            ec.clear();
            continue;
        }
        const fs::path target = destination / relative;
        if (it->is_directory(ec))
        {
            if (!fs::exists(target, ec))
            {
                fs::create_directories(target, ec);
                if (ec)
                {
                    error = "创建子目录失败（" + toUtf8Path(target) + "）：" + ec.message();
                    return false;
                }
                JournalEntry entry;
                entry.actionId = action.id;
                entry.kind = "createDir";
                entry.path = toUtf8Path(target);
                result.journal.entries.push_back(entry);
            }
        }
        else if (it->is_regular_file(ec))
        {
            if (!copyFileIfMissing(it->path(), target, action, result, error))
            {
                return false;
            }
            ++copied;
        }
    }

    result.log.push_back(
        "复制目录  " + action.path + " → " + action.destination + "（" + std::to_string(copied) + " 个文件）"
    );
    return true;
}

bool movePath(const fs::path& from, const fs::path& to, std::string& error)
{
    std::error_code ec;
    fs::rename(from, to, ec);
    if (!ec)
    {
        return true;
    }

    // 跨卷 rename 会失败（ERROR_NOT_SAME_DEVICE）：退化为复制后删除
    ec.clear();
    if (fs::is_directory(from, ec))
    {
        fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
        if (ec)
        {
            error = "跨卷复制失败（" + toUtf8Path(from) + " → " + toUtf8Path(to) + "）：" + ec.message();
            return false;
        }
        fs::remove_all(from, ec);
    }
    else
    {
        fs::copy_file(from, to, fs::copy_options::none, ec);
        if (ec)
        {
            error = "跨卷复制失败（" + toUtf8Path(from) + " → " + toUtf8Path(to) + "）：" + ec.message();
            return false;
        }
        fs::remove(from, ec);
    }
    if (ec)
    {
        error = "复制后删除源失败（" + toUtf8Path(from) + "）：" + ec.message();
        return false;
    }
    return true;
}

bool moveTree(const Action& action, ExecResult& result, std::string& error)
{
    const fs::path source = pathFromUtf8(action.path);
    const fs::path destination = pathFromUtf8(action.destination);

    std::error_code ec;
    if (!fs::exists(source, ec))
    {
        result.warnings.push_back("源目录不存在，跳过：" + action.path);
        return true;
    }
    if (!fs::is_directory(source, ec))
    {
        error = "源不是目录：" + action.path;
        return false;
    }
    if (!fs::exists(destination, ec))
    {
        fs::create_directories(destination, ec);
        if (ec)
        {
            error = "创建目标目录失败（" + action.destination + "）：" + ec.message();
            return false;
        }
        JournalEntry entry;
        entry.actionId = action.id;
        entry.kind = "createDir";
        entry.path = action.destination;
        result.journal.entries.push_back(entry);
    }

    std::size_t moved = 0;
    fs::directory_iterator it(source, fs::directory_options::skip_permission_denied, ec);
    const fs::directory_iterator end;
    for (; it != end; it.increment(ec))
    {
        if (ec)
        {
            result.warnings.push_back("遍历源目录时跳过：" + ec.message());
            ec.clear();
            continue;
        }
        const fs::path target = destination / it->path().filename();
        if (fs::exists(target, ec))
        {
            result.warnings.push_back("目标已存在，跳过：" + toUtf8Path(target));
            continue;
        }
        if (!movePath(it->path(), target, error))
        {
            return false;
        }
        JournalEntry entry;
        entry.actionId = action.id;
        entry.kind = "move";
        entry.from = toUtf8Path(it->path());
        entry.to = toUtf8Path(target);
        result.journal.entries.push_back(entry);
        ++moved;
    }

    result.log.push_back(
        "移动目录内容  " + action.path + " → " + action.destination + "（" + std::to_string(moved) +
        " 个条目）"
    );
    return true;
}

// 单个文件复制（任务栏固定的快捷方式走这条）
bool copyOneFile(const Action& action, ExecResult& result, std::string& error)
{
    const std::filesystem::path source = osp::win7::pathFromUtf8(action.path);
    const std::filesystem::path target = osp::win7::pathFromUtf8(action.destination);
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
    {
        error = "复制失败：" + action.path + " → " + action.destination + "（" + ec.message() + "）";
        return false;
    }
    result.log.push_back(action.id + "：完成");
    return true;
}

// 单个文件删除。文件本来就不在算成功——「取消固定」的目标就是它不在。
bool deleteOneFile(const Action& action, ExecResult& result, std::string& error)
{
    const std::filesystem::path target = osp::win7::pathFromUtf8(action.path);
    if (::GetFileAttributesW(target.wstring().c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        return true;
    }
    std::error_code ec;
    if (!std::filesystem::remove(target, ec))
    {
        error = "删除失败：" + action.path + (ec ? "（" + ec.message() + "）" : "");
        return false;
    }
    result.log.push_back(action.id + "：完成");
    return true;
}

// 单个文件复制（任务栏固定的快捷方式走这条）
// 静默创建宽带（PPPoE）连接：让 Windows 写进临时 pbk，再复制到默认用户配置目录。
//
// 为什么要经过临时文件：RasSetEntryProperties 支持指定 pbk 路径，但目标目录
// （默认用户的 Network\Connections\Pbk）在封装前不一定已被系统创建好；
// 先写到 %TEMP% 里、确认读得回来，再复制过去，任何一步失败都能明确报出来，
// 不会留下一个「看起来有、其实连不上」的半成品。
bool pppoeCreate(const Action& action, ExecResult& result, std::string& error)
{
    const std::filesystem::path target = osp::win7::pathFromUtf8(action.destination);
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "ospt-pppoe";
    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);
    const std::string tempPbk = osp::win7::toUtf8Path(tempDir / "probe.pbk");
    std::filesystem::remove(osp::win7::pathFromUtf8(tempPbk), ec);

    std::string createError;
    if (!osp::netcfg::createPppoeEntry(
            tempPbk, action.connName, action.connUser, action.connPassword, createError
        ))
    {
        error = createError;
        std::filesystem::remove_all(tempDir, ec);
        return false;
    }
    if (!createError.empty())
    {
        // 条目建好但凭据没写进去：如实告警，不当成成功掩盖过去
        result.warnings.push_back(action.id + "：" + createError);
    }

    // 读回确认 Windows 真的接受了这个条目（写失败却不报错是 RAS 的常见坑）
    std::string readUser;
    std::string readError;
    if (!osp::netcfg::readPppoeEntry(tempPbk, action.connName, readUser, readError))
    {
        error = "创建后读不回宽带连接：" + readError;
        std::filesystem::remove_all(tempDir, ec);
        return false;
    }
    // 账号密码是否真的落进了电话簿，只能读回来才知道：RasSetCredentials 会返回 0
    // 却没写进指定的 pbk（实测如此）。所以这里核对一次，没写进去就如实告警，
    // 不要让用户以为「已经存好密码了」。
    if (!action.connUser.empty() && readUser != action.connUser)
    {
        result.warnings.push_back(
            action.id + "：账号密码未能写入电话簿（Windows 未持久化），首次连接时需要用户自己填"
        );
    }

    std::error_code copyError;
    std::filesystem::create_directories(target.parent_path(), copyError);
    std::filesystem::copy_file(
        osp::win7::pathFromUtf8(tempPbk), target, std::filesystem::copy_options::overwrite_existing, copyError
    );
    if (copyError)
    {
        error = "复制宽带连接配置失败：" + action.destination + "（" + copyError.message() + "）";
        return false;
    }
    // 临时 pbk 用完就删；删不掉不影响结果，忽略即可
    std::error_code cleanupError;
    std::filesystem::remove_all(tempDir, cleanupError);
    result.log.push_back(action.id + "：完成");
    return true;
}

bool deleteTreeContents(const Action& action, ExecResult& result, std::string& error)
{
    const fs::path root = pathFromUtf8(action.path);
    std::error_code ec;
    if (!fs::exists(root, ec))
    {
        result.warnings.push_back("路径不存在，跳过：" + action.path);
        return true;
    }

    // 文件：直接删（残留的 unattend.xml 这类单文件清理走同一条路径）
    if (fs::is_regular_file(root, ec))
    {
        fs::remove(root, ec);
        if (ec)
        {
            error = "删除文件失败（" + action.path + "）：" + ec.message();
            return false;
        }
        result.log.push_back("删除文件  " + action.path);
        return true;
    }

    if (!fs::is_directory(root, ec))
    {
        error = "既不是文件也不是目录：" + action.path;
        return false;
    }

    std::size_t removed = 0;
    std::size_t failed = 0;
    fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    const fs::directory_iterator end;
    for (; it != end; it.increment(ec))
    {
        if (ec)
        {
            result.warnings.push_back("遍历目录时跳过：" + ec.message());
            ec.clear();
            continue;
        }
        std::error_code removeError;
        fs::remove_all(it->path(), removeError);
        if (removeError)
        {
            ++failed;
            result.warnings.push_back("删除失败（" + toUtf8Path(it->path()) + "）：" + removeError.message());
        }
        else
        {
            ++removed;
        }
    }

    result.log.push_back(
        "清空目录  " + action.path + "（删除 " + std::to_string(removed) + " 个条目，失败 " +
        std::to_string(failed) + " 个）"
    );
    return true;
}

bool registryDeleteKey(const Action& action, ExecResult& result, std::string& error)
{
    regio::Hive hive;
    if (!regio::parseHive(action.hive, hive, error))
    {
        return false;
    }
    if (!regio::deleteTree(hive, action.key, error))
    {
        return false;
    }
    result.log.push_back("RegDeleteTree  " + action.hive + "\\" + action.key);
    return true;
}

bool bcdTimeoutSet(const Action& action, ExecResult& result, std::string& error)
{
    if (action.program.empty())
    {
        error = "没有 bcdedit.exe 的路径";
        return false;
    }

    const std::string bcd = quoteIfNeeded(action.program);
    std::uint32_t enumCode = 0;
    std::string enumOutput;
    std::string enumError;
    const bool enumOk = dism::runCommandCaptured(bcd + " /enum {bootmgr}", enumCode, enumOutput, enumError);
    int oldSeconds = -1;
    const bool haveOld = enumOk && parseBcdTimeout(enumOutput, oldSeconds);

    std::uint32_t code = 0;
    const std::string commandLine = bcd + " /set {bootmgr} timeout " + std::to_string(action.dword);
    if (!dism::runCommand(commandLine, code, error))
    {
        return false;
    }
    result.log.push_back("执行  " + commandLine + "  → 退出码 " + std::to_string(code));
    if (code != 0)
    {
        error = "bcdedit 退出码非 0（" + std::to_string(code) + "）：" + commandLine;
        return false;
    }

    if (haveOld)
    {
        JournalEntry entry;
        entry.actionId = action.id;
        entry.kind = "command";
        entry.undoCommand = bcd + " /set {bootmgr} timeout " + std::to_string(oldSeconds);
        result.journal.entries.push_back(entry);
        result.log.push_back("原启动菜单超时 = " + std::to_string(oldSeconds) + " 秒，已记入回滚日志");
    }
    else
    {
        result.warnings.push_back("读不到原来的启动菜单超时（bcdedit /enum 解析失败），回滚需要手工执行 "
                                  "bcdedit /set {bootmgr} timeout <原值>");
    }
    return true;
}

// 写进离线挂载的用户 hive（Users\Default\NTUSER.DAT）。
// 流程：Load -> 建键 -> 读旧值进 journal -> 写 -> Unload。
// 失败时也必须卸载：LoadedHive 的析构会做，这里再显式调一次以拿到错误信息。

// 回读校验用：写入的值与期望一致吗
bool matchesExpected(const Action& action, const regio::Value& value, bool exists)
{
    if (!exists)
    {
        return false;
    }
    switch (value.type)
    {
    case regio::ValueType::Dword:
        return value.dword == action.dword;
    case regio::ValueType::Qword:
        return value.qword == action.qword;
    case regio::ValueType::String:
    case regio::ValueType::ExpandString:
        return value.text == action.text;
    case regio::ValueType::MultiString:
        return value.multi == action.multi;
    case regio::ValueType::Binary:
        return true; // 二进制比较留给调用方，这里不重复实现
    }
    return false;
}

bool hiveSet(const Action& action, ExecResult& result, std::string& error)
{
    if (action.hiveFile.empty())
    {
        error = action.id + "：hiveFile 为空";
        return false;
    }

    regio::LoadedHive hive;
    std::string loadError;
    if (!hive.load(action.hiveFile, action.hiveName, loadError))
    {
        error = action.id + "：" + loadError;
        return false;
    }

    bool ok = true;
    {
        // 子键可能还不存在（新用户的默认配置里 User Shell Folders 一般有，但不能假设）
        std::string keyError;
        regio::Key key = regio::openUserHiveKey(action.hiveName, action.key, true, keyError);
        if (!key.valid())
        {
            if (!regio::createUserHiveKey(action.hiveName, action.key, key, keyError))
            {
                error = action.id + "：建键失败 " + action.key + " —— " + keyError;
                ok = false;
            }
        }

        if (ok && key.valid())
        {
            regio::Value previous;
            bool existed = false;
            if (!regio::readValue(key, action.valueName, previous, existed, error))
            {
                ok = false;
            }
            else
            {
                JournalEntry entry = journalOfValue(action, previous, existed);
                entry.kind = "hiveRegistry";
                entry.hiveFile = action.hiveFile;
                entry.hiveName = action.hiveName;
                result.journal.entries.push_back(entry);

                if (!regio::writeValue(key, action.valueName, valueOf(action), error))
                {
                    if (action.tolerateFailure)
                    {
                        result.warnings.push_back(
                            action.id + "：写入被系统拒绝，已跳过（" + action.key + "\\" + action.valueName +
                            "）：" + error
                        );
                        error.clear();
                    }
                    else
                    {
                        ok = false;
                    }
                }
                else
                {
                    // 写后回读：某些系统键（如 IE 的 Main）带拒绝写入的 ACL，
                    // 会出现「写入返回成功但值根本没进去」的静默失败（实测 reg.exe
                    // 在这种键上就返回 0 却查不到值）。不校验就等于假装做成了。
                    regio::Value readBack;
                    bool existsAfter = false;
                    std::string readError;
                    const bool verified =
                        regio::readValue(key, action.valueName, readBack, existsAfter, readError) &&
                        matchesExpected(action, readBack, existsAfter);
                    if (!verified)
                    {
                        if (action.tolerateFailure)
                        {
                            error.clear();
                        }
                        result.warnings.push_back(
                            action.id + "：写入后回读不一致（该键可能被系统保护，拒绝写入）：" + action.key +
                            "\\" + action.valueName + "（" + readError + "）"
                        );
                    }
                    else
                    {
                        result.log.push_back(
                            "HiveSet  " + action.hiveFile + "  [" + action.key + "] \"" + action.valueName +
                            "\" (" + action.valueType + ") = " + actionValueText(action)
                        );
                    }
                }
            }
        }
    }

    std::string unloadError;
    if (!hive.unload(unloadError))
    {
        result.warnings.push_back(unloadError);
    }
    return ok;
}

bool executeOne(const Action& action, ExecResult& result, std::string& error)
{
    switch (action.kind)
    {
    case ActionKind::RegistrySet:
        return registrySet(action, result, error);
    case ActionKind::RegistryDelete:
        return registryDelete(action, result, error);
    case ActionKind::RunCommand:
        return runCommand(action, result, error);
    case ActionKind::MakeDirectory:
        return createDirectory(action, result, error);
    case ActionKind::CopyTree:
        return copyTree(action, result, error);
    case ActionKind::MoveTree:
        return moveTree(action, result, error);
    case ActionKind::DeleteTree:
        return deleteTreeContents(action, result, error);
    case ActionKind::BcdTimeout:
        return bcdTimeoutSet(action, result, error);
    case ActionKind::RegistryDeleteKey:
        return registryDeleteKey(action, result, error);
    case ActionKind::FileCopy:
        return copyOneFile(action, result, error);
    case ActionKind::FileDelete:
        return deleteOneFile(action, result, error);
    case ActionKind::PppoeCreate:
        return pppoeCreate(action, result, error);
    case ActionKind::HiveSet:
        return hiveSet(action, result, error);
    }
    error = "未知的动作类型";
    return false;
}

// ---------------------------------------------------------------- 回滚辅助

regio::Value valueOfEntry(const JournalEntry& entry)
{
    regio::Value value;
    std::string error;
    regio::parseValueType(entry.valueType, value.type, error);
    value.text = entry.text;
    value.dword = entry.dword;
    value.qword = entry.qword;
    value.multi = entry.multi;
    value.bytes = hexDecode(entry.bytesHex);
    return value;
}

bool rollbackOne(const JournalEntry& entry, bool dryRun, ExecResult& result, std::string& error)
{
    if (entry.kind == "registry")
    {
        regio::Hive hive;
        if (!regio::parseHive(entry.hive, hive, error))
        {
            return false;
        }
        if (dryRun)
        {
            result.log.push_back(
                entry.existed
                    ? "将还原注册表值  " + entry.hive + "\\" + entry.key + "  \"" + entry.valueName + "\""
                    : "将删除注册表值  " + entry.hive + "\\" + entry.key + "  \"" + entry.valueName + "\""
            );
            return true;
        }
        regio::Key key = regio::create(hive, entry.key, error);
        if (!key.valid())
        {
            return false;
        }
        if (entry.existed)
        {
            if (!regio::writeValue(key, entry.valueName, valueOfEntry(entry), error))
            {
                return false;
            }
            result.log.push_back(
                "还原注册表值  " + entry.hive + "\\" + entry.key + "  \"" + entry.valueName + "\""
            );
        }
        else
        {
            if (!regio::deleteValue(key, entry.valueName, error))
            {
                return false;
            }
            result.log.push_back(
                "删除注册表值  " + entry.hive + "\\" + entry.key + "  \"" + entry.valueName + "\""
            );
        }
        return true;
    }

    if (entry.kind == "command")
    {
        if (entry.undoCommand.empty())
        {
            result.warnings.push_back("回滚条目没有撤销命令：" + entry.actionId);
            return true;
        }
        if (dryRun)
        {
            result.log.push_back("将执行撤销命令  " + entry.undoCommand);
            return true;
        }
        std::uint32_t code = 0;
        if (!dism::runCommand(entry.undoCommand, code, error))
        {
            return false;
        }
        result.log.push_back("撤销命令  " + entry.undoCommand + "  → 退出码 " + std::to_string(code));
        if (code != 0)
        {
            result.warnings.push_back("撤销命令退出码非 0：" + entry.undoCommand);
        }
        return true;
    }

    if (entry.kind == "move")
    {
        if (dryRun)
        {
            result.log.push_back("将移回  " + entry.to + " → " + entry.from);
            return true;
        }
        const fs::path from = pathFromUtf8(entry.from);
        const fs::path to = pathFromUtf8(entry.to);
        std::error_code ec;
        if (!fs::exists(to, ec))
        {
            result.warnings.push_back("回滚时源已不存在：" + entry.to);
            return true;
        }
        if (fs::exists(from, ec))
        {
            result.warnings.push_back("回滚目标已存在，跳过：" + entry.from);
            return true;
        }
        if (!movePath(to, from, error))
        {
            return false;
        }
        result.log.push_back("移回  " + entry.to + " → " + entry.from);
        return true;
    }

    if (entry.kind == "copy" || entry.kind == "createDir")
    {
        if (dryRun)
        {
            result.log.push_back("将删除  " + entry.path);
            return true;
        }
        const fs::path path = pathFromUtf8(entry.path);
        std::error_code ec;
        if (!fs::exists(path, ec))
        {
            return true;
        }
        if (entry.kind == "createDir")
        {
            fs::remove(path, ec); // 只删空目录
        }
        else
        {
            fs::remove(path, ec);
        }
        if (ec)
        {
            result.warnings.push_back("回滚删除失败（" + entry.path + "）：" + ec.message());
            return true;
        }
        result.log.push_back("删除  " + entry.path);
        return true;
    }

    result.warnings.push_back("未知的回滚条目类型：" + entry.kind);
    return true;
}

} // namespace

// ---------------------------------------------------------------- 动作类型

const char* actionKindId(ActionKind kind)
{
    switch (kind)
    {
    case ActionKind::RegistrySet:
        return "registrySet";
    case ActionKind::RegistryDelete:
        return "registryDelete";
    case ActionKind::RunCommand:
        return "runCommand";
    case ActionKind::MakeDirectory:
        return "createDirectory";
    case ActionKind::CopyTree:
        return "copyTree";
    case ActionKind::MoveTree:
        return "moveTree";
    case ActionKind::DeleteTree:
        return "deleteTree";
    case ActionKind::BcdTimeout:
        return "bcdTimeout";
    case ActionKind::RegistryDeleteKey:
        return "registryDeleteKey";
    case ActionKind::HiveSet:
        return "hiveSet";
    case ActionKind::FileCopy:
        return "fileCopy";
    case ActionKind::FileDelete:
        return "fileDelete";
    case ActionKind::PppoeCreate:
        return "pppoeCreate";
    }
    return "unknown";
}

// ---------------------------------------------------------------- 选项

Options defaultOptions()
{
    Options options;
    options.systemDrive = normalizeDir(env("SystemDrive"));
    options.userProfile = env("USERPROFILE");
    options.publicProfile = env("PUBLIC");
    options.windowsDir = env("SystemRoot");
    options.tempDir = env("TEMP");
    options.defaultUserProfile = joinPath(options.systemDrive, "Users\\Default");
    return options;
}

// ---------------------------------------------------------------- 构建计划

Plan build(const config::Config& cfg, const Options& options)
{
    Plan plan;
    plan.ok = true;

    Options opt = options;
    const Options defaults = defaultOptions();
    if (opt.systemDrive.empty())
    {
        opt.systemDrive = defaults.systemDrive;
    }
    if (opt.userProfile.empty())
    {
        opt.userProfile = defaults.userProfile;
    }
    if (opt.publicProfile.empty())
    {
        opt.publicProfile = defaults.publicProfile;
    }
    if (opt.windowsDir.empty())
    {
        opt.windowsDir = defaults.windowsDir;
    }
    if (opt.tempDir.empty())
    {
        opt.tempDir = defaults.tempDir;
    }
    if (opt.defaultUserProfile.empty())
    {
        opt.defaultUserProfile = joinPath(normalizeDir(opt.systemDrive), "Users\\Default");
    }
    opt.systemDrive = normalizeDir(opt.systemDrive);
    opt.windowsDir = normalizeDir(opt.windowsDir);

    if (opt.systemDrive.empty())
    {
        plan.ok = false;
        plan.error = "无法确定系统盘路径（%SystemDrive% 为空）";
        return plan;
    }

    const auto defer = [&plan](std::string field, std::string reason) {
        Deferred item;
        item.sourceField = std::move(field);
        item.reason = std::move(reason);
        plan.deferred.push_back(std::move(item));
    };

    // ------------------------------------------------------------ 系统信息
    // 启动菜单等待时间是 BCD 的 {bootmgr} timeout，不是 unattend 元素 ——
    // 早期版本只把它存进 cfg.json，界面上填了也不生效。0 = 不修改。
    if (cfg.systemInfo.bootMenuTimeoutSec > 0)
    {
        if (opt.windowsDir.empty())
        {
            plan.warnings.push_back("无法确定 %SystemRoot%，已跳过设置启动菜单时间");
        }
        else
        {
            Action action;
            action.kind = ActionKind::BcdTimeout;
            action.id = "systemInfo.bootMenuTimeoutSec";
            action.group = "系统信息";
            action.title = "设置启动菜单等待时间（BCD {bootmgr} timeout）";
            action.sourceField = "systemInfo.bootMenuTimeoutSec";
            action.program = joinPath(opt.windowsDir, "System32\\bcdedit.exe");
            action.dword = static_cast<std::uint32_t>(cfg.systemInfo.bootMenuTimeoutSec);
            action.requiresReboot = true;
            action.note = "改的是引导菜单的等待秒数；执行时先读原值，回滚能写回去";
            plan.actions.push_back(std::move(action));
        }
    }

    // ------------------------------------------------------------ 系统优化
    if (cfg.optimization.closeNumLock)
    {
        Action action = makeRegistryAction(
            ActionKind::RegistrySet,
            "optimization.closeNumLock",
            "系统优化",
            "关闭笔记本小键盘（登录时 NumLock 默认关闭）",
            "optimization.closeNumLock",
            "HKU\\.DEFAULT",
            "Control Panel\\Keyboard",
            "InitialKeyboardIndicators",
            "sz"
        );
        action.text = "2";
        action.note = "2 = 登录时 NumLock 关闭；写在 .DEFAULT 上，对所有新用户生效";
        plan.actions.push_back(std::move(action));
    }

    if (cfg.optimization.enableHibernate)
    {
        if (opt.windowsDir.empty())
        {
            plan.warnings.push_back("无法确定 %SystemRoot%，已跳过开启休眠");
        }
        else
        {
            Action action;
            action.kind = ActionKind::RunCommand;
            action.id = "optimization.enableHibernate";
            action.group = "系统优化";
            action.title = "开启系统休眠";
            action.sourceField = "optimization.enableHibernate";
            action.program = joinPath(opt.windowsDir, "System32\\powercfg.exe");
            action.arguments = "/hibernate on";
            action.undoCommand =
                quoteIfNeeded(joinPath(opt.windowsDir, "System32\\powercfg.exe")) + " /hibernate off";
            action.note = "开启后会创建 hiberfil.sys；不需要时可在系统里再关掉（回滚会替你关）";
            plan.actions.push_back(std::move(action));
        }
    }

    if (cfg.optimization.closeRebootDialog)
    {
        const std::string key = "SOFTWARE\\Policies\\Microsoft\\Windows NT\\Reliability";

        Action on = makeRegistryAction(
            ActionKind::RegistrySet,
            "optimization.closeRebootDialog.reasonOn",
            "系统优化",
            "关闭关机事件跟踪（不再要求填写原因）",
            "optimization.closeRebootDialog",
            "HKLM",
            key,
            "ShutdownReasonOn",
            "dword"
        );
        on.dword = 0;
        on.note = "策略键，对所有用户生效";
        plan.actions.push_back(std::move(on));

        Action ui = makeRegistryAction(
            ActionKind::RegistrySet,
            "optimization.closeRebootDialog.reasonUi",
            "系统优化",
            "关闭关机事件跟踪界面",
            "optimization.closeRebootDialog",
            "HKLM",
            key,
            "ShutdownReasonUI",
            "dword"
        );
        ui.dword = 0;
        plan.actions.push_back(std::move(ui));
    }

    if (!cfg.optimization.pageFileTarget.empty() || cfg.optimization.autoPagefileDrive)
    {
        // SC 的两个单选：Rb_DiyMovePageFile（指定分区）与 Rb_SmartMovePageFile
        // （智能选剩余最大的分区）。两者同时给时以「指定」为准，并提示一句 ——
        // 静默二选一最容易让人觉得配置没生效。
        std::string rawTarget = cfg.optimization.pageFileTarget;
        if (!rawTarget.empty() && cfg.optimization.autoPagefileDrive)
        {
            plan.warnings.push_back(
                "optimization.pageFileTarget 与 optimization.autoPagefileDrive 同时开启，"
                "按指定分区处理：" +
                rawTarget
            );
        }
        if (rawTarget.empty())
        {
            const std::vector<PagefileCandidate> candidates = options.pagefileCandidates.empty()
                ? enumeratePagefileCandidates(options.systemDrive)
                : options.pagefileCandidates;
            const std::string picked = pickPagefileDrive(candidates);
            if (picked.empty())
            {
                plan.warnings.push_back(
                    "智能转移虚拟内存：除系统盘外没有可用的固定磁盘，改为系统盘由系统管理大小"
                );
                rawTarget = options.systemDrive;
            }
            else
            {
                rawTarget = picked;
            }
        }

        const std::string target = normalizeDir(rawTarget);
        if (!hasDriveLetter(target))
        {
            plan.warnings.push_back("虚拟内存目标不是盘符路径，已跳过：" + rawTarget);
        }
        else
        {
            Action action = makeRegistryAction(
                ActionKind::RegistrySet,
                cfg.optimization.autoPagefileDrive && cfg.optimization.pageFileTarget.empty()
                    ? "optimization.autoPagefileDrive"
                    : "optimization.pageFileTarget",
                "系统优化",
                cfg.optimization.autoPagefileDrive && cfg.optimization.pageFileTarget.empty()
                    ? "智能转移虚拟内存到剩余空间最大的分区"
                    : "把虚拟内存放到指定分区",
                cfg.optimization.autoPagefileDrive && cfg.optimization.pageFileTarget.empty()
                    ? "optimization.autoPagefileDrive"
                    : "optimization.pageFileTarget",
                "HKLM",
                "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management",
                "PagingFiles",
                "multiSz"
            );
            // 只搬位置，大小仍交给系统管理：SC 的 MovePageFile 也只做这件事，
            // 手写具体 MB 数反而会在不同内存的机器上变成新问题。
            action.multi = {target + "pagefile.sys 0 0"};
            action.requiresReboot = true;
            action.note = "0 0 = 由系统管理大小；重启后生效";
            plan.actions.push_back(std::move(action));
        }
    }

    if (!cfg.optimization.sysTweaks.empty())
    {
        // SC 的系统优化项：数据表在 core/tweaks，这里只负责翻译成动作。
        // 未知 id 不静默丢弃 —— 写进 warnings，让人一眼看出配置里的名字打错了。
        for (const std::string& id : cfg.optimization.sysTweaks)
        {
            const tweaks::Tweak* tweak = tweaks::find(id);
            if (tweak == nullptr)
            {
                plan.warnings.push_back("optimization.sysTweaks 里有未知优化项，已跳过：" + id);
                continue;
            }

            Action action = makeRegistryAction(
                ActionKind::RegistrySet,
                tweak->id,
                "系统优化（SC 优化项）",
                tweak->title,
                "optimization.sysTweaks",
                tweak->hive == tweaks::Hive::CurrentUser ? "HKCU" : "HKLM",
                tweak->path,
                tweak->name,
                tweak->type == tweaks::ValueType::Integer
                    ? "dword"
                    : (tweak->type == tweaks::ValueType::String ? "sz" : "binary")
            );

            if (tweak->type == tweaks::ValueType::Integer)
            {
                action.dword = static_cast<std::uint32_t>(std::stoul(tweak->value));
            }
            else if (tweak->type == tweaks::ValueType::String)
            {
                action.text = tweak->value;
            }
            else
            {
                // SC 表里的 binary 值是 "00000000" 这种以字节为单位的十六进制串，
                // 而 Action::bytesHex 按字节对存，所以原样搬即可。
                action.bytesHex = tweak->value;
            }
            action.requiresReboot = tweak->requiresReboot;
            if (!tweak->scLabel.empty())
            {
                action.note = "SC " + tweak->scLabel;
            }
            plan.actions.push_back(std::move(action));
        }

        // 语言文件里有描述、但没有原始数据的那几条：如实报「尚未实现」，
        // 不假装做了（见 core/tweaks 的 unresolvedLabels）。
        std::string tail;
        for (const std::string& label : tweaks::unresolvedLabels())
        {
            tail += (tail.empty() ? "" : "、") + label;
        }
        if (!tail.empty())
        {
            plan.warnings.push_back("SC 优化表里有以下项缺少原始数据，尚未实现：" + tail);
        }
    }

    if (cfg.optimization.pinTaskbarItems)
    {
        defer("optimization.pinTaskbarItems", "需要调用 Shell 的固定动词并处理用户配置文件，M9 不做");
    }
    // 我的文档 / IE 收藏夹 / IE 主页：都要写进「所有新用户」的默认配置，
    // 落点是 <系统盘>\Users\Default\NTUSER.DAT，必须离线挂载后写。
    const std::string defaultHive = cfg.optimization.defaultUserHive.empty()
        ? joinPath(opt.defaultUserProfile, "NTUSER.DAT")
        : cfg.optimization.defaultUserHive;
    const bool defaultHiveExists = fileExists(defaultHive);

    if (cfg.optimization.moveDocuments || !cfg.optimization.myDocumentsTarget.empty())
    {
        std::string target = cfg.optimization.myDocumentsTarget;
        if (cfg.optimization.documentsSmart || target.empty())
        {
            // 智能转移：SC 的说明是「自动扫描目标系统最大剩余容量的分区，条件不成立则不转移」
            const std::vector<PagefileCandidate> candidates = options.pagefileCandidates.empty()
                ? enumeratePagefileCandidates(options.systemDrive)
                : options.pagefileCandidates;
            const std::string picked = pickPagefileDrive(candidates);
            if (picked.empty())
            {
                plan.warnings.push_back("智能转移我的文档：除系统盘外没有可用固定磁盘，按 SC 的规矩不转移");
                target.clear();
            }
            else
            {
                target = picked + "\\My Documents";
            }
        }

        if (!target.empty())
        {
            // 找不到文件时仍然出动作：dry-run 要看得见意图，真实执行由 hiveSet 报清楚的错。
            // 静默跳过会让人以为「配置没生效是因为没这项功能」。
            if (!defaultHiveExists)
            {
                plan.warnings.push_back("找不到默认用户配置 " + defaultHive + "，我的文档位置在执行时会失败");
            }
            {
                Action action;
                action.kind = ActionKind::HiveSet;
                action.id = "optimization.myDocumentsTarget";
                action.group = "系统优化";
                action.title = "转移我的文档并让所有新用户继承";
                action.sourceField = cfg.optimization.documentsSmart ? "optimization.documentsSmart"
                                                                     : "optimization.myDocumentsTarget";
                action.hiveFile = defaultHive;
                action.key = "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders";
                action.valueName = "Personal";
                action.valueType = "expandSz";
                action.text = target;
                action.note = "离线写 " + defaultHive + "；已存在的用户配置不受影响";
                plan.actions.push_back(std::move(action));
            }
        }
    }

    if (cfg.optimization.moveFavorites)
    {
        std::string target = cfg.optimization.favoritesTarget;
        if (cfg.optimization.favoritesSmart || target.empty())
        {
            const std::vector<PagefileCandidate> candidates = options.pagefileCandidates.empty()
                ? enumeratePagefileCandidates(options.systemDrive)
                : options.pagefileCandidates;
            const std::string picked = pickPagefileDrive(candidates);
            if (picked.empty())
            {
                plan.warnings.push_back("智能转移 IE 收藏夹：除系统盘外没有可用固定磁盘，按 SC 的规矩不转移");
                target.clear();
            }
            else
            {
                target = picked + "\\Favorites";
            }
        }

        if (!target.empty())
        {
            if (!defaultHiveExists)
            {
                plan.warnings.push_back("找不到默认用户配置 " + defaultHive + "，收藏夹位置在执行时会失败");
            }
            {
                Action action;
                action.kind = ActionKind::HiveSet;
                action.id = "optimization.moveFavorites";
                action.group = "系统优化";
                action.title = "转移 IE 收藏夹并让所有新用户继承";
                action.sourceField = cfg.optimization.favoritesSmart ? "optimization.favoritesSmart"
                                                                     : "optimization.favoritesTarget";
                action.hiveFile = defaultHive;
                action.key = "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders";
                action.valueName = "Favorites";
                action.valueType = "expandSz";
                action.text = target;
                action.note = "离线写 " + defaultHive + "；已存在的用户配置不受影响";
                plan.actions.push_back(std::move(action));
            }
        }
    }

    if (cfg.optimization.saveIeHomePage)
    {
        // SC 的语义是「同步封装前 IE 主页网址」：把**当前用户**的主页写进默认配置，
        // 这样新用户进桌面看到的还是封装前那个主页。没显式给就用当前用户的。
        std::string homePage = cfg.optimization.ieHomePage;
        if (homePage.empty())
        {
            homePage = currentUserIeHomePage();
        }
        if (homePage.empty())
        {
            plan.warnings.push_back(
                "同步 IE 主页：读不到当前用户的 IE 主页，跳过（可用 optimization.ieHomePage 显式指定）"
            );
        }
        else
        {
            if (!defaultHiveExists)
            {
                plan.warnings.push_back("找不到默认用户配置 " + defaultHive + "，IE 主页在执行时会失败");
            }
            Action action;
            action.kind = ActionKind::HiveSet;
            action.id = "optimization.saveIeHomePage";
            action.group = "首次进桌面";
            action.title = "同步封装前 IE 主页到所有新用户";
            action.sourceField = "optimization.saveIeHomePage";
            action.hiveFile = defaultHive;
            action.key = "Software\\Microsoft\\Internet Explorer\\Main";
            action.valueName = "Start Page";
            action.valueType = "sz";
            action.text = homePage;
            // Win10/11 上这个键带拒绝写入的 ACL，写不进去只告警、不中断封装
            action.tolerateFailure = true;
            action.note = "起始页 = " + homePage +
                "；若被系统键保护拒绝，可在运行计划→登录时 增加一条 reg add 作为替代";
            plan.actions.push_back(std::move(action));
        }
    }
    if (cfg.optimization.autoSetIp)
    {
        // SC 的三种模式：固定 IP / 随机 IP / IP 段
        const std::string mode = cfg.optimization.ipMode.empty() ? "fixed" : cfg.optimization.ipMode;
        const std::string ifName = options.ipInterface.empty() ? activeAdapterName() : options.ipInterface;

        if (ifName.empty())
        {
            plan.warnings.push_back("自动设置 IP：找不到可用的网卡（没有带网关的已连接适配器），已跳过");
        }
        else if (mode == "fixed" || mode == "random" || mode == "range")
        {
            std::string address = cfg.optimization.ipAddress;
            std::uint32_t chosen = 0;

            if (mode == "fixed")
            {
                std::uint32_t parsed = 0;
                if (!netcfg::parseIpv4(address, parsed))
                {
                    plan.warnings.push_back("自动设置 IP：指定 IP 不是合法 IPv4，已跳过：" + address);
                    address.clear();
                }
            }
            else if (mode == "range" || mode == "random")
            {
                std::uint32_t startIp = 0;
                std::uint32_t endIp = 0;
                const bool hasRange = netcfg::parseIpv4(cfg.optimization.ipRangeStart, startIp) &&
                    netcfg::parseIpv4(cfg.optimization.ipRangeEnd, endIp);

                if (mode == "range" && !hasRange)
                {
                    plan.warnings.push_back(
                        "自动设置 IP：IP 段模式的起始/结束 IP 不合法，已跳过：" +
                        cfg.optimization.ipRangeStart + " - " + cfg.optimization.ipRangeEnd
                    );
                }
                else if (hasRange)
                {
                    chosen = netcfg::pickInRange(startIp, endIp, options.ipRandomSeed);
                    address = netcfg::formatIpv4(chosen);
                }
                else
                {
                    // 随机模式没有给网段：在本机当前地址的同一 /24 里取，保证网关仍可达。
                    // 这是本实现的取舍，SC 的原始算法未在产物中体现，已在文档中注明。
                    std::uint32_t current = currentAdapterAddress(ifName);
                    if (current == 0)
                    {
                        plan.warnings.push_back(
                            "自动设置 IP：随机模式既没给 IP 段，也读不到本机当前地址，已跳过"
                        );
                        address.clear();
                    }
                    else
                    {
                        chosen = netcfg::pickSameSubnet24(current, options.ipRandomSeed);
                        address = netcfg::formatIpv4(chosen);
                    }
                }
            }

            if (!address.empty())
            {
                const std::string mask =
                    cfg.optimization.subnetMask.empty() ? "255.255.255.0" : cfg.optimization.subnetMask;
                std::uint32_t maskValue = 0;
                if (!netcfg::parseIpv4(mask, maskValue))
                {
                    plan.warnings.push_back("自动设置 IP：子网掩码不合法，已跳过：" + mask);
                }
                else
                {
                    std::uint32_t addressValue = 0;
                    netcfg::parseIpv4(address, addressValue);
                    if (!cfg.optimization.gateway.empty())
                    {
                        std::uint32_t gatewayValue = 0;
                        if (!netcfg::parseIpv4(cfg.optimization.gateway, gatewayValue))
                        {
                            plan.warnings.push_back(
                                "自动设置 IP：默认网关不合法，忽略：" + cfg.optimization.gateway
                            );
                        }
                        else if ((gatewayValue & maskValue) != (addressValue & maskValue))
                        {
                            // 随机/网段模式很容易撞到这种情况：地址落在 A 网段、网关填的是 B 网段，
                            // 执行完就直接断网。这不是可以用「用户自己知道」带过的错误。
                            plan.warnings.push_back(
                                "自动设置 IP：地址 " + address + " 与网关 " + cfg.optimization.gateway +
                                " 不在同一子网（掩码 " + mask + "），执行后可能直接断网，请核对配置"
                            );
                        }
                    }

                    Action addressAction;
                    addressAction.kind = ActionKind::RunCommand;
                    addressAction.id = "optimization.autoSetIp";
                    addressAction.group = "系统优化";
                    addressAction.title = "设置静态 IP（" + mode + "）";
                    addressAction.sourceField = "optimization.autoSetIp";
                    addressAction.program = "netsh";
                    addressAction.arguments =
                        "interface ipv4 set address name=\"" + ifName + "\" static " + address + " " + mask;
                    if (!cfg.optimization.gateway.empty())
                    {
                        addressAction.arguments += " " + cfg.optimization.gateway;
                    }
                    addressAction.note = "网卡：" + ifName + "；地址 " + address + "/" + mask +
                        (cfg.optimization.gateway.empty() ? "" : " 网关 " + cfg.optimization.gateway);
                    plan.actions.push_back(std::move(addressAction));

                    if (!cfg.optimization.dnsPreferred.empty())
                    {
                        Action dnsAction;
                        dnsAction.kind = ActionKind::RunCommand;
                        dnsAction.id = "optimization.autoSetIp.dns";
                        dnsAction.group = "系统优化";
                        dnsAction.title = "设置首选 DNS";
                        dnsAction.sourceField = "optimization.dnsPreferred";
                        dnsAction.program = "netsh";
                        dnsAction.arguments = "interface ipv4 set dns name=\"" + ifName + "\" static " +
                            cfg.optimization.dnsPreferred;
                        plan.actions.push_back(std::move(dnsAction));

                        if (!cfg.optimization.dnsAlternate.empty())
                        {
                            Action altAction;
                            altAction.kind = ActionKind::RunCommand;
                            altAction.id = "optimization.autoSetIp.dnsAlt";
                            altAction.group = "系统优化";
                            altAction.title = "设置备用 DNS";
                            altAction.sourceField = "optimization.dnsAlternate";
                            altAction.program = "netsh";
                            altAction.arguments = "interface ipv4 add dns name=\"" + ifName + "\" " +
                                cfg.optimization.dnsAlternate + " index=2";
                            plan.actions.push_back(std::move(altAction));
                        }
                    }
                }
            }
        }
        else
        {
            plan.warnings.push_back(
                "自动设置 IP：未知模式 \"" + mode + "\"（允许 fixed/random/range），已跳过"
            );
        }
    }
    if (cfg.optimization.clearSrsDrivers)
    {
        defer(
            "optimization.clearSrsDrivers",
            "需要枚举 Services 与 CriticalDeviceDatabase 并排除存储控制器，归入驱动模块"
        );
    }
    if (cfg.optimization.setResolution)
    {
        defer(
            "optimization.setResolution",
            "首登分辨率已由 unattend 的 Display 组件处理；智能检测当前分辨率未实现"
        );
    }

    // ------------------------------------------------------------ 隐私痕迹
    // SC 在封装前会清一百多条痕迹。这里只做位置确定、清掉不影响功能的那批：
    // 地址栏历史、运行对话框、最近文档、文件对话框历史、托盘缓存、压缩软件路径历史。
    // 删的是整个键，值集合没法逐条还原，所以标为不可回滚。

    // ------------------------------------------------------------ 首次进桌面
    if (cfg.firstLogon.syncIeFavorites)
    {
        if (opt.userProfile.empty() || opt.defaultUserProfile.empty())
        {
            plan.warnings.push_back("无法确定用户配置目录，已跳过同步 IE 收藏夹");
        }
        else
        {
            Action action;
            action.kind = ActionKind::CopyTree;
            action.id = "firstLogon.syncIeFavorites";
            action.group = "首次进桌面";
            action.title = "同步 IE 收藏夹到默认用户配置";
            action.sourceField = "firstLogon.syncIeFavorites";
            action.path = joinPath(opt.userProfile, "Favorites");
            action.destination = joinPath(opt.defaultUserProfile, "Favorites");
            action.note = "把当前用户的收藏夹复制到默认用户；已存在的同名文件不覆盖";
            plan.actions.push_back(std::move(action));
        }
    }

    if (cfg.firstLogon.syncIeHomepage)
    {
        defer(
            "firstLogon.syncIeHomepage",
            "要写到所有新用户必须离线加载 " + joinPath(opt.defaultUserProfile, "NTUSER.DAT") + "；归入 M10"
        );
    }
    if (cfg.firstLogon.rebootAfterRun)
    {
        defer(
            "firstLogon.rebootAfterRun",
            "需要在 FirstLogonCommands 末尾追加 shutdown 命令，M9 未改 unattend 生成器"
        );
    }

    // ------------------------------------------------------------ 部署设置
    if (cfg.deploy.enableAdministrator)
    {
        if (opt.windowsDir.empty())
        {
            plan.warnings.push_back("无法确定 %SystemRoot%，已跳过启用 Administrator");
        }
        else
        {
            Action action;
            action.kind = ActionKind::RunCommand;
            action.id = "deploy.enableAdministrator";
            action.group = "部署设置";
            action.title = "启用内置 Administrator 账户";
            action.sourceField = "deploy.enableAdministrator";
            action.program = joinPath(opt.windowsDir, "System32\\net.exe");
            action.arguments = "user Administrator /active:yes";
            action.note = "只启用账户，不设置密码；账户状态不写入回滚日志";
            plan.actions.push_back(std::move(action));
        }
    }

    if (cfg.deploy.blockVirtualMachine)
    {
        defer("deploy.blockVirtualMachine", "由部署运行时 osptd 在目标机上判定，封装前不适用");
    }
    // ---------------------------------------------------------- 宽带连接（SC 的 Cb_CreateConnection）
    // 静默模式（Rb_Silence_nc）：把连接建在**默认用户**的电话簿里，新用户登录即有，
    // 全程无界面。展示模式（Rb_Show_nc）：改成在首次进桌面时弹创建向导，见 unattend。
    if (cfg.firstLogon.createConnection)
    {
        const std::string mode =
            cfg.firstLogon.connectionMode.empty() ? "show" : cfg.firstLogon.connectionMode;
        if (mode == "silent")
        {
            if (cfg.firstLogon.connectionName.empty())
            {
                plan.warnings.push_back("宽带连接：连接名为空，已跳过");
            }
            else
            {
                Action action;
                action.kind = ActionKind::PppoeCreate;
                action.id = "firstLogon.createConnection";
                action.group = "首次进桌面";
                action.title = "静默创建宽带连接";
                action.sourceField = "firstLogon.createConnection";
                action.connName = cfg.firstLogon.connectionName;
                action.connUser = cfg.firstLogon.connectionUser;
                action.connPassword = cfg.firstLogon.connectionPassword;
                action.destination = joinPath(
                    options.defaultUserProfile,
                    "AppData\\Roaming\\Microsoft\\Network\\Connections\\Pbk\\rasphone.pbk"
                );
                action.note = "写进默认用户的电话簿，新用户登录即有该连接；不执行拨号";
                plan.actions.push_back(std::move(action));
            }
        }
        else if (mode != "show")
        {
            plan.warnings.push_back("宽带连接：未知模式 \"" + mode + "\"（允许 silent/show），已跳过");
        }
    }

    // ---------------------------------------------------------- 任务栏固定 / 取消固定（SC 的 Cb_Locktaskbar）
    // 落点是「默认用户」的 User Pinned\TaskBar：新用户建配置目录时由系统复制过去。
    // 只写用户自己列出来的文件，绝不预置任何推广快捷方式（宪章 §7）。
    const std::string pinnedDir = joinPath(
        options.defaultUserProfile,
        "AppData\\Roaming\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar"
    );
    for (const std::string& file : cfg.optimization.lockTaskbarFiles)
    {
        const std::string name = baseName(file);
        if (file.empty() || name.empty())
        {
            plan.warnings.push_back("任务栏固定：条目为空，已跳过");
            continue;
        }
        if (!fileExists(file))
        {
            plan.warnings.push_back("任务栏固定：源文件不存在，已跳过：" + file);
            continue;
        }
        Action action;
        action.kind = ActionKind::FileCopy;
        action.id = "optimization.lockTaskbar." + name;
        action.group = "系统优化";
        action.title = "固定到任务栏";
        action.sourceField = "optimization.lockTaskbarFiles";
        action.path = file;
        action.destination = joinPath(pinnedDir, name);
        action.note = "写入默认用户的任务栏固定目录，新用户登录即可见";
        plan.actions.push_back(std::move(action));
    }
    for (const std::string& file : cfg.optimization.unlockTaskbarFiles)
    {
        const std::string name = baseName(file);
        if (name.empty())
        {
            plan.warnings.push_back("任务栏取消固定：条目为空，已跳过");
            continue;
        }
        Action action;
        action.kind = ActionKind::FileDelete;
        action.id = "optimization.unlockTaskbar." + name;
        action.group = "系统优化";
        action.title = "从任务栏取消固定";
        action.sourceField = "optimization.unlockTaskbarFiles";
        action.path = joinPath(pinnedDir, name);
        action.note = "只删默认用户里的同名快捷方式，本机其他用户不受影响";
        plan.actions.push_back(std::move(action));
    }

    // ---------------------------------------------------------- 卸载本机设备驱动（SC 的 Cb_Uninstalldriver）
    // 目的：把「参考机」上装过的第三方驱动从映像里清掉，避免带到别的机器。
    // 只列非微软的第三方驱动包，微软自带的一律不碰。
    if (cfg.sysprep.uninstallDrivers)
    {
        std::vector<std::string> skippedDrivers;
        std::vector<DriverPackage> packages = enumerateThirdPartyDrivers(skippedDrivers);
        for (const std::string& skipped : skippedDrivers)
        {
            plan.warnings.push_back("卸载本机设备驱动：跳过 " + skipped);
        }
        if (packages.empty())
        {
            plan.warnings.push_back("卸载本机设备驱动：没有枚举出第三方驱动包（可能 pnputil 不可用）");
        }
        for (const DriverPackage& package : packages)
        {
            if (package.publishedName.empty())
            {
                continue;
            }
            Action action;
            action.kind = ActionKind::RunCommand;
            action.id = "sysprep.uninstallDriver." + package.publishedName;
            action.group = "封装清理";
            action.title = "卸载第三方驱动包";
            action.sourceField = "sysprep.uninstallDrivers";
            action.program = "pnputil";
            action.arguments = "/delete-driver " + package.publishedName + " /uninstall /force";
            action.note = package.provider + " / " + package.className + " / " + package.version;
            action.irreversible = true; // 驱动包删掉就回不来了
            plan.actions.push_back(std::move(action));
        }
    }

    if (cfg.deploy.verifyIntegrity && cfg.deploy.tamperFiles.empty())
    {
        // 防篡改本身在部署端（osptd 的部署前闸门）执行，封装前不适用；
        // 但「开了校验却没给清单」是配置错误，必须在这里就点出来。
        plan.warnings.push_back("deploy.verifyIntegrity 已开启，但 tamperFiles 为空 —— 部署时将拒绝执行");
    }

    // ------------------------------------------------------------ 封装清理
    if (cfg.sysprep.transferDesktopFiles)
    {
        if (opt.userProfile.empty() || opt.publicProfile.empty())
        {
            plan.warnings.push_back("无法确定桌面目录，已跳过转移桌面文件");
        }
        else
        {
            Action action;
            action.kind = ActionKind::MoveTree;
            action.id = "sysprep.transferDesktopFiles";
            action.group = "封装清理";
            action.title = "把当前用户桌面文件转移到公共桌面";
            action.sourceField = "sysprep.transferDesktopFiles";
            action.path = joinPath(opt.userProfile, "Desktop");
            action.destination = joinPath(opt.publicProfile, "Desktop");
            action.note = "目标已存在同名项时跳过；移动记录写入回滚日志";
            plan.actions.push_back(std::move(action));
        }
    }

    if (cfg.sysprep.cleanTemp)
    {
        if (!opt.tempDir.empty())
        {
            Action userTemp;
            userTemp.kind = ActionKind::DeleteTree;
            userTemp.id = "sysprep.cleanTemp.user";
            userTemp.group = "封装清理";
            userTemp.title = "清空当前用户临时目录";
            userTemp.sourceField = "sysprep.cleanTemp";
            userTemp.path = opt.tempDir;
            userTemp.irreversible = true;
            userTemp.note = "占用中的文件会跳过；删除内容不可恢复";
            plan.actions.push_back(std::move(userTemp));
        }
        else
        {
            plan.warnings.push_back("无法确定 %TEMP%，已跳过清理用户临时目录");
        }

        if (!opt.windowsDir.empty())
        {
            Action windowsTemp;
            windowsTemp.kind = ActionKind::DeleteTree;
            windowsTemp.id = "sysprep.cleanTemp.windows";
            windowsTemp.group = "封装清理";
            windowsTemp.title = "清空 Windows 临时目录";
            windowsTemp.sourceField = "sysprep.cleanTemp";
            windowsTemp.path = joinPath(opt.windowsDir, "Temp");
            windowsTemp.irreversible = true;
            windowsTemp.note = "占用中的文件会跳过；删除内容不可恢复";
            plan.actions.push_back(std::move(windowsTemp));
        }
        else
        {
            plan.warnings.push_back("无法确定 %SystemRoot%，已跳过清理 Windows 临时目录");
        }
    }

    if (cfg.sysprep.cleanDllCache)
    {
        if (opt.windowsDir.empty())
        {
            plan.warnings.push_back("无法确定 %SystemRoot%，已跳过清理 DLLCache");
        }
        else
        {
            Action action;
            action.kind = ActionKind::RunCommand;
            action.id = "sysprep.cleanDllCache";
            action.group = "封装清理";
            action.title = "清理 DLLCache（sfc /purgecache）";
            action.sourceField = "sysprep.cleanDllCache";
            action.program = joinPath(opt.windowsDir, "System32\\sfc.exe");
            action.arguments = "/purgecache";
            action.irreversible = true;
            action.note = "清除 WinSxS 的缓存副本，系统需要时会从安装源还原";
            plan.actions.push_back(std::move(action));
        }
    }

    // ------------------------------------------------------------ 防流氓清理
    // 对标产品的「防流氓封装方式」：把我们逆向出来的推广残留清掉。
    // 只清有确凿证据的落地物（见 ES5P 报告 §9 的产物清单），不猜别的路径。
    if (cfg.sysprep.mode == "antiRogue")
    {
        Action softInst;
        softInst.kind = ActionKind::DeleteTree;
        softInst.id = "sysprep.antiRogue.softInst";
        softInst.group = "防流氓清理";
        softInst.title = "清空 SoftInst 推广载荷目录";
        softInst.sourceField = "sysprep.mode";
        softInst.path = joinPath(opt.systemDrive, "SoftInst");
        softInst.irreversible = true;
        softInst.note = "ES5P 用它放 wget/inst.cmd/inst.vbs 静默装推广软件；不存在时跳过";
        plan.actions.push_back(std::move(softInst));

        for (const char* hive : {"HKLM", "HKCU"})
        {
            Action runOnce = makeRegistryAction(
                ActionKind::RegistryDelete,
                std::string("sysprep.antiRogue.runOnce.") + (std::string(hive) == "HKLM" ? "hklm" : "hkcu"),
                "防流氓清理",
                std::string("删除 RunOnce 里的推广启动项（") + hive + "）",
                "sysprep.mode",
                hive,
                "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce",
                "inst.vbs",
                "sz"
            );
            runOnce.note = "ES5P 在 Deploy 阶段无条件注册它，SSP 开关关掉也会跑一次";
            plan.actions.push_back(std::move(runOnce));
        }
    }
    else if (cfg.sysprep.mode == "volume")
    {
        plan.warnings.push_back("封装方式 volume（大客户高级）：反编译里只找到它与「每服务器授权」同现，"
                                "sysprep 参数与 classic 相同，本次按 classic 执行");
    }

    // ------------------------------------------------------------ 封装前清理
    // 对齐 SysprepPreparator 的 PreparationTasks（docs/开源参考调研.md §2.2）。
    // 这些动作都会改系统状态且大多不可回滚，逐项默认关闭，必须显式勾选。
    const auto addCleanup = [&plan,
                             &opt](ActionKind kind, const char* id, const char* title, const char* field) {
        Action action;
        action.kind = kind;
        action.id = id;
        action.group = "封装前清理";
        action.title = title;
        action.sourceField = field;
        action.irreversible = true;
        return action;
    };

    if (cfg.sysprep.loadSrsDrivers)
    {
        defer("sysprep.loadSrsDrivers", "由 --inject-drivers --srs 处理（M6 已实现），不在封装前设置里重复");
    }
    if (cfg.sysprep.perServerLicensing)
    {
        defer("sysprep.perServerLicensing", "unattend 生成器尚未写 Licensing 组件，M9 不落地");
    }

    if (plan.actions.empty() && plan.deferred.empty())
    {
        plan.warnings.push_back("配置里没有需要封装前执行的设置");
    }

    return plan;
}

// ---------------------------------------------------------------- 报告

std::string dryRunReport(const Plan& plan)
{
    std::string out;
    out += "封装前设置：" + std::to_string(plan.actions.size()) + " 个动作\n";

    std::string group;
    for (const Action& action : plan.actions)
    {
        if (action.group != group)
        {
            group = action.group;
            out += "\n[" + group + "]\n";
        }
        out += "  " + action.id + "  " + action.title + "\n";

        switch (action.kind)
        {
        case ActionKind::RegistrySet:
        case ActionKind::RegistryDelete:
            out += "    " +
                std::string(action.kind == ActionKind::RegistrySet ? "RegSetValueEx" : "RegDeleteValue") +
                "  " + action.hive + "\\" + action.key + "  \"" + action.valueName + "\"";
            if (action.kind == ActionKind::RegistrySet)
            {
                out += " (" + action.valueType + ") = " + actionValueText(action);
            }
            out += "\n";
            break;
        case ActionKind::RunCommand:
            out += "    执行  " + quoteIfNeeded(action.program);
            if (!action.arguments.empty())
            {
                out += " " + action.arguments;
            }
            out += "\n";
            break;
        case ActionKind::MakeDirectory:
            out += "    创建目录  " + action.path + "\n";
            break;
        case ActionKind::CopyTree:
            out += "    复制目录  " + action.path + " → " + action.destination + "\n";
            break;
        case ActionKind::MoveTree:
            out += "    移动目录内容  " + action.path + " → " + action.destination + "\n";
            break;
        case ActionKind::DeleteTree:
            out += "    删除路径  " + action.path + "（文件直接删，目录清空内容）\n";
            break;
        case ActionKind::FileCopy:
            out += "    复制文件  " + action.path + " → " + action.destination + "\n";
            break;
        case ActionKind::FileDelete:
            out += "    删除文件  " + action.path + "（不存在算成功）\n";
            break;
        case ActionKind::BcdTimeout:
            out += "    执行  " + quoteIfNeeded(action.program) + " /set {bootmgr} timeout " +
                std::to_string(action.dword) + "（执行时读取原值写入回滚日志）\n";
            break;
        case ActionKind::RegistryDeleteKey:
            out += "    RegDeleteTree  " + action.hive + "\\" + action.key + "\n";
            break;
        case ActionKind::HiveSet:
            out += "    离线写 hive  " + action.hiveFile + "  [HKU\\" + action.hiveName + "\\" + action.key +
                "] \"" + action.valueName + "\" (" + action.valueType + ") = " + actionValueText(action) +
                "\n";
            break;
        }

        if (action.requiresReboot)
        {
            out += "    需要重启后生效\n";
        }
        if (action.irreversible)
        {
            out += "    不可回滚\n";
        }
        if (!action.note.empty())
        {
            out += "    说明：" + action.note + "\n";
        }
    }

    if (!plan.deferred.empty())
    {
        out += "\n[本里程碑不执行]\n";
        for (const Deferred& item : plan.deferred)
        {
            out += "  " + item.sourceField + " —— " + item.reason + "\n";
        }
    }

    if (!plan.warnings.empty())
    {
        out += "\n[警告]\n";
        for (const std::string& warning : plan.warnings)
        {
            out += "  " + warning + "\n";
        }
    }
    if (!plan.blockers.empty())
    {
        out += "\n[阻塞项]\n";
        for (const std::string& blocker : plan.blockers)
        {
            out += "  " + blocker + "\n";
        }
    }

    out += "\nRESULT: DRY-RUN OK  （" + std::to_string(plan.actions.size()) + " 个动作，" +
        std::to_string(plan.deferred.size()) + " 项推迟）\n";
    return out;
}

std::string toJson(const Plan& plan)
{
    json root;
    root["schemaVersion"] = 1;
    root["generatedAt"] = nowText();
    root["ok"] = plan.ok;
    if (!plan.error.empty())
    {
        root["error"] = plan.error;
    }
    root["actionCount"] = plan.actions.size();

    json actions = json::array();
    for (const Action& action : plan.actions)
    {
        json item;
        item["id"] = action.id;
        item["group"] = action.group;
        item["title"] = action.title;
        item["sourceField"] = action.sourceField;
        item["kind"] = actionKindId(action.kind);
        item["requiresReboot"] = action.requiresReboot;
        item["irreversible"] = action.irreversible;
        // 允许写入失败降级的动作要显式标出来，验收脚本据此判断
        // （IE 主页在 Win10+ 的键带拒绝写入 ACL）
        item["tolerateFailure"] = action.tolerateFailure;
        item["note"] = action.note;

        switch (action.kind)
        {
        case ActionKind::RegistrySet:
        case ActionKind::RegistryDelete:
        case ActionKind::RegistryDeleteKey:
            item["hive"] = action.hive;
            item["key"] = action.key;
            item["valueName"] = action.valueName;
            item["valueType"] = action.valueType;
            item["value"] = actionValueText(action);
            break;
        case ActionKind::RunCommand:
            item["program"] = action.program;
            item["arguments"] = action.arguments;
            break;
        case ActionKind::MakeDirectory:
        case ActionKind::DeleteTree:
            item["path"] = action.path;
            break;
        case ActionKind::FileCopy:
            item["path"] = action.path;
            item["destination"] = action.destination;
            break;
        case ActionKind::FileDelete:
            item["path"] = action.path;
            break;
        case ActionKind::PppoeCreate:
            item["path"] = action.destination;
            item["connectionName"] = action.connName;
            break;
        case ActionKind::CopyTree:
        case ActionKind::MoveTree:
            item["path"] = action.path;
            item["destination"] = action.destination;
            break;
        case ActionKind::HiveSet:
            item["hiveFile"] = action.hiveFile;
            item["hiveName"] = action.hiveName;
            item["key"] = action.key;
            item["valueName"] = action.valueName;
            item["valueType"] = action.valueType;
            item["value"] = actionValueText(action);
            break;
        case ActionKind::BcdTimeout:
            item["program"] = action.program;
            item["seconds"] = action.dword;
            break;
        }
        actions.push_back(std::move(item));
    }
    root["actions"] = std::move(actions);

    json deferred = json::array();
    for (const Deferred& item : plan.deferred)
    {
        deferred.push_back({{"sourceField", item.sourceField}, {"reason", item.reason}});
    }
    root["deferred"] = std::move(deferred);
    root["warnings"] = plan.warnings;
    root["blockers"] = plan.blockers;
    return root.dump(2) + "\n";
}

// ---------------------------------------------------------------- 回滚日志

std::string journalToJson(const Journal& journal)
{
    json root;
    root["schemaVersion"] = journal.schemaVersion;
    root["createdAt"] = journal.createdAt;

    json entries = json::array();
    for (const JournalEntry& entry : journal.entries)
    {
        json item;
        item["actionId"] = entry.actionId;
        item["kind"] = entry.kind;
        if (entry.kind == "registry")
        {
            item["hive"] = entry.hive;
            item["key"] = entry.key;
            item["valueName"] = entry.valueName;
            item["existed"] = entry.existed;
            if (entry.existed)
            {
                item["valueType"] = entry.valueType;
                if (entry.valueType == "dword")
                {
                    item["dword"] = entry.dword;
                }
                else if (entry.valueType == "qword")
                {
                    item["qword"] = entry.qword;
                }
                else if (entry.valueType == "multiSz")
                {
                    item["multi"] = entry.multi;
                }
                else if (entry.valueType == "binary")
                {
                    item["bytesHex"] = entry.bytesHex;
                }
                else
                {
                    item["text"] = entry.text;
                }
            }
        }
        else if (entry.kind == "move")
        {
            item["from"] = entry.from;
            item["to"] = entry.to;
        }
        else if (entry.kind == "command")
        {
            item["undoCommand"] = entry.undoCommand;
        }
        else
        {
            item["path"] = entry.path;
        }
        entries.push_back(std::move(item));
    }
    root["entries"] = std::move(entries);
    return root.dump(2) + "\n";
}

bool journalFromJson(const std::string& text, Journal& journal, std::string& error)
{
    error.clear();
    json root;
    try
    {
        root = json::parse(text);
    }
    catch (const json::exception& exception)
    {
        error = exception.what();
        return false;
    }

    if (!root.is_object())
    {
        error = "回滚日志的根不是 JSON 对象";
        return false;
    }

    Journal parsed;
    parsed.schemaVersion = root.value("schemaVersion", 1);
    parsed.createdAt = root.value("createdAt", std::string());
    if (parsed.schemaVersion != 1)
    {
        error = "不支持的回滚日志版本：" + std::to_string(parsed.schemaVersion);
        return false;
    }
    if (!root.contains("entries") || !root["entries"].is_array())
    {
        error = "回滚日志缺少 entries 数组";
        return false;
    }

    try
    {
        for (const auto& item : root["entries"])
        {
            JournalEntry entry;
            entry.actionId = item.value("actionId", std::string());
            entry.kind = item.value("kind", std::string());
            if (entry.kind == "registry")
            {
                entry.hive = item.value("hive", std::string());
                entry.key = item.value("key", std::string());
                entry.valueName = item.value("valueName", std::string());
                entry.existed = item.value("existed", false);
                if (entry.existed)
                {
                    entry.valueType = item.value("valueType", std::string("sz"));
                    entry.text = item.value("text", std::string());
                    entry.dword = item.value("dword", 0u);
                    entry.qword = item.value("qword", 0ull);
                    entry.bytesHex = item.value("bytesHex", std::string());
                    if (item.contains("multi") && item["multi"].is_array())
                    {
                        for (const auto& value : item["multi"])
                        {
                            entry.multi.push_back(value.get<std::string>());
                        }
                    }
                }
            }
            else if (entry.kind == "move")
            {
                entry.from = item.value("from", std::string());
                entry.to = item.value("to", std::string());
            }
            else if (entry.kind == "command")
            {
                entry.undoCommand = item.value("undoCommand", std::string());
            }
            else
            {
                entry.path = item.value("path", std::string());
            }
            parsed.entries.push_back(std::move(entry));
        }
    }
    catch (const json::exception& exception)
    {
        error = exception.what();
        return false;
    }

    journal = std::move(parsed);
    return true;
}

// ---------------------------------------------------------------- 执行与回滚

ExecResult executeActions(const std::vector<Action>& actions)
{
    ExecResult result;
    result.ok = true;
    result.journal.schemaVersion = 1;
    result.journal.createdAt = nowText();

    for (const Action& action : actions)
    {
        std::string error;
        if (!executeOne(action, result, error))
        {
            result.ok = false;
            result.failed += 1;
            result.error = action.id + "：" + error;
            result.log.push_back("失败  " + action.id + "：" + error);
            return result;
        }
        ++result.applied;
    }
    return result;
}

ExecResult execute(const Plan& plan)
{
    return executeActions(plan.actions);
}

ExecResult rollback(const Journal& journal, bool dryRun)
{
    ExecResult result;
    result.ok = true;
    result.journal = journal;

    for (auto it = journal.entries.rbegin(); it != journal.entries.rend(); ++it)
    {
        std::string error;
        if (!rollbackOne(*it, dryRun, result, error))
        {
            result.ok = false;
            result.failed += 1;
            result.error = it->actionId + "：" + error;
            result.log.push_back("回滚失败  " + it->actionId + "：" + error);
            return result;
        }
        ++result.applied;
    }
    return result;
}

// ---------------------------------------------------------------- 自检

bool selfTest(std::string& report, std::string& error)
{
    const std::string key = "Software\\OpenSysprepTool\\SelfTest";

    std::string cleanupError;
    regio::deleteTree(regio::Hive::CurrentUser, key, cleanupError);

    std::vector<Action> actions;

    Action text = makeRegistryAction(
        ActionKind::RegistrySet, "selftest.text", "自检", "写字符串", "selftest", "HKCU", key, "Text", "sz"
    );
    text.text = "hello 自检";
    actions.push_back(std::move(text));

    Action number = makeRegistryAction(
        ActionKind::RegistrySet,
        "selftest.dword",
        "自检",
        "写 DWORD",
        "selftest",
        "HKCU",
        key,
        "Number",
        "dword"
    );
    number.dword = 42;
    actions.push_back(std::move(number));

    Action list = makeRegistryAction(
        ActionKind::RegistrySet,
        "selftest.multi",
        "自检",
        "写多字符串",
        "selftest",
        "HKCU",
        key,
        "List",
        "multiSz"
    );
    list.multi = {"a", "b"};
    actions.push_back(std::move(list));

    const ExecResult applied = executeActions(actions);
    if (!applied.ok)
    {
        error = "写入失败：" + applied.error;
        return false;
    }
    if (applied.journal.entries.size() != 3)
    {
        error = "回滚记录数量不符：期望 3，实际 " + std::to_string(applied.journal.entries.size());
        return false;
    }

    regio::Key handle = regio::open(regio::Hive::CurrentUser, key, false, error);
    if (!handle.valid())
    {
        return false;
    }
    regio::Value value;
    bool exists = false;
    if (!regio::readValue(handle, "Text", value, exists, error) || !exists || value.text != "hello 自检")
    {
        error = error.empty() ? "字符串值读回不一致" : error;
        return false;
    }
    if (!regio::readValue(handle, "Number", value, exists, error) || !exists || value.dword != 42)
    {
        error = error.empty() ? "DWORD 值读回不一致" : error;
        return false;
    }
    if (!regio::readValue(handle, "List", value, exists, error) || !exists ||
        value.multi != std::vector<std::string>{"a", "b"})
    {
        error = error.empty() ? "多字符串值读回不一致" : error;
        return false;
    }
    handle.close();

    const ExecResult reverted = rollback(applied.journal, false);
    if (!reverted.ok)
    {
        error = "回滚失败：" + reverted.error;
        return false;
    }

    handle = regio::open(regio::Hive::CurrentUser, key, false, error);
    if (handle.valid())
    {
        bool stillThere = false;
        if (!regio::readValue(handle, "Text", value, stillThere, error))
        {
            return false;
        }
        if (stillThere)
        {
            error = "回滚后值仍然存在";
            return false;
        }
    }
    else if (error.find("GetLastError=2") != std::string::npos)
    {
        error.clear();
    }

    regio::deleteTree(regio::Hive::CurrentUser, key, cleanupError);

    report = "prepare selftest OK（3 个动作已写入并回滚，值已还原）";
    return true;
}

} // namespace osp::prepare

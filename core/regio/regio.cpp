#include "core/regio.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace osp::regio {
namespace {

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

std::string upperAscii(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return text;
}

HKEY nativeHive(Hive hive)
{
    switch (hive)
    {
    case Hive::LocalMachine:
        return HKEY_LOCAL_MACHINE;
    case Hive::CurrentUser:
        return HKEY_CURRENT_USER;
    case Hive::UsersDefault:
        return HKEY_USERS;
    }
    return nullptr;
}

std::wstring fullPath(Hive hive, const std::string& subKey)
{
    std::wstring path;
    if (hive == Hive::UsersDefault)
    {
        path = L".DEFAULT";
    }
    if (!subKey.empty())
    {
        if (!path.empty())
        {
            path += L'\\';
        }
        path += toWide(subKey);
    }
    return path;
}

REGSAM accessFor(Hive hive, bool writable)
{
    REGSAM access = writable ? (KEY_READ | KEY_WRITE) : KEY_READ;
    if (hive == Hive::LocalMachine)
    {
        // 32 位进程访问 HKLM\SOFTWARE 会被重定向到 WOW6432Node，必须显式要 64 位视图
        access |= KEY_WOW64_64KEY;
    }
    return access;
}

std::string describeWin32(LSTATUS code)
{
    std::string text = "GetLastError=" + std::to_string(code);

    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        static_cast<DWORD>(code),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr
    );
    if (length != 0 && buffer != nullptr)
    {
        std::wstring message(buffer, length);
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n'))
        {
            message.pop_back();
        }
        text += " (" + toUtf8(message) + ")";
    }
    if (buffer != nullptr)
    {
        LocalFree(buffer);
    }
    return text;
}

std::vector<std::uint8_t> encodeString(const std::string& text)
{
    const std::wstring wide = toWide(text);
    std::vector<std::uint8_t> bytes(wide.size() * sizeof(wchar_t) + sizeof(wchar_t), 0);
    if (!wide.empty())
    {
        std::memcpy(bytes.data(), wide.data(), wide.size() * sizeof(wchar_t));
    }
    return bytes;
}

std::string decodeString(const std::vector<std::uint8_t>& data)
{
    const std::size_t count = data.size() / sizeof(wchar_t);
    if (count == 0)
    {
        return std::string();
    }
    std::wstring wide(count, L'\0');
    std::memcpy(wide.data(), data.data(), count * sizeof(wchar_t));
    while (!wide.empty() && wide.back() == L'\0')
    {
        wide.pop_back();
    }
    return toUtf8(wide);
}

std::vector<std::uint8_t> encodeMulti(const std::vector<std::string>& values)
{
    std::vector<std::uint8_t> bytes;
    for (const std::string& value : values)
    {
        const std::wstring wide = toWide(value);
        const std::size_t offset = bytes.size();
        bytes.resize(offset + (wide.size() + 1) * sizeof(wchar_t), 0);
        if (!wide.empty())
        {
            std::memcpy(bytes.data() + offset, wide.data(), wide.size() * sizeof(wchar_t));
        }
    }
    // REG_MULTI_SZ 以额外的空字符串结尾；上面的每个字符串都已经带了结尾 NUL。
    bytes.resize(bytes.size() + sizeof(wchar_t), 0);
    return bytes;
}

std::vector<std::string> decodeMulti(const std::vector<std::uint8_t>& data)
{
    std::vector<std::string> values;
    const std::size_t count = data.size() / sizeof(wchar_t);
    if (count == 0)
    {
        return values;
    }
    std::wstring wide(count, L'\0');
    std::memcpy(wide.data(), data.data(), count * sizeof(wchar_t));

    std::size_t begin = 0;
    while (begin < wide.size())
    {
        std::size_t end = begin;
        while (end < wide.size() && wide[end] != L'\0')
        {
            ++end;
        }
        if (end == begin)
        {
            break; // 空字符串 = 列表结束
        }
        values.push_back(toUtf8(wide.substr(begin, end - begin)));
        begin = end + 1;
    }
    return values;
}

DWORD nativeType(ValueType type)
{
    switch (type)
    {
    case ValueType::String:
        return REG_SZ;
    case ValueType::ExpandString:
        return REG_EXPAND_SZ;
    case ValueType::Dword:
        return REG_DWORD;
    case ValueType::Qword:
        return REG_QWORD;
    case ValueType::MultiString:
        return REG_MULTI_SZ;
    case ValueType::Binary:
        return REG_BINARY;
    }
    return REG_SZ;
}

ValueType fromNativeType(DWORD type)
{
    switch (type)
    {
    case REG_EXPAND_SZ:
        return ValueType::ExpandString;
    case REG_DWORD:
        return ValueType::Dword;
    case REG_QWORD:
        return ValueType::Qword;
    case REG_MULTI_SZ:
        return ValueType::MultiString;
    case REG_BINARY:
        return ValueType::Binary;
    default:
        return ValueType::String;
    }
}

} // namespace

const char* hiveId(Hive hive)
{
    switch (hive)
    {
    case Hive::LocalMachine:
        return "HKLM";
    case Hive::CurrentUser:
        return "HKCU";
    case Hive::UsersDefault:
        return "HKU\\.DEFAULT";
    }
    return "?";
}

bool parseHive(const std::string& text, Hive& hive, std::string& error)
{
    const std::string upper = upperAscii(text);
    if (upper == "HKLM" || upper == "HKEY_LOCAL_MACHINE")
    {
        hive = Hive::LocalMachine;
        return true;
    }
    if (upper == "HKCU" || upper == "HKEY_CURRENT_USER")
    {
        hive = Hive::CurrentUser;
        return true;
    }
    if (upper == "HKU\\.DEFAULT" || upper == "HKEY_USERS\\.DEFAULT" || upper == "HKU" ||
        upper == "HKEY_USERS")
    {
        hive = Hive::UsersDefault;
        return true;
    }
    error = "未知的注册表根键: " + text + "（只支持 HKLM / HKCU / HKU\\.DEFAULT）";
    return false;
}

const char* valueTypeId(ValueType type)
{
    switch (type)
    {
    case ValueType::String:
        return "sz";
    case ValueType::ExpandString:
        return "expandSz";
    case ValueType::Dword:
        return "dword";
    case ValueType::Qword:
        return "qword";
    case ValueType::MultiString:
        return "multiSz";
    case ValueType::Binary:
        return "binary";
    }
    return "sz";
}

bool parseValueType(const std::string& text, ValueType& type, std::string& error)
{
    const std::string upper = upperAscii(text);
    if (upper == "SZ" || upper == "STRING" || upper == "REG_SZ")
    {
        type = ValueType::String;
        return true;
    }
    if (upper == "EXPANDSZ" || upper == "EXPAND" || upper == "REG_EXPAND_SZ")
    {
        type = ValueType::ExpandString;
        return true;
    }
    if (upper == "DWORD" || upper == "REG_DWORD")
    {
        type = ValueType::Dword;
        return true;
    }
    if (upper == "QWORD" || upper == "REG_QWORD")
    {
        type = ValueType::Qword;
        return true;
    }
    if (upper == "MULTISZ" || upper == "MULTI" || upper == "REG_MULTI_SZ")
    {
        type = ValueType::MultiString;
        return true;
    }
    if (upper == "BINARY" || upper == "REG_BINARY")
    {
        type = ValueType::Binary;
        return true;
    }
    error = "未知的注册表值类型: " + text;
    return false;
}

Key::~Key()
{
    close();
}

Key::Key(Key&& other) noexcept : handle_(other.handle_)
{
    other.handle_ = nullptr;
}

Key& Key::operator=(Key&& other) noexcept
{
    if (this != &other)
    {
        close();
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

bool Key::valid() const
{
    return handle_ != nullptr;
}

void Key::close()
{
    if (handle_ != nullptr)
    {
        RegCloseKey(static_cast<HKEY>(handle_));
        handle_ = nullptr;
    }
}

Key open(Hive hive, const std::string& subKey, bool writable, std::string& error)
{
    error.clear();
    Key key;
    const std::wstring path = fullPath(hive, subKey);
    HKEY handle = nullptr;
    const LSTATUS status =
        RegOpenKeyExW(nativeHive(hive), path.c_str(), 0, accessFor(hive, writable), &handle);
    if (status != ERROR_SUCCESS)
    {
        error = "RegOpenKeyEx 失败（" + std::string(hiveId(hive)) + "\\" + subKey + "）：" +
            describeWin32(status);
        return key;
    }
    key.handle_ = handle;
    return key;
}

Key create(Hive hive, const std::string& subKey, std::string& error)
{
    error.clear();
    Key key;
    const std::wstring path = fullPath(hive, subKey);
    HKEY handle = nullptr;
    DWORD disposition = 0;
    const LSTATUS status = RegCreateKeyExW(
        nativeHive(hive),
        path.c_str(),
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        accessFor(hive, true),
        nullptr,
        &handle,
        &disposition
    );
    if (status != ERROR_SUCCESS)
    {
        error = "RegCreateKeyEx 失败（" + std::string(hiveId(hive)) + "\\" + subKey + "）：" +
            describeWin32(status);
        return key;
    }
    key.handle_ = handle;
    return key;
}

bool readValue(Key& key, const std::string& name, Value& value, bool& exists, std::string& error)
{
    error.clear();
    value = Value{};
    exists = false;

    if (!key.valid())
    {
        error = "注册表句柄无效";
        return false;
    }

    const std::wstring wideName = toWide(name);
    const wchar_t* namePtr = name.empty() ? nullptr : wideName.c_str();

    DWORD type = 0;
    DWORD size = 0;
    LSTATUS status =
        RegQueryValueExW(static_cast<HKEY>(key.native()), namePtr, nullptr, &type, nullptr, &size);
    if (status == ERROR_FILE_NOT_FOUND)
    {
        return true;
    }
    if (status != ERROR_SUCCESS)
    {
        error = "RegQueryValueEx 失败（值 " + name + "）：" + describeWin32(status);
        return false;
    }

    std::vector<std::uint8_t> data(size, 0);
    status = RegQueryValueExW(
        static_cast<HKEY>(key.native()), namePtr, nullptr, &type, data.empty() ? nullptr : data.data(), &size
    );
    if (status != ERROR_SUCCESS)
    {
        error = "RegQueryValueEx 读取失败（值 " + name + "）：" + describeWin32(status);
        return false;
    }
    data.resize(size);

    value.type = fromNativeType(type);
    switch (value.type)
    {
    case ValueType::String:
    case ValueType::ExpandString:
        value.text = decodeString(data);
        break;
    case ValueType::Dword:
        if (data.size() >= sizeof(std::uint32_t))
        {
            std::memcpy(&value.dword, data.data(), sizeof(std::uint32_t));
        }
        break;
    case ValueType::Qword:
        if (data.size() >= sizeof(std::uint64_t))
        {
            std::memcpy(&value.qword, data.data(), sizeof(std::uint64_t));
        }
        break;
    case ValueType::MultiString:
        value.multi = decodeMulti(data);
        break;
    case ValueType::Binary:
        value.bytes = std::move(data);
        break;
    }

    exists = true;
    return true;
}

bool writeValue(Key& key, const std::string& name, const Value& value, std::string& error)
{
    error.clear();
    if (!key.valid())
    {
        error = "注册表句柄无效";
        return false;
    }

    std::vector<std::uint8_t> data;
    switch (value.type)
    {
    case ValueType::String:
    case ValueType::ExpandString:
        data = encodeString(value.text);
        break;
    case ValueType::Dword:
        data.resize(sizeof(std::uint32_t));
        std::memcpy(data.data(), &value.dword, sizeof(std::uint32_t));
        break;
    case ValueType::Qword:
        data.resize(sizeof(std::uint64_t));
        std::memcpy(data.data(), &value.qword, sizeof(std::uint64_t));
        break;
    case ValueType::MultiString:
        data = encodeMulti(value.multi);
        break;
    case ValueType::Binary:
        data = value.bytes;
        break;
    }

    const std::wstring wideName = toWide(name);
    const LSTATUS status = RegSetValueExW(
        static_cast<HKEY>(key.native()),
        name.empty() ? nullptr : wideName.c_str(),
        0,
        nativeType(value.type),
        data.empty() ? nullptr : data.data(),
        static_cast<DWORD>(data.size())
    );
    if (status != ERROR_SUCCESS)
    {
        error = "RegSetValueEx 失败（值 " + name + "）：" + describeWin32(status);
        return false;
    }
    return true;
}

bool deleteValue(Key& key, const std::string& name, std::string& error)
{
    error.clear();
    if (!key.valid())
    {
        error = "注册表句柄无效";
        return false;
    }
    const std::wstring wideName = toWide(name);
    const LSTATUS status =
        RegDeleteValueW(static_cast<HKEY>(key.native()), name.empty() ? nullptr : wideName.c_str());
    if (status == ERROR_FILE_NOT_FOUND)
    {
        return true;
    }
    if (status != ERROR_SUCCESS)
    {
        error = "RegDeleteValue 失败（值 " + name + "）：" + describeWin32(status);
        return false;
    }
    return true;
}

bool deleteTree(Hive hive, const std::string& subKey, std::string& error)
{
    error.clear();
    const std::wstring path = fullPath(hive, subKey);
    const LSTATUS status = RegDeleteTreeW(nativeHive(hive), path.c_str());
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND)
    {
        return true;
    }
    if (status != ERROR_SUCCESS)
    {
        error = "RegDeleteTree 失败（" + std::string(hiveId(hive)) + "\\" + subKey + "）：" +
            describeWin32(status);
        return false;
    }
    return true;
}

std::string describeValue(const Value& value)
{
    switch (value.type)
    {
    case ValueType::String:
    case ValueType::ExpandString:
        return value.text;
    case ValueType::Dword:
        return std::to_string(value.dword);
    case ValueType::Qword:
        return std::to_string(value.qword);
    case ValueType::MultiString: {
        std::string text;
        for (const std::string& item : value.multi)
        {
            if (!text.empty())
            {
                text += "|";
            }
            text += item;
        }
        return text;
    }
    case ValueType::Binary: {
        std::string text;
        char buffer[4] = {};
        for (std::uint8_t byte : value.bytes)
        {
            std::snprintf(buffer, sizeof(buffer), "%02x", byte);
            text += buffer;
        }
        return text;
    }
    }
    return std::string();
}

// ---------------------------------------------------------------- 离线 hive

Key openUserHiveKey(const std::string& hiveName, const std::string& subKey, bool writable, std::string& error)
{
    if (hiveName.empty())
    {
        error = "hive 名为空";
        return Key();
    }
    std::wstring path = toWide(hiveName);
    if (!subKey.empty())
    {
        path += L'\\';
        path += toWide(subKey);
    }
    HKEY handle = nullptr;
    const LSTATUS status =
        RegOpenKeyExW(HKEY_USERS, path.c_str(), 0, writable ? (KEY_READ | KEY_WRITE) : KEY_READ, &handle);
    if (status != ERROR_SUCCESS)
    {
        error = "RegOpenKeyEx 失败（HKU\\" + hiveName + (subKey.empty() ? "" : "\\" + subKey) + "）：" +
            describeWin32(status);
        return Key();
    }
    Key key;
    key.handle_ = handle;
    return key;
}

bool createUserHiveKey(const std::string& hiveName, const std::string& subKey, Key& key, std::string& error)
{
    if (hiveName.empty() || subKey.empty())
    {
        error = "hive 名或子键为空";
        return false;
    }
    const std::wstring path = toWide(hiveName) + L'\\' + toWide(subKey);
    HKEY handle = nullptr;
    DWORD disposition = 0;
    const LSTATUS status = RegCreateKeyExW(
        HKEY_USERS, path.c_str(), 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &handle, &disposition
    );
    if (status != ERROR_SUCCESS)
    {
        error = "RegCreateKeyEx 失败（HKU\\" + hiveName + "\\" + subKey + "）：" + describeWin32(status);
        return false;
    }
    key.handle_ = handle;
    return true;
}

LoadedHive::~LoadedHive()
{
    std::string ignored;
    unload(ignored);
}

LoadedHive::LoadedHive(LoadedHive&& other) noexcept : loaded_(other.loaded_), name_(std::move(other.name_))
{
    other.loaded_ = false;
    other.name_.clear();
}

LoadedHive& LoadedHive::operator=(LoadedHive&& other) noexcept
{
    if (this != &other)
    {
        std::string ignored;
        unload(ignored);
        loaded_ = other.loaded_;
        name_ = std::move(other.name_);
        other.loaded_ = false;
        other.name_.clear();
    }
    return *this;
}

// 打开并启用一个令牌特权。已启用就返回 true。
//
// 为什么必须这一步：RegLoadKey/RegUnLoadKey 要求调用进程的令牌里
// **启用** SeRestorePrivilege 与 SeBackupPrivilege。管理员令牌里这两个特权
// 默认是「存在但禁用」，所以哪怕是提权会话，直接 RegLoadKey 也会返回
// ERROR_PRIVILEGE_NOT_HELD(1314)。这个坑不实测根本发现不了。
bool enablePrivilege(const wchar_t* name, std::string& error)
{
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token) == 0)
    {
        error = "OpenProcessToken 失败：GetLastError=" + std::to_string(GetLastError());
        return false;
    }

    LUID luid = {};
    if (LookupPrivilegeValueW(nullptr, name, &luid) == 0)
    {
        error = "LookupPrivilegeValue 失败：" + toUtf8(std::wstring(name));
        CloseHandle(token);
        return false;
    }

    TOKEN_PRIVILEGES privileges = {};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Luid = luid;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    const BOOL ok = AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr);
    const DWORD lastError = GetLastError();
    CloseHandle(token);

    if (ok == 0)
    {
        error = "AdjustTokenPrivileges 失败：" + toUtf8(std::wstring(name)) +
            " GetLastError=" + std::to_string(lastError);
        return false;
    }
    // AdjustTokenPrivileges 在「特权不在令牌里」时也返回成功，必须再看 GetLastError
    if (lastError == ERROR_NOT_ALL_ASSIGNED)
    {
        error = "令牌里没有这个特权：" + toUtf8(std::wstring(name)) + "（需要以管理员身份运行）";
        return false;
    }
    return true;
}

bool LoadedHive::load(const std::string& utf8File, const std::string& hiveName, std::string& error)
{
    if (loaded_)
    {
        error = "已经加载了 " + name_ + "，不能重复加载";
        return false;
    }
    if (hiveName.empty())
    {
        error = "hive 名为空";
        return false;
    }

    // 同名已挂载：直接沿用。这既是幂等（同一次 --prepare 里多条动作写同一个 hive），
    // 也避免 RegLoadKey 报 ERROR_ALREADY_EXISTS 让人以为失败。
    HKEY existing = nullptr;
    if (RegOpenKeyExW(HKEY_USERS, toWide(hiveName).c_str(), 0, KEY_READ, &existing) == ERROR_SUCCESS)
    {
        RegCloseKey(existing);
        name_ = hiveName;
        loaded_ = true;
        return true;
    }

    // 加载/卸载 hive 需要这两个特权；管理员令牌里它们默认是禁用的，必须先启用
    std::string privilegeError;
    if (!enablePrivilege(SE_RESTORE_NAME, privilegeError) || !enablePrivilege(SE_BACKUP_NAME, privilegeError))
    {
        error = "启用 hive 加载特权失败：" + privilegeError;
        return false;
    }

    const std::wstring file = toWide(utf8File);
    const std::wstring name = toWide(hiveName);
    const LSTATUS status = RegLoadKeyW(HKEY_USERS, name.c_str(), file.c_str());
    if (status != ERROR_SUCCESS)
    {
        error = "RegLoadKey 失败（" + utf8File + " -> HKU\\" + hiveName + "）：" + describeWin32(status);
        return false;
    }
    name_ = hiveName;
    loaded_ = true;
    return true;
}

bool LoadedHive::unload(std::string& error)
{
    if (!loaded_)
    {
        return true;
    }
    // 卸载前必须确认没有遗留的键句柄，否则 RegUnloadKey 会返回 ERROR_ACCESS_DENIED
    const LSTATUS status = RegUnLoadKeyW(HKEY_USERS, toWide(name_).c_str());
    loaded_ = false;
    if (status != ERROR_SUCCESS)
    {
        error = "RegUnloadKey 失败（HKU\\" + name_ + "）：" + describeWin32(status);
        return false;
    }
    return true;
}

} // namespace osp::regio

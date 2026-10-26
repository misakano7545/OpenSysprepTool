#include "core/prepare.hpp"

#include <windows.h>
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

    if (!cfg.optimization.pageFileTarget.empty())
    {
        const std::string target = normalizeDir(cfg.optimization.pageFileTarget);
        if (!hasDriveLetter(target))
        {
            plan.warnings.push_back("虚拟内存目标不是盘符路径，已跳过：" + cfg.optimization.pageFileTarget);
        }
        else
        {
            Action action = makeRegistryAction(
                ActionKind::RegistrySet,
                "optimization.pageFileTarget",
                "系统优化",
                "把虚拟内存放到指定分区",
                "optimization.pageFileTarget",
                "HKLM",
                "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management",
                "PagingFiles",
                "multiSz"
            );
            action.multi = {target + "pagefile.sys 0 0"};
            action.requiresReboot = true;
            action.note = "0 0 = 由系统管理大小；重启后生效";
            plan.actions.push_back(std::move(action));
        }
    }

    if (cfg.optimization.pinTaskbarItems)
    {
        defer("optimization.pinTaskbarItems", "需要调用 Shell 的固定动词并处理用户配置文件，M9 不做");
    }
    if (!cfg.optimization.myDocumentsTarget.empty())
    {
        defer(
            "optimization.myDocumentsTarget",
            "要写到所有新用户必须离线加载 " + joinPath(opt.defaultUserProfile, "NTUSER.DAT") +
                "；离线 hive 归入 M10"
        );
    }
    if (cfg.optimization.autoSetIp)
    {
        defer(
            "optimization.autoSetIp", "配置模型没有 IP 地址 / 子网掩码 / 网关 / DNS 字段，无法生成 netsh 命令"
        );
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
    if (cfg.optimization.clearPrivacyTraces)
    {
        struct TraceKey
        {
            const char* id;
            const char* title;
            const char* key;
        };
        const TraceKey traces[] = {
            {"typedUrls", "清空 IE 地址栏历史", "Software\\Microsoft\\Internet Explorer\\TypedURLs"},
            {"runMru",
             "清空「运行」对话框历史",
             "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\RunMRU"},
            {"recentDocs",
             "清空最近文档",
             "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\RecentDocs"},
            {"lastVisited",
             "清空文件对话框历史（LastVisited）",
             "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ComDlg32\\LastVisitedPidlMRU"},
            {"openSave",
             "清空文件对话框历史（OpenSave）",
             "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ComDlg32\\OpenSavePidlMRU"},
            {"trayNotify",
             "清空托盘图标缓存",
             "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\TrayNotify"},
            {"winrar", "清空 WinRAR 路径历史", "Software\\WinRAR\\DialogEditHistory"},
        };

        for (const TraceKey& trace : traces)
        {
            Action action = makeRegistryAction(
                ActionKind::RegistryDeleteKey,
                std::string("optimization.privacy.") + trace.id,
                "隐私痕迹清理",
                trace.title,
                "optimization.clearPrivacyTraces",
                "HKCU",
                trace.key,
                "",
                "sz"
            );
            action.irreversible = true;
            action.note = "整个键删除；键不存在时跳过";
            plan.actions.push_back(std::move(action));
        }

        const std::string appData = env("APPDATA");
        if (!appData.empty())
        {
            Action recent;
            recent.kind = ActionKind::DeleteTree;
            recent.id = "optimization.privacy.recentFolder";
            recent.group = "隐私痕迹清理";
            recent.title = "清空「最近访问」目录";
            recent.sourceField = "optimization.clearPrivacyTraces";
            recent.path = joinPath(appData, "Microsoft\\Windows\\Recent");
            recent.irreversible = true;
            recent.note = "开始菜单「最近使用的项目」背后的快捷方式目录";
            plan.actions.push_back(std::move(recent));
        }
    }

    if (cfg.optimization.clearUpdateCache)
    {
        if (opt.windowsDir.empty())
        {
            plan.warnings.push_back("无法确定 %SystemRoot%，已跳过清理更新缓存");
        }
        else
        {
            Action action;
            action.kind = ActionKind::DeleteTree;
            action.id = "optimization.clearUpdateCache";
            action.group = "隐私痕迹清理";
            action.title = "清空 Windows 更新下载缓存";
            action.sourceField = "optimization.clearUpdateCache";
            action.path = joinPath(opt.windowsDir, "SoftwareDistribution\\Download");
            action.irreversible = true;
            action.note = "只删已下载的更新包，系统需要时会重新下载；正在安装更新时不要跑";
            plan.actions.push_back(std::move(action));
        }
    }

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
    if (cfg.deploy.verifyIntegrity)
    {
        defer("deploy.verifyIntegrity", "文件清单与 MD5 校验归入 M10（用户态防篡改）");
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

    if (cfg.sysprep.stopSysprepProcess)
    {
        Action action = addCleanup(
            ActionKind::RunCommand,
            "sysprep.stopSysprepProcess",
            "结束已在运行的 sysprep.exe",
            "sysprep.stopSysprepProcess"
        );
        action.program = joinPath(opt.windowsDir, "System32\\taskkill.exe");
        action.arguments = "/F /IM sysprep.exe /T";
        action.tolerateNonZeroExit = true;
        action.note = "没有在跑的 sysprep 时 taskkill 返回非 0，已按容忍处理";
        plan.actions.push_back(std::move(action));
    }

    if (cfg.sysprep.cleanShadowCopies)
    {
        Action action = addCleanup(
            ActionKind::RunCommand,
            "sysprep.cleanShadowCopies",
            "删除卷影副本与还原点",
            "sysprep.cleanShadowCopies"
        );
        action.program = joinPath(opt.windowsDir, "System32\\vssadmin.exe");
        action.arguments = "delete shadows /all /quiet";
        action.tolerateNonZeroExit = true;
        action.note = "没有卷影副本时返回非 0，已按容忍处理；删除后无法恢复";
        plan.actions.push_back(std::move(action));
    }

    if (cfg.sysprep.cleanEventLogs)
    {
        // 只清这几个固定日志：wevtutil el 枚举会把 100 多个通道都清掉，
        // 其中包含驱动安装记录之类的排错依据，没必要。
        const char* logs[] = {"System", "Application", "Setup", "Windows PowerShell"};
        for (const char* log : logs)
        {
            Action action = addCleanup(
                ActionKind::RunCommand,
                (std::string("sysprep.cleanEventLogs.") + log).c_str(),
                (std::string("清空事件日志：") + log).c_str(),
                "sysprep.cleanEventLogs"
            );
            action.program = joinPath(opt.windowsDir, "System32\\wevtutil.exe");
            action.arguments = std::string("cl \"") + log + "\"";
            action.tolerateNonZeroExit = true;
            action.note = "日志不存在或无权限时返回非 0，已按容忍处理";
            plan.actions.push_back(std::move(action));
        }
    }

    if (cfg.sysprep.cleanRecycleBin)
    {
        // 只删当前用户 SID 那一份，不整目录删 —— 其他用户的回收站不属于本次清理范围。
        const std::string sid = currentUserSid();
        if (sid.empty())
        {
            plan.warnings.push_back("取不到当前用户 SID，已跳过清空回收站");
        }
        else
        {
            const DWORD drives = GetLogicalDrives();
            for (int i = 0; i < 26; ++i)
            {
                if ((drives & (1u << i)) == 0)
                {
                    continue;
                }
                const std::string root = std::string(1, static_cast<char>('A' + i)) + ":\\";
                if (GetDriveTypeA(root.c_str()) != DRIVE_FIXED)
                {
                    continue;
                }
                Action action = addCleanup(
                    ActionKind::DeleteTree,
                    ("sysprep.cleanRecycleBin." + std::string(1, static_cast<char>('A' + i))).c_str(),
                    ("清空回收站：" + root).c_str(),
                    "sysprep.cleanRecycleBin"
                );
                action.path = root + "$Recycle.Bin\\" + sid;
                action.note = "只清当前用户 SID 的回收站目录";
                plan.actions.push_back(std::move(action));
            }
        }
    }

    if (cfg.sysprep.cleanComponentStore)
    {
        Action action = addCleanup(
            ActionKind::RunCommand,
            "sysprep.cleanComponentStore",
            "DISM 组件清理（startcomponentcleanup）",
            "sysprep.cleanComponentStore"
        );
        action.program = joinPath(opt.windowsDir, "System32\\Dism\\dism.exe");
        action.arguments = "/online /cleanup-image /startcomponentcleanup";
        action.note = "故意不加 /resetbase：那会让已装更新无法卸载，且耗时很长";
        plan.actions.push_back(std::move(action));
    }

    if (cfg.sysprep.removeUnattendResidue)
    {
        const char* residues[] = {
            "Panther\\unattend.xml",
            "System32\\Sysprep\\unattend.xml",
        };
        for (const char* residue : residues)
        {
            Action action = addCleanup(
                ActionKind::DeleteTree,
                (std::string("sysprep.removeUnattendResidue.") + residue).c_str(),
                (std::string("删除残留应答文件：%WINDIR%\\") + residue).c_str(),
                "sysprep.removeUnattendResidue"
            );
            action.path = joinPath(opt.windowsDir, residue);
            action.note = "不存在的文件跳过";
            plan.actions.push_back(std::move(action));
        }

        Action rootFile = addCleanup(
            ActionKind::DeleteTree,
            "sysprep.removeUnattendResidue.systemDrive",
            "删除系统盘根目录的 unattend.xml",
            "sysprep.removeUnattendResidue"
        );
        rootFile.path = joinPath(opt.systemDrive, "unattend.xml");
        rootFile.note = "不存在的文件跳过";
        plan.actions.push_back(std::move(rootFile));
    }

    if (cfg.sysprep.uninstallDrivers)
    {
        defer("sysprep.uninstallDrivers", "需要枚举第三方 OEM 驱动并避开存储控制器，归入 M10 的驱动卸载");
    }
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
        case ActionKind::BcdTimeout:
            out += "    执行  " + quoteIfNeeded(action.program) + " /set {bootmgr} timeout " +
                std::to_string(action.dword) + "（执行时读取原值写入回滚日志）\n";
            break;
        case ActionKind::RegistryDeleteKey:
            out += "    RegDeleteTree  " + action.hive + "\\" + action.key + "\n";
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
        case ActionKind::CopyTree:
        case ActionKind::MoveTree:
            item["path"] = action.path;
            item["destination"] = action.destination;
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

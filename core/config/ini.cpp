#include "core/ini.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

#include <windows.h>

#include "core/win7.hpp"

namespace osp::ini {
namespace {

std::string trim(const std::string& text)
{
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0)
    {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
    {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool isComment(const std::string& line)
{
    return !line.empty() && (line[0] == ';' || line[0] == '#');
}

// 去掉成对的首尾双引号。SC 的 nt5.ini 里 Line1="..." 就是这种写法，
// 引号是格式的一部分，不是值的内容。
std::string unquote(const std::string& text)
{
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
    {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

// 严格校验是不是合法 UTF-8。用 MB_ERR_INVALID_CHARS 让系统判，
// 比自己写状态机可靠（同一招在 win7::pathFromUtf8 里也用了）。
bool isValidUtf8(const std::string& bytes)
{
    if (bytes.empty())
    {
        return true;
    }
    const int wideLen = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, bytes.c_str(), static_cast<int>(bytes.size()), nullptr, 0
    );
    return wideLen > 0;
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

std::string decodeWith(UINT codePage, const std::string& bytes)
{
    if (bytes.empty())
    {
        return std::string();
    }
    const int wideLen =
        MultiByteToWideChar(codePage, 0, bytes.c_str(), static_cast<int>(bytes.size()), nullptr, 0);
    if (wideLen <= 0)
    {
        return std::string();
    }
    std::wstring wide(static_cast<std::size_t>(wideLen), L'\0');
    MultiByteToWideChar(codePage, 0, bytes.c_str(), static_cast<int>(bytes.size()), wide.data(), wideLen);
    return fromWide(wide);
}

} // namespace

std::string decodeToUtf8(const std::string& bytes)
{
    // UTF-8 BOM
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF)
    {
        const std::string body = bytes.substr(3);
        return isValidUtf8(body) ? body : decodeWith(CP_ACP, body);
    }
    // UTF-16 LE / BE BOM：转成宽字符再转 UTF-8
    if (bytes.size() >= 2)
    {
        const unsigned char lo = static_cast<unsigned char>(bytes[0]);
        const unsigned char hi = static_cast<unsigned char>(bytes[1]);
        if ((lo == 0xFF && hi == 0xFE) || (lo == 0xFE && hi == 0xFF))
        {
            const bool little = lo == 0xFF;
            const std::size_t offset = 2;
            const std::size_t count = (bytes.size() - offset) / 2;
            std::wstring wide;
            wide.reserve(count);
            for (std::size_t i = 0; i < count; ++i)
            {
                const unsigned char a = static_cast<unsigned char>(bytes[offset + i * 2]);
                const unsigned char b = static_cast<unsigned char>(bytes[offset + i * 2 + 1]);
                const wchar_t ch =
                    little ? static_cast<wchar_t>(a | (b << 8)) : static_cast<wchar_t>((a << 8) | b);
                wide.push_back(ch);
            }
            return fromWide(wide);
        }
    }
    // 无 BOM：合法 UTF-8 就当 UTF-8，否则按 ANSI（中文系统即 GBK）
    if (isValidUtf8(bytes))
    {
        return bytes;
    }
    return decodeWith(CP_ACP, bytes);
}

Document parse(const std::string& utf8Text)
{
    Document doc;
    doc.ok = true;

    std::string section;
    int lineNumber = 0;
    std::size_t begin = 0;
    std::vector<std::pair<std::string, std::string>> seen; // 已出现的 section\0key

    while (begin <= utf8Text.size())
    {
        std::size_t end = utf8Text.find('\n', begin);
        const bool last = end == std::string::npos;
        std::string line = last ? utf8Text.substr(begin) : utf8Text.substr(begin, end - begin);
        begin = last ? utf8Text.size() + 1 : end + 1;
        ++lineNumber;

        // CRLF / CR
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        {
            line.pop_back();
        }

        const std::string trimmed = trim(line);
        if (trimmed.empty() || isComment(trimmed))
        {
            continue;
        }

        if (trimmed.front() == '[')
        {
            const std::size_t close = trimmed.find(']');
            if (close == std::string::npos)
            {
                doc.warnings.push_back("第 " + std::to_string(lineNumber) + " 行：节名缺少 ']'，按原样使用");
                section = trim(trimmed.substr(1));
            }
            else
            {
                section = trim(trimmed.substr(1, close - 1));
            }
            continue;
        }

        Entry entry;
        entry.section = section;
        entry.line = lineNumber;
        const std::size_t eq = trimmed.find('=');
        if (eq == std::string::npos)
        {
            // 无键行：[Version] 段里的产品名就是这种，保留但不算键
            entry.key = std::string();
            entry.value = unquote(trimmed);
        }
        else
        {
            entry.key = trim(trimmed.substr(0, eq));
            entry.value = unquote(trim(trimmed.substr(eq + 1)));
        }
        doc.entries.push_back(entry);
    }

    return doc;
}

bool Document::has(const std::string& section, const std::string& key) const
{
    for (const Entry& entry : entries)
    {
        if (entry.section == section && entry.key == key)
        {
            return true;
        }
    }
    return false;
}

std::string
Document::get(const std::string& section, const std::string& key, const std::string& fallback) const
{
    for (const Entry& entry : entries)
    {
        if (entry.section == section && entry.key == key)
        {
            return entry.value;
        }
    }
    return fallback;
}

std::vector<std::string> Document::sections() const
{
    std::vector<std::string> names;
    for (const Entry& entry : entries)
    {
        if (std::find(names.begin(), names.end(), entry.section) == names.end())
        {
            names.push_back(entry.section);
        }
    }
    return names;
}

std::vector<Entry> Document::inSection(const std::string& section) const
{
    std::vector<Entry> result;
    for (const Entry& entry : entries)
    {
        if (entry.section == section)
        {
            result.push_back(entry);
        }
    }
    return result;
}

Document loadFile(const std::string& utf8Path)
{
    Document doc;
    std::ifstream in(osp::win7::pathFromUtf8(utf8Path), std::ios::binary);
    if (!in)
    {
        doc.ok = false;
        doc.error = "打不开文件：" + utf8Path;
        return doc;
    }
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty())
    {
        doc.ok = false;
        doc.error = "文件为空：" + utf8Path;
        return doc;
    }
    return parse(decodeToUtf8(bytes));
}

} // namespace osp::ini

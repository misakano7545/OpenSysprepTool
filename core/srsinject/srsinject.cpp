#include "core/srsinject.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>

#include <nlohmann/json.hpp>

#include "core/win7.hpp"

namespace osp::srsinject {
namespace {

std::string toUtf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.c_str()), u8.size());
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

std::string lowerAscii(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

// 拼接路径：根以分隔符结尾时不要重复加一个（"C:\" + "\Windows" -> "C:\\Windows"）
std::string joinPath(const std::string& root, const std::string& tail)
{
    if (root.empty())
    {
        return tail;
    }
    if (root.back() == '\\' || root.back() == '/')
    {
        return root + tail;
    }
    return root + "\\" + tail;
}

std::string trim(const std::string& text)
{
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r'))
    {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r'))
    {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::string current;
    for (char ch : text)
    {
        if (ch == '\n')
        {
            lines.push_back(trim(current));
            current.clear();
        }
        else
        {
            current.push_back(ch);
        }
    }
    lines.push_back(trim(current));
    return lines;
}

// ---------------------------------------------------------------- 字节与显示

std::vector<std::uint8_t> parseHexBytes(const std::string& text, std::size_t begin)
{
    std::vector<std::uint8_t> bytes;
    std::string token;
    const auto flush = [&bytes, &token]() {
        if (token.empty())
        {
            return;
        }
        const unsigned long value = std::strtoul(token.c_str(), nullptr, 16);
        bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
        token.clear();
    };
    for (std::size_t i = begin; i < text.size(); ++i)
    {
        const char ch = text[i];
        if (ch == ',')
        {
            flush();
        }
        else if (std::isxdigit(static_cast<unsigned char>(ch)) != 0)
        {
            token.push_back(ch);
        }
    }
    flush();
    return bytes;
}

// hex(2)/hex(7) 的 UTF-16LE 负载解成文本（去掉结尾的 00 00）
std::string utf16FromBytes(const std::vector<std::uint8_t>& bytes)
{
    std::wstring wide;
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2)
    {
        const wchar_t ch = static_cast<wchar_t>(bytes[i] | (bytes[i + 1] << 8));
        if (ch == L'\0')
        {
            break;
        }
        wide.push_back(ch);
    }
    if (wide.empty())
    {
        return std::string();
    }
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr
    );
    if (size <= 0)
    {
        return std::string();
    }
    std::string text(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), text.data(), size, nullptr, nullptr
    );
    return text;
}

std::string hexDump(const std::vector<std::uint8_t>& bytes, std::size_t limit = 24)
{
    std::string out;
    char buffer[8] = {};
    for (std::size_t i = 0; i < bytes.size() && i < limit; ++i)
    {
        std::snprintf(buffer, sizeof(buffer), "%02X", bytes[i]);
        if (!out.empty())
        {
            out += ' ';
        }
        out += buffer;
    }
    if (bytes.size() > limit)
    {
        out += " ...";
    }
    return out;
}

// .reg 里的字符串转义：\\ \" 以及 \0（续行）
std::string unescape(const std::string& text)
{
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != '\\' || i + 1 >= text.size())
        {
            out.push_back(text[i]);
            continue;
        }
        const char next = text[i + 1];
        if (next == '\\')
        {
            out.push_back('\\');
            ++i;
        }
        else if (next == '"')
        {
            out.push_back('"');
            ++i;
        }
        else
        {
            out.push_back(text[i]);
        }
    }
    return out;
}

// ---------------------------------------------------------------- 读文件

bool readAllBytes(const std::string& utf8Path, std::vector<std::uint8_t>& out, std::string& error)
{
    std::ifstream in(osp::win7::pathFromUtf8(utf8Path), std::ios::binary);
    if (!in)
    {
        error = "读不到文件: " + utf8Path;
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    in.close();
    return true;
}

bool decodeText(const std::vector<std::uint8_t>& raw, std::string& text, std::string& encoding)
{
    if (raw.size() >= 2 && raw[0] == 0xFF && raw[1] == 0xFE)
    {
        encoding = "utf-16le";
        for (std::size_t i = 2; i + 1 < raw.size(); i += 2)
        {
            const wchar_t ch = static_cast<wchar_t>(raw[i] | (raw[i + 1] << 8));
            const int size = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, nullptr, 0, nullptr, nullptr);
            if (size > 0)
            {
                std::string piece(static_cast<std::size_t>(size), '\0');
                WideCharToMultiByte(CP_UTF8, 0, &ch, 1, piece.data(), size, nullptr, nullptr);
                text += piece;
            }
        }
        return true;
    }
    if (raw.size() >= 2 && raw[0] == 0xFE && raw[1] == 0xFF)
    {
        encoding = "utf-16be";
        for (std::size_t i = 2; i + 1 < raw.size(); i += 2)
        {
            const wchar_t ch = static_cast<wchar_t>((raw[i] << 8) | raw[i + 1]);
            const int size = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, nullptr, 0, nullptr, nullptr);
            if (size > 0)
            {
                std::string piece(static_cast<std::size_t>(size), '\0');
                WideCharToMultiByte(CP_UTF8, 0, &ch, 1, piece.data(), size, nullptr, nullptr);
                text += piece;
            }
        }
        return true;
    }
    encoding =
        (raw.size() >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) ? "utf-8-bom" : "utf-8";
    const std::size_t begin = (encoding == "utf-8-bom") ? 3 : 0;
    text.assign(reinterpret_cast<const char*>(raw.data()) + begin, raw.size() - begin);
    return true;
}

} // namespace

const char* regTypeId(RegType type)
{
    switch (type)
    {
    case RegType::String:
        return "REG_SZ";
    case RegType::ExpandString:
        return "REG_EXPAND_SZ";
    case RegType::Dword:
        return "REG_DWORD";
    case RegType::Qword:
        return "REG_QWORD";
    case RegType::Binary:
        return "REG_BINARY";
    case RegType::MultiString:
        return "REG_MULTI_SZ";
    case RegType::Deleted:
        return "DELETE";
    }
    return "UNKNOWN";
}

RegFile parseRegFile(const std::string& utf8Path)
{
    RegFile file;
    file.path = utf8Path;

    std::vector<std::uint8_t> raw;
    if (!readAllBytes(utf8Path, raw, file.error))
    {
        return file;
    }
    if (!decodeText(raw, file.error, file.encoding))
    {
        return file;
    }
    file.error.clear();

    std::string text;
    std::string encoding;
    decodeText(raw, text, encoding);
    file.encoding = encoding;

    RegKey* current = nullptr;

    for (const std::string& line : splitLines(text))
    {
        if (line.empty() || line[0] == ';')
        {
            continue;
        }

        if (line.front() == '[' && line.back() == ']')
        {
            std::string inner = line.substr(1, line.size() - 2);
            RegKey key;
            if (!inner.empty() && inner.front() == '-')
            {
                key.deleted = true;
                inner.erase(inner.begin());
            }
            key.path = inner;
            file.keys.push_back(std::move(key));
            current = &file.keys.back();
            continue;
        }

        if (current == nullptr)
        {
            continue; // 键头之前的行（Windows Registry Editor Version 5.00）忽略
        }

        if (line.front() == '@')
        {
            const std::size_t eq = line.find('=');
            if (eq == std::string::npos)
            {
                continue;
            }
            const std::string rest = trim(line.substr(eq + 1));

            RegValue value;
            value.name.clear();
            if (rest == "-")
            {
                value.type = RegType::Deleted;
                value.rawData = "-";
                value.display = "(删除该值)";
            }
            else if (!rest.empty() && rest.front() == '"')
            {
                value.type = RegType::String;
                value.text = unescape(rest.substr(1, rest.size() >= 2 ? rest.size() - 2 : 0));
                value.rawData = rest;
                value.display = value.text;
            }
            current->values.push_back(std::move(value));
            continue;
        }

        if (line.front() != '"')
        {
            continue;
        }

        // "名称"=数据
        std::size_t closing = std::string::npos;
        for (std::size_t i = 1; i < line.size(); ++i)
        {
            if (line[i] == '\\')
            {
                ++i;
                continue;
            }
            if (line[i] == '"')
            {
                closing = i;
                break;
            }
        }
        if (closing == std::string::npos)
        {
            continue;
        }
        const std::size_t eq = line.find('=', closing);
        if (eq == std::string::npos)
        {
            continue;
        }

        RegValue value;
        value.name = unescape(line.substr(1, closing - 1));
        const std::string rest = trim(line.substr(eq + 1));

        if (rest == "-")
        {
            value.type = RegType::Deleted;
            value.rawData = "-";
            value.display = "(删除该值)";
        }
        else if (rest.rfind("dword:", 0) == 0)
        {
            value.type = RegType::Dword;
            value.rawData = rest;
            const unsigned long number = std::strtoul(rest.c_str() + 6, nullptr, 16);
            value.bytes = {
                static_cast<std::uint8_t>(number & 0xFF),
                static_cast<std::uint8_t>((number >> 8) & 0xFF),
                static_cast<std::uint8_t>((number >> 16) & 0xFF),
                static_cast<std::uint8_t>((number >> 24) & 0xFF)
            };
            char buffer[32] = {};
            std::snprintf(buffer, sizeof(buffer), "0x%08lX (%lu)", number, number);
            value.display = buffer;
        }
        else if (rest.rfind("hex(", 0) == 0)
        {
            const std::size_t colon = rest.find("):");
            const std::string code = (colon == std::string::npos) ? std::string() : rest.substr(4, colon - 4);
            value.rawData = rest;
            value.bytes = parseHexBytes(rest, colon == std::string::npos ? rest.size() : colon + 2);

            if (code == "2")
            {
                value.type = RegType::ExpandString;
                value.text = utf16FromBytes(value.bytes);
                value.display = value.text + "   [" + hexDump(value.bytes) + "]";
            }
            else if (code == "7")
            {
                value.type = RegType::MultiString;
                value.text = utf16FromBytes(value.bytes);
                value.display = value.text + "   [" + hexDump(value.bytes) + "]";
            }
            else if (code == "b")
            {
                value.type = RegType::Qword;
                unsigned long long number = 0;
                for (std::size_t i = value.bytes.size(); i-- > 0;)
                {
                    number = (number << 8) | value.bytes[i];
                }
                char buffer[32] = {};
                std::snprintf(buffer, sizeof(buffer), "0x%016llX (%llu)", number, number);
                value.display = buffer;
            }
            else
            {
                value.type = RegType::Binary;
                value.display = "[" + hexDump(value.bytes) + "]";
            }
        }
        else if (rest.rfind("hex:", 0) == 0)
        {
            value.type = RegType::Binary;
            value.rawData = rest;
            value.bytes = parseHexBytes(rest, 4);
            value.display = "[" + hexDump(value.bytes) + "]";
        }
        else if (!rest.empty() && rest.front() == '"')
        {
            value.type = RegType::String;
            value.rawData = rest;
            value.text = unescape(rest.substr(1, rest.size() >= 2 ? rest.size() - 2 : 0));
            value.display = value.text;
        }
        else
        {
            value.rawData = rest;
            value.display = rest;
        }

        current->values.push_back(std::move(value));
    }

    file.ok = true;
    return file;
}

MountedKey mountKeyOf(const std::string& rawPath, const std::string& mountKey)
{
    MountedKey result;

    const std::string lower = lowerAscii(rawPath);
    std::string rest;
    const std::string systemPrefix = "hkey_local_machine\\system\\";
    const std::string usersPrefix = "hkey_users\\";

    if (lower.rfind(systemPrefix, 0) == 0)
    {
        rest = rawPath.substr(systemPrefix.size());
        result.injectable = true;
    }
    else if (lower.rfind(usersPrefix, 0) == 0)
    {
        // Drv.pesrs 用的是 HK_USERS\DCSRS\...，DCSRS 就是 SC 自己的挂载点
        const std::string after = rawPath.substr(usersPrefix.size());
        const std::size_t sep = after.find('\\');
        rest = (sep == std::string::npos) ? std::string() : after.substr(sep + 1);
        result.injectable = !rest.empty();
    }
    else
    {
        // HKCU 等：离线 hive 注入管不到
        result.relative = rawPath;
        result.displayPath = rawPath;
        return result;
    }

    result.relative = rest;
    result.apiPath = mountKey + "\\" + rest;
    result.displayPath = "HKLM\\" + result.apiPath;
    return result;
}

// ---------------------------------------------------------------- 包扫描与计划

namespace {

struct PackageDir
{
    std::string dir;
    std::string regFile;
    bool usedPesrs = false;
    std::vector<std::string> sysFiles;
};

std::vector<PackageDir> findPackages(const std::string& utf8Dir, std::string& error)
{
    std::vector<PackageDir> found;
    error.clear();

    if (utf8Dir.empty())
    {
        error = "SRS 目录为空";
        return found;
    }

    const std::filesystem::path root = osp::win7::pathFromUtf8(utf8Dir);
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec))
    {
        error = "SRS 目录不存在或不是目录：" + utf8Dir;
        return found;
    }

    std::vector<std::filesystem::path> regFiles;
    std::vector<std::filesystem::path> sysFiles;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec))
    {
        if (ec)
        {
            break;
        }
        std::error_code typeEc;
        if (!it->is_regular_file(typeEc))
        {
            continue;
        }
        const std::string name = lowerAscii(toUtf8(it->path().filename()));
        if (name == "drv.pesrs" || name == "drv.reg")
        {
            regFiles.push_back(it->path());
        }
        else if (it->path().extension().string() == ".sys" ||
                 lowerAscii(it->path().extension().string()) == ".sys")
        {
            sysFiles.push_back(it->path());
        }
    }

    const auto entryFor = [&found](const std::string& dir) -> PackageDir& {
        for (PackageDir& item : found)
        {
            if (item.dir == dir)
            {
                return item;
            }
        }
        PackageDir fresh;
        fresh.dir = dir;
        found.push_back(std::move(fresh));
        return found.back();
    };

    for (const std::filesystem::path& file : regFiles)
    {
        PackageDir& entry = entryFor(toUtf8(file.parent_path()));
        const bool pesrs = lowerAscii(toUtf8(file.filename())) == "drv.pesrs";
        // 优先 .pesrs（它就是为离线挂载准备的），同名时不被 .reg 覆盖
        if (entry.regFile.empty() || (pesrs && !entry.usedPesrs))
        {
            entry.regFile = toUtf8(file);
            entry.usedPesrs = pesrs;
        }
    }

    for (const std::filesystem::path& file : sysFiles)
    {
        const std::string dir = toUtf8(file.parent_path());
        for (PackageDir& entry : found)
        {
            // 包目录本身或它下面的 drivers/
            if (dir == entry.dir || dir.rfind(entry.dir + "\\", 0) == 0)
            {
                entry.sysFiles.push_back(toUtf8(file));
            }
        }
    }

    std::sort(found.begin(), found.end(), [](const PackageDir& a, const PackageDir& b) {
        return a.dir < b.dir;
    });
    for (PackageDir& entry : found)
    {
        std::sort(entry.sysFiles.begin(), entry.sysFiles.end());
    }
    return found;
}

RegValue makeStringValue(const std::string& name, const std::string& text)
{
    RegValue value;
    value.name = name;
    value.type = RegType::String;
    value.text = text;
    value.rawData = "\"" + text + "\"";
    value.display = text;
    return value;
}

RegValue makeExpandValue(const std::string& name, const std::string& text)
{
    RegValue value;
    value.name = name;
    value.type = RegType::ExpandString;
    value.text = text;
    value.rawData = "hex(2):" + text;
    value.display = text;
    // UTF-16LE + 结尾 NUL
    const std::wstring wide = toWide(text);
    for (wchar_t ch : wide)
    {
        value.bytes.push_back(static_cast<std::uint8_t>(ch & 0xFF));
        value.bytes.push_back(static_cast<std::uint8_t>((ch >> 8) & 0xFF));
    }
    value.bytes.push_back(0);
    value.bytes.push_back(0);
    return value;
}

RegValue makeDwordValue(const std::string& name, std::uint32_t number)
{
    RegValue value;
    value.name = name;
    value.type = RegType::Dword;
    value.bytes = {
        static_cast<std::uint8_t>(number & 0xFF),
        static_cast<std::uint8_t>((number >> 8) & 0xFF),
        static_cast<std::uint8_t>((number >> 16) & 0xFF),
        static_cast<std::uint8_t>((number >> 24) & 0xFF)
    };
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "0x%08X (%u)", number, number);
    const std::string hexValue = buffer;
    value.rawData = hexValue;
    value.display = hexValue;
    return value;
}

bool hasValue(const std::vector<RegValue>& values, const char* name)
{
    return std::any_of(values.begin(), values.end(), [name](const RegValue& value) {
        return value.name == name;
    });
}

std::string baseNameNoExt(const std::string& path)
{
    const std::filesystem::path p = osp::win7::pathFromUtf8(path);
    return toUtf8(p.stem());
}

std::string baseNameUtf8(const std::string& path)
{
    return toUtf8(osp::win7::pathFromUtf8(path).filename());
}

} // namespace

Plan plan(const std::string& image, const std::string& srsDir, const std::string& mountKey)
{
    Plan result;
    result.image = image;
    while (result.image.size() > 3 && (result.image.back() == '\\' || result.image.back() == '/'))
    {
        result.image.pop_back();
    }
    result.mountKey = mountKey.empty() ? std::string(kDefaultMountKey) : mountKey;
    result.hivePath = joinPath(result.image, "Windows\\System32\\config\\SYSTEM");
    result.driversDir = joinPath(result.image, "Windows\\System32\\drivers");

    if (result.image.empty())
    {
        result.error = "--image 为空";
        return result;
    }

    result.imageLooksLikeWindows = win7::probeFileNative(toWide(joinPath(result.image, "Windows"))).exists;
    result.hiveExists = win7::probeFileNative(toWide(result.hivePath)).exists;
    if (!result.imageLooksLikeWindows)
    {
        result.warnings.push_back(
            "挂载点下看不到 Windows 目录: " + joinPath(result.image, "Windows") +
            "（--dry-run 不需要它真实存在）"
        );
    }
    if (!result.hiveExists)
    {
        result.warnings.push_back(
            "目标 SYSTEM hive 不存在: " + result.hivePath + "（真实注入前必须先挂载映像）"
        );
    }

    std::string error;
    const std::vector<PackageDir> dirs = findPackages(srsDir, error);
    if (!error.empty())
    {
        result.error = error;
        return result;
    }
    if (dirs.empty())
    {
        result.error = "在 " + srsDir +
            " 下没找到 SRS 包（需要 Drv.pesrs 或 Drv.reg；"
            "含 .inf 的标准包请用 --driver）";
        return result;
    }

    // 包内已声明的服务名（用于判断哪些 .sys 是「包内没有服务项」的裸驱动）
    std::set<std::string> declaredServices;

    for (const PackageDir& dir : dirs)
    {
        SrsPackage package;
        package.dir = dir.dir;
        package.regFile = dir.regFile;
        package.usedPesrs = dir.usedPesrs;
        package.sysFiles = dir.sysFiles;

        if (package.regFile.empty())
        {
            result.warnings.push_back("包内没有 Drv.pesrs / Drv.reg，只有裸 .sys: " + package.dir);
        }

        std::vector<RegKey> keys;
        if (!package.regFile.empty())
        {
            const RegFile parsed = parseRegFile(package.regFile);
            if (!parsed.ok)
            {
                result.error = "解析失败: " + parsed.error;
                return result;
            }
            keys = parsed.keys;
        }

        // 本包自己声明的服务名（按包粒度，免得跨包误报）
        std::set<std::string> packageServices;

        for (const RegKey& key : keys)
        {
            const MountedKey mounted = mountKeyOf(key.path, result.mountKey);
            if (!mounted.injectable)
            {
                package.foreignKeys.push_back(key.path);
                continue;
            }

            if (package.controlSet.empty())
            {
                const std::size_t pos = mounted.relative.find("ControlSet");
                if (pos != std::string::npos && pos + 14 <= mounted.relative.size())
                {
                    const std::string candidate = mounted.relative.substr(pos + 10, 3);
                    const bool digits = std::all_of(candidate.begin(), candidate.end(), [](unsigned char ch) {
                        return std::isdigit(ch) != 0;
                    });
                    if (digits)
                    {
                        package.controlSet = candidate;
                    }
                }
            }

            const bool isCdd = (mounted.relative.find("CriticalDeviceDatabase\\") != std::string::npos) ||
                (mounted.relative.size() > 24 &&
                 mounted.relative.rfind("CriticalDeviceDatabase") == (mounted.relative.size() - 24));
            const bool isServiceKey = (mounted.relative.find("\\Services\\") != std::string::npos) ||
                (mounted.relative.size() > 9 &&
                 mounted.relative.rfind("\\Services") == (mounted.relative.size() - 9));

            // 服务主键：...\Services\<name>（后面没有更深一层）
            const std::size_t servicesPos = mounted.relative.find("\\Services\\");
            const bool isServiceRoot = servicesPos != std::string::npos &&
                mounted.relative.find('\\', servicesPos + 10) == std::string::npos;

            if (isCdd)
            {
                RegistryOp op;
                op.apiPath = mounted.apiPath;
                op.displayPath = mounted.displayPath;
                op.kind = "cdd";
                const std::size_t sep = mounted.relative.find_last_of('\\');
                op.subject = (sep == std::string::npos) ? mounted.relative : mounted.relative.substr(sep + 1);
                op.values = key.values;

                // 宪章要求写 ClassGUID 与 Service，缺了要明说
                if (!hasValue(op.values, "ClassGUID"))
                {
                    result.warnings.push_back("CDD 条目没有 ClassGUID: " + mounted.displayPath);
                }
                if (!hasValue(op.values, "Service"))
                {
                    result.warnings.push_back("CDD 条目没有 Service: " + mounted.displayPath);
                }

                if (key.deleted)
                {
                    result.otherOps.push_back(op);
                }
                else
                {
                    result.cddOps.push_back(std::move(op));
                    ++package.cddCount;
                }
                continue;
            }

            if (isServiceRoot)
            {
                RegistryOp op;
                op.apiPath = mounted.apiPath;
                op.displayPath = mounted.displayPath;
                op.kind = "services";
                const std::size_t sep = mounted.relative.find_last_of('\\');
                op.subject = (sep == std::string::npos) ? mounted.relative : mounted.relative.substr(sep + 1);
                op.values = key.values;

                if (key.deleted)
                {
                    result.otherOps.push_back(op);
                    continue;
                }

                // 宪章点名要 Type / Start / ImagePath；包内没写就按内核驱动补默认值并标注推断
                if (!hasValue(op.values, "Type"))
                {
                    op.values.push_back(makeDwordValue("Type", 1)); // 1 = 内核驱动
                    op.inferred = true;
                }
                if (!hasValue(op.values, "Start"))
                {
                    op.values.push_back(makeDwordValue("Start", 0)); // 0 = Boot
                    op.inferred = true;
                }
                if (!hasValue(op.values, "ImagePath"))
                {
                    op.values.push_back(
                        makeExpandValue("ImagePath", "system32\\drivers\\" + op.subject + ".sys")
                    );
                    op.inferred = true;
                }

                declaredServices.insert(op.subject);
                packageServices.insert(lowerAscii(op.subject));
                package.serviceNames.push_back(op.subject);
                result.serviceOps.push_back(std::move(op));
                ++package.serviceCount;
                continue;
            }

            if (isServiceKey)
            {
                // 服务键下面的子键（例如 Parameters\PnpInterface）也照写，保持忠实
                RegistryOp op;
                op.apiPath = mounted.apiPath;
                op.displayPath = mounted.displayPath;
                op.kind = "services-subkey";
                op.subject = mounted.relative;
                op.values = key.values;
                result.otherOps.push_back(std::move(op));
                continue;
            }

            // 其余 SYSTEM hive 下的键也照写（包里的 AddReg 之类）
            RegistryOp op;
            op.apiPath = mounted.apiPath;
            op.displayPath = mounted.displayPath;
            op.kind = "other";
            op.subject = mounted.relative;
            op.values = key.values;
            result.otherOps.push_back(std::move(op));
        }

        // 裸 .sys：包内没有为它声明服务项时，按内核驱动补一条最小的 Services 键。
        // 这是宪章第 3 步的兜底 —— 包内声明优先，工具推断的值会标注出来。
        const std::string controlSetOfPackage =
            package.controlSet.empty() ? std::string("001") : package.controlSet;
        for (const std::string& sys : package.sysFiles)
        {
            const std::string name = lowerAscii(baseNameNoExt(sys));
            if (packageServices.count(name) != 0)
            {
                continue;
            }

            RegistryOp op;
            const std::string serviceName = baseNameNoExt(sys);
            op.apiPath =
                result.mountKey + "\\ControlSet" + controlSetOfPackage + "\\Services\\" + serviceName;
            op.displayPath = "HKLM\\" + op.apiPath;
            op.kind = "services";
            op.subject = serviceName;
            op.inferred = true;
            op.values.push_back(makeDwordValue("Type", 1));  // 1 = 内核驱动
            op.values.push_back(makeDwordValue("Start", 0)); // 0 = Boot
            op.values.push_back(makeExpandValue("ImagePath", "system32\\drivers\\" + serviceName + ".sys"));

            result.warnings.push_back(
                "包 " + package.dir + " 里的 " + baseNameUtf8(sys) +
                " 没有对应的 Services 项，已按内核驱动（Type=1/Start=0）"
                "补一条推断项"
            );
            packageServices.insert(name);
            result.serviceOps.push_back(std::move(op));
            ++package.serviceCount;
        }

        result.packages.push_back(std::move(package));
    }

    // 复制 .sys。包内声明了服务名却没有对应 .sys 的，只提示，不臆造文件。
    for (const SrsPackage& package : result.packages)
    {
        std::set<std::string> presentSys;
        for (const std::string& sys : package.sysFiles)
        {
            presentSys.insert(lowerAscii(baseNameUtf8(sys)));
            CopyOp copy;
            copy.from = sys;
            copy.to = result.driversDir + "\\" + baseNameUtf8(sys);
            result.copyOps.push_back(std::move(copy));
        }

        for (const std::string& name : package.serviceNames)
        {
            const std::string wanted = lowerAscii(name) + ".sys";
            if (presentSys.count(wanted) == 0)
            {
                result.warnings.push_back(
                    "服务 " + name + " 在包 " + package.dir + " 里没有对应的 .sys（可能由 in-box 驱动提供）"
                );
            }
        }
    }

    const std::string controlSet = result.packages.empty() || result.packages.front().controlSet.empty()
        ? std::string("001")
        : result.packages.front().controlSet;
    if (controlSet != "001")
    {
        result.warnings.push_back("包内使用的控制集是 ControlSet" + controlSet + "（通常为 001）");
    }
    result.warnings.push_back(
        "控制集取自包内声明的 ControlSet" + controlSet +
        "；真实注入时会先读目标 hive 的 Select\\Current 核对，不一致就按目标为准"
    );

    result.sysFileCount = result.copyOps.size();
    result.registryOpCount = result.serviceOps.size() + result.cddOps.size();
    result.ok = true;
    return result;
}

std::vector<std::string> executionSteps(const Plan& plan)
{
    std::vector<std::string> steps;
    steps.push_back("1. RegLoadKey     HKLM\\" + plan.mountKey + "  <-  " + plan.hivePath);
    steps.push_back(
        "2. 复制           " + std::to_string(plan.copyOps.size()) + " 个 .sys -> " + plan.driversDir
    );
    steps.push_back(
        "3. RegSetValueEx  " + std::to_string(plan.serviceOps.size()) +
        " 个 Services 键（Type / Start / ImagePath）"
    );
    steps.push_back(
        "4. RegSetValueEx  " + std::to_string(plan.cddOps.size()) +
        " 个 CriticalDeviceDatabase 键（ClassGUID / Service）"
    );
    steps.push_back("5. RegUnLoadKey   HKLM\\" + plan.mountKey + " 卸载");
    return steps;
}

std::string dryRunReport(const Plan& plan)
{
    std::string out;
    out += "模式 B：SRS 注册表绑定注入（离线 hive）\n";
    out += "  挂载点        : " + plan.image + "\n";
    out += "  目标 hive     : " + plan.hivePath + "\n";
    out += "  临时挂载键    : HKLM\\" + plan.mountKey + "\n";
    out += "  驱动目录      : " + plan.driversDir + "\n";
    out += "  包 / .sys / 注册表项: " + std::to_string(plan.packages.size()) + " / " +
        std::to_string(plan.sysFileCount) + " / " + std::to_string(plan.registryOpCount) + "\n";

    out += "\n执行步骤（预告，--dry-run 下一条都没执行）\n";
    for (const std::string& step : executionSteps(plan))
    {
        out += "  " + step + "\n";
    }

    for (const SrsPackage& package : plan.packages)
    {
        out += "\n包 " + package.dir + "\n";
        out += "  注册表来源 : " + package.regFile +
            (package.usedPesrs ? "（Drv.pesrs，面向离线挂载）" : "（Drv.reg）") + "\n";
        out += "  控制集     : ControlSet" +
            (package.controlSet.empty() ? "001（默认）" : package.controlSet) + "\n";
        out += "  Services 键: " + std::to_string(package.serviceCount) + " 个\n";
        out += "  CDD 条目   : " + std::to_string(package.cddCount) + " 条\n";
        out += "  .sys       : " + std::to_string(package.sysFiles.size()) + " 个\n";
        for (const std::string& path : package.sysFiles)
        {
            out += "      " + path + "\n";
        }
        if (!package.foreignKeys.empty())
        {
            out += "  被跳过的键（不属于 SYSTEM hive，离线注入管不到）\n";
            for (const std::string& key : package.foreignKeys)
            {
                out += "      " + key + "\n";
            }
        }
    }

    out += "\n将复制的驱动文件（" + std::to_string(plan.copyOps.size()) + " 个）\n";
    for (const CopyOp& copy : plan.copyOps)
    {
        out += "  " + copy.from + "\n";
        out += "    -> " + copy.to + "\n";
    }

    out += "\n将写入的 Services 注册表项（" + std::to_string(plan.serviceOps.size()) + " 个）\n";
    for (const RegistryOp& op : plan.serviceOps)
    {
        out += "  [" + op.displayPath + "]" + (op.inferred ? "   （值含工具推断）" : "") + "\n";
        for (const RegValue& value : op.values)
        {
            out += "      \"" + value.name + "\"  " + regTypeId(value.type) + " = " + value.display + "\n";
        }
    }

    out += "\n将写入的 CriticalDeviceDatabase 注册表项（" + std::to_string(plan.cddOps.size()) + " 条）\n";
    for (const RegistryOp& op : plan.cddOps)
    {
        out += "  [" + op.displayPath + "]\n";
        for (const RegValue& value : op.values)
        {
            out += "      \"" + value.name + "\"  " + regTypeId(value.type) + " = " + value.display + "\n";
        }
    }

    if (!plan.otherOps.empty())
    {
        out += "\n其余同源键（" + std::to_string(plan.otherOps.size()) + " 个，一并照写）\n";
        for (const RegistryOp& op : plan.otherOps)
        {
            out += "  [" + op.displayPath + "]  (" + op.kind + ")\n";
            for (const RegValue& value : op.values)
            {
                out +=
                    "      \"" + value.name + "\"  " + regTypeId(value.type) + " = " + value.display + "\n";
            }
        }
    }

    if (!plan.warnings.empty())
    {
        out += "\n告警\n";
        for (const std::string& warning : plan.warnings)
        {
            out += "  - " + warning + "\n";
        }
    }

    out += "\n以上为预览（--dry-run）：没有 RegLoadKey，没有复制文件，没有写任何注册表项。\n";
    return out;
}

std::string toJson(const Plan& plan)
{
    using nlohmann::json;

    const auto valueJson = [](const RegValue& value) {
        json item;
        item["name"] = value.name;
        item["type"] = regTypeId(value.type);
        item["display"] = value.display;
        item["rawData"] = value.rawData;
        return item;
    };
    const auto opJson = [&valueJson](const RegistryOp& op) {
        json item;
        item["apiPath"] = op.apiPath;
        item["keyPath"] = op.displayPath;
        item["kind"] = op.kind;
        item["subject"] = op.subject;
        item["inferred"] = op.inferred;
        json values = json::array();
        for (const RegValue& value : op.values)
        {
            values.push_back(valueJson(value));
        }
        item["values"] = std::move(values);
        return item;
    };

    json root;
    root["schemaVersion"] = 1;
    root["mode"] = "B";
    root["ok"] = plan.ok;
    root["error"] = plan.error;
    root["image"] = plan.image;
    root["mountKey"] = plan.mountKey;
    root["hivePath"] = plan.hivePath;
    root["driversDir"] = plan.driversDir;
    root["imageLooksLikeWindows"] = plan.imageLooksLikeWindows;
    root["hiveExists"] = plan.hiveExists;
    root["sysFileCount"] = plan.sysFileCount;
    root["registryOpCount"] = plan.registryOpCount;

    json packages = json::array();
    for (const SrsPackage& package : plan.packages)
    {
        json item;
        item["dir"] = package.dir;
        item["regFile"] = package.regFile;
        item["usedPesrs"] = package.usedPesrs;
        item["controlSet"] = package.controlSet;
        item["serviceCount"] = package.serviceCount;
        item["cddCount"] = package.cddCount;
        item["sysFiles"] = package.sysFiles;
        item["foreignKeys"] = package.foreignKeys;
        packages.push_back(std::move(item));
    }
    root["packages"] = std::move(packages);

    json copies = json::array();
    for (const CopyOp& copy : plan.copyOps)
    {
        json item;
        item["from"] = copy.from;
        item["to"] = copy.to;
        copies.push_back(std::move(item));
    }
    root["copyOps"] = std::move(copies);

    json services = json::array();
    for (const RegistryOp& op : plan.serviceOps)
    {
        services.push_back(opJson(op));
    }
    root["serviceOps"] = std::move(services);

    json cdd = json::array();
    for (const RegistryOp& op : plan.cddOps)
    {
        cdd.push_back(opJson(op));
    }
    root["cddOps"] = std::move(cdd);

    json others = json::array();
    for (const RegistryOp& op : plan.otherOps)
    {
        others.push_back(opJson(op));
    }
    root["otherOps"] = std::move(others);

    root["steps"] = executionSteps(plan);
    root["warnings"] = plan.warnings;
    return root.dump(2) + "\n";
}

// ---------------------------------------------------------------- 真实执行

namespace {

std::string describeWin32(DWORD code)
{
    return "GetLastError=" + std::to_string(code);
}

bool writeRegistryOp(const RegistryOp& op, std::vector<std::string>& log, std::string& error)
{
    const std::wstring path = toWide(op.apiPath);
    if (path.empty())
    {
        error = "键路径无法转换为 UTF-16: " + op.apiPath;
        return false;
    }

    HKEY key = nullptr;
    // KEY_WOW64_64KEY：显式要求 64 位视图，不让 WOW64 把注册表访问重定向走
    const REGSAM access = KEY_SET_VALUE | KEY_WOW64_64KEY;
    const LSTATUS created = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE, path.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, access, nullptr, &key, nullptr
    );
    if (created != ERROR_SUCCESS)
    {
        error = "RegCreateKeyEx 失败（" + op.displayPath + "）：" + describeWin32(created);
        return false;
    }

    log.push_back("RegCreateKeyEx  HKLM\\" + op.apiPath);
    for (const RegValue& value : op.values)
    {
        if (value.type == RegType::Deleted)
        {
            const LSTATUS removed =
                RegDeleteValueW(key, value.name.empty() ? nullptr : toWide(value.name).c_str());
            log.push_back("RegDeleteValue  \"" + value.name + "\" -> " + describeWin32(removed));
            continue;
        }

        const DWORD type = [&value]() -> DWORD {
            switch (value.type)
            {
            case RegType::String:
                return REG_SZ;
            case RegType::ExpandString:
                return REG_EXPAND_SZ;
            case RegType::Dword:
                return REG_DWORD;
            case RegType::Qword:
                return REG_QWORD;
            case RegType::MultiString:
                return REG_MULTI_SZ;
            default:
                return REG_BINARY;
            }
        }();

        std::vector<std::uint8_t> bytes = value.bytes;
        if (value.type == RegType::String || value.type == RegType::ExpandString)
        {
            // 字符串型按 UTF-16LE + 结尾 NUL 写入
            bytes.clear();
            const std::wstring wide = toWide(value.text);
            for (wchar_t ch : wide)
            {
                bytes.push_back(static_cast<std::uint8_t>(ch & 0xFF));
                bytes.push_back(static_cast<std::uint8_t>((ch >> 8) & 0xFF));
            }
            bytes.push_back(0);
            bytes.push_back(0);
        }

        const LSTATUS status = RegSetValueExW(
            key,
            value.name.empty() ? nullptr : toWide(value.name).c_str(),
            0,
            type,
            bytes.empty() ? nullptr : bytes.data(),
            static_cast<DWORD>(bytes.size())
        );
        log.push_back(
            "RegSetValueEx   \"" + value.name + "\" (" + regTypeId(value.type) + ") = " + value.display +
            " -> " + describeWin32(status)
        );
        if (status != ERROR_SUCCESS)
        {
            RegCloseKey(key);
            error = "RegSetValueEx 失败（" + op.displayPath + " 的 " + value.name + "）：" +
                describeWin32(status);
            return false;
        }
    }

    RegCloseKey(key);
    return true;
}

} // namespace

bool execute(const Plan& plan, std::vector<std::string>& log, std::string& error)
{
    log.clear();
    error.clear();

    if (!plan.ok)
    {
        error = plan.error;
        return false;
    }

    const std::wstring subKey = toWide(plan.mountKey);
    const std::wstring hive = toWide(plan.hivePath);

    // 1. 挂载目标 SYSTEM hive
    const LSTATUS loaded = RegLoadKeyW(HKEY_LOCAL_MACHINE, subKey.c_str(), hive.c_str());
    if (loaded != ERROR_SUCCESS)
    {
        error = "RegLoadKey 失败（需要管理员权限，且目标 hive 不能已被挂载）：" + describeWin32(loaded);
        return false;
    }
    log.push_back("RegLoadKey      HKLM\\" + plan.mountKey + " <- " + plan.hivePath);

    bool ok = true;
    {
        // 关掉 WOW64 文件系统重定向：目标路径可能落在 System32 下
        win7::ScopedWow64FsRedirection guard;
        (void)guard.disabled();

        // 2. 复制 .sys
        for (const CopyOp& copy : plan.copyOps)
        {
            const std::wstring from = toWide(copy.from);
            const std::wstring to = toWide(copy.to);
            if (CopyFileW(from.c_str(), to.c_str(), FALSE) == 0)
            {
                error = "复制失败 " + copy.from + " -> " + copy.to + "：" + describeWin32(GetLastError());
                ok = false;
                break;
            }
            log.push_back("CopyFile        " + copy.from + " -> " + copy.to);
        }

        // 3/4. 写注册表项
        if (ok)
        {
            for (const RegistryOp& op : plan.serviceOps)
            {
                if (!writeRegistryOp(op, log, error))
                {
                    ok = false;
                    break;
                }
            }
        }
        if (ok)
        {
            for (const RegistryOp& op : plan.cddOps)
            {
                if (!writeRegistryOp(op, log, error))
                {
                    ok = false;
                    break;
                }
            }
        }

        // 其余同源键
        if (ok)
        {
            for (const RegistryOp& op : plan.otherOps)
            {
                if (op.values.empty())
                {
                    continue;
                }
                if (!writeRegistryOp(op, log, error))
                {
                    ok = false;
                    break;
                }
            }
        }
    }

    // 5. 无论成败都要卸载，否则目标 hive 会被锁住
    const LSTATUS unloaded = RegUnLoadKeyW(HKEY_LOCAL_MACHINE, subKey.c_str());
    log.push_back("RegUnLoadKey    HKLM\\" + plan.mountKey + " -> " + describeWin32(unloaded));
    if (unloaded != ERROR_SUCCESS && ok)
    {
        error = "RegUnLoadKey 失败，目标 hive 可能仍被占用：" + describeWin32(unloaded);
        return false;
    }

    return ok;
}

} // namespace osp::srsinject

#include "core/oem.hpp"

#include <algorithm>
#include <cctype>
#include <cwchar>

#include <windows.h>

#include "core/ini.hpp"
#include "core/win7.hpp"

namespace osp::oem {
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
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr
    );
    return out;
}

std::string readString(HKEY root, const wchar_t* subKey, const wchar_t* name)
{
    std::wstring buffer(512, L'\0');
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        DWORD bytes = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
        DWORD type = 0;
        const LSTATUS status = RegGetValueW(root, subKey, name, RRF_RT_REG_SZ, &type, buffer.data(), &bytes);
        if (status == ERROR_SUCCESS)
        {
            const std::size_t length = bytes / sizeof(wchar_t);
            std::size_t actual = 0;
            while (actual < length && buffer[actual] != L'\0')
            {
                ++actual;
            }
            buffer.resize(actual);
            return toUtf8(buffer);
        }
        if (status == ERROR_MORE_DATA)
        {
            // 值比缓冲区长：按返回的长度重来一次
            buffer.resize(bytes / sizeof(wchar_t) + 1);
            continue;
        }
        return std::string();
    }
    return std::string();
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

std::string upperAscii(const std::string& text)
{
    std::string out = text;
    for (char& ch : out)
    {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return out;
}

// 品牌名匹配：BIOS 里的厂商名往往带尾缀或与目录名不完全一致
// （"LENOVO" vs "Lenovo"、"Hewlett-Packard" vs "HP"）。
// 这里只做两件事：忽略大小写；一方包含另一方即算命中。
bool brandMatches(const std::string& brand, const std::string& biosManufacturer)
{
    const std::string a = lowerAscii(trim(brand));
    const std::string b = lowerAscii(trim(biosManufacturer));
    if (a.empty() || b.empty())
    {
        return false;
    }
    return a == b || b.find(a) != std::string::npos || a.find(b) != std::string::npos;
}

} // namespace

BiosInfo readBios()
{
    BiosInfo info;
    const wchar_t* kPath = L"HARDWARE\\DESCRIPTION\\System\\BIOS";
    info.manufacturer = readString(HKEY_LOCAL_MACHINE, kPath, L"SystemManufacturer");
    info.productName = readString(HKEY_LOCAL_MACHINE, kPath, L"SystemProductName");
    info.version = readString(HKEY_LOCAL_MACHINE, kPath, L"SystemVersion");
    info.family = readString(HKEY_LOCAL_MACHINE, kPath, L"SystemFamily");
    info.biosVendor = readString(HKEY_LOCAL_MACHINE, kPath, L"BIOSVendor");
    info.biosVersion = readString(HKEY_LOCAL_MACHINE, kPath, L"BIOSVersion");

    if (info.manufacturer.empty() && info.productName.empty())
    {
        info.ok = false;
        info.error = "BIOS 标识读不到（HKLM\\HARDWARE\\DESCRIPTION\\System\\BIOS 为空）";
        return info;
    }
    info.ok = true;
    return info;
}

LoadResult loadIni(const std::string& utf8Path, const std::string& language)
{
    LoadResult result;

    const ini::Document doc = ini::loadFile(utf8Path);
    if (!doc.ok)
    {
        result.error = doc.error;
        return result;
    }
    result.warnings = doc.warnings;

    // [General_<LANG>] 优先，退回 [General]（nt5.ini 只有后者）
    std::string generalSection = "General_" + upperAscii(language);
    if (!doc.has(generalSection, "Manufacturer") && !doc.has(generalSection, "Model"))
    {
        generalSection = "General";
    }

    Profile& p = result.profile;
    p.manufacturer = trim(doc.get(generalSection, "Manufacturer"));
    p.model = trim(doc.get(generalSection, "Model"));
    // [Support] 是 nt6 的写法，[Support Information] 是 nt5 的，两个都看
    p.supportUrl = trim(doc.get("Support", "Url"));
    if (p.supportUrl.empty())
    {
        p.supportUrl = trim(doc.get("Support", "URL"));
    }
    p.supportPhone = trim(doc.get("Support", "Tel"));
    p.supportHours = trim(doc.get("Support", "Stime"));
    p.supportProvider = trim(doc.get("Support", "Provider"));

    if (p.manufacturer.empty() && p.model.empty() && p.supportUrl.empty() && p.supportPhone.empty())
    {
        result.error = "这份 ini 里没有可用的 OEM 字段（找过 " + generalSection + " 与 [Support]）";
        return result;
    }

    p.source = utf8Path + "（" + generalSection + "）";
    result.ok = true;
    return result;
}

Profile fromBios(const BiosInfo& bios)
{
    Profile profile;
    if (!bios.ok)
    {
        return profile;
    }
    profile.manufacturer = trim(bios.manufacturer);
    profile.model = trim(bios.productName);
    profile.supportProvider = profile.manufacturer;
    profile.source = "BIOS 标识（HKLM\\HARDWARE\\DESCRIPTION\\System\\BIOS）";
    return profile;
}

bool hasContent(const Profile& profile)
{
    return !profile.manufacturer.empty() || !profile.model.empty() || !profile.supportUrl.empty() ||
        !profile.supportPhone.empty() || !profile.supportHours.empty() || !profile.supportProvider.empty();
}

DetectResult
detectFromSkinDir(const std::string& utf8SkinDir, const BiosInfo& bios, const std::string& language)
{
    DetectResult result;

    if (utf8SkinDir.empty())
    {
        result.error = "没有给品牌目录";
        return result;
    }

    std::error_code ec;
    const std::filesystem::path root = osp::win7::pathFromUtf8(utf8SkinDir);
    if (!std::filesystem::is_directory(root, ec))
    {
        result.error = "品牌目录不存在或不是目录：" + utf8SkinDir;
        return result;
    }

    // 品牌目录顺序不稳定，排序后再试，保证同样输入同样结果
    std::vector<std::filesystem::path> brands;
    for (const auto& item : std::filesystem::directory_iterator(root, ec))
    {
        if (item.is_directory(ec))
        {
            brands.push_back(item.path());
        }
    }
    std::sort(brands.begin(), brands.end());

    for (const std::filesystem::path& brandDir : brands)
    {
        const std::string brand = osp::win7::toUtf8Path(brandDir.filename());
        std::filesystem::path iniPath = brandDir / "nt6.ini";
        if (!std::filesystem::is_regular_file(iniPath, ec))
        {
            iniPath = brandDir / "nt5.ini";
            if (!std::filesystem::is_regular_file(iniPath, ec))
            {
                continue;
            }
        }

        const std::string iniUtf8 = osp::win7::toUtf8Path(iniPath);
        result.tried.push_back(brand);

        // 先用目录名与 BIOS 厂商对一次；对不上再看 ini 里的 Manufacturer。
        // 目录名更可靠：SC 的目录名就是品牌名，而 ini 里可能是「ACER（宏碁）」这种带注释的写法。
        const LoadResult loaded = loadIni(iniUtf8, language);
        const std::string iniManufacturer = loaded.ok ? loaded.profile.manufacturer : std::string();

        bool matched = brandMatches(brand, bios.manufacturer);
        if (!matched && !iniManufacturer.empty())
        {
            // ini 里的 Manufacturer 常带中文括注，取 '（' / '(' 之前的部分比
            std::string head = iniManufacturer;
            const std::size_t cut = head.find_first_of("（(");
            if (cut != std::string::npos)
            {
                head = head.substr(0, cut);
            }
            matched = brandMatches(brand, head) || brandMatches(head, bios.manufacturer);
        }
        if (!matched)
        {
            continue;
        }

        if (!loaded.ok)
        {
            // 品牌对上了但 ini 读不出字段：仍然算命中，只是没有可写的档案内容
            result.ok = true;
            result.brand = brand;
            result.iniPath = iniUtf8;
            result.profile = fromBios(bios);
            result.error = loaded.error;
            return result;
        }

        result.ok = true;
        result.brand = brand;
        result.iniPath = iniUtf8;
        result.profile = loaded.profile;
        result.warnings = loaded.warnings;
        return result;
    }

    result.error = "品牌目录里没有与 BIOS 厂商 \"" + bios.manufacturer + "\" 匹配的品牌";
    return result;
}

} // namespace osp::oem

#include "ui.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/sysinfo.hpp"

namespace osp::ui {
namespace {

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

std::string formatBytes(std::uint64_t bytes)
{
    char buffer[64] = {};
    const double gib = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    std::snprintf(buffer, sizeof(buffer), "%.1f GB", gib);
    return std::string(buffer);
}

std::string formatPercent(double percent)
{
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%.1f%%", percent);
    return std::string(buffer);
}

std::string currentUser()
{
    wchar_t buffer[256] = {};
    DWORD length = static_cast<DWORD>(std::size(buffer));
    if (GetUserNameW(buffer, &length) == 0)
    {
        return "读取失败";
    }
    return fromWide(std::wstring(buffer, length > 0 ? length - 1 : 0));
}

// WinPE / 正常系统。M2 的体检报告没有采集这一项，而 DFM 里 Lab_RunEnviron 要显示它，
// 所以在 UI 层补一个判定：WinPE 会建 HKLM\SYSTEM\CurrentControlSet\Control\MiniNT。
std::string runEnvironment()
{
    HKEY key = nullptr;
    const LSTATUS status = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\MiniNT", 0, KEY_READ | KEY_WOW64_64KEY, &key
    );
    if (status == ERROR_SUCCESS)
    {
        RegCloseKey(key);
        return "WinPE";
    }

    // 32 位进程在 64 位系统上跑，也值得点明 —— 与封装工具的部署环境判断相关
    BOOL wow64 = FALSE;
    const bool isWow64 = (IsWow64Process(GetCurrentProcess(), &wow64) != 0) && (wow64 != FALSE);
    return isWow64 ? "正常系统（32 位进程 / WOW64）" : "正常系统";
}

const sysinfo::PartitionInfo* systemPartition(const sysinfo::CheckupReport& report)
{
    for (const sysinfo::PartitionInfo& part : report.partitions)
    {
        if (part.systemDrive)
        {
            return &part;
        }
    }
    return nullptr;
}

std::string osName(const sysinfo::OsInfo& os)
{
    if (os.productName.empty())
    {
        return "未知";
    }
    if (os.editionId.empty())
    {
        return os.productName;
    }
    return os.productName + " " + os.editionId;
}

std::string archText(const sysinfo::OsInfo& os)
{
    std::string text = os.architecture.empty() ? std::string("unknown") : os.architecture;
    text += os.is64BitOs ? "（64 位系统）" : "（32 位系统）";
    return text;
}

// DFM 里这一组是 Lab_SysprepSupport + Lab_SysprepSupportok/no 两个图标位
std::string sysprepSupportText(const sysinfo::SysprepInfo& sysprep)
{
    if (!sysprep.available)
    {
        return "不支持（未找到 sysprep.exe）";
    }
    if (sysprep.sysprepPending)
    {
        return "不支持（系统已封装，处于待 OOBE/待部署状态）";
    }
    return "支持";
}

} // namespace

Page systemPage()
{
    const sysinfo::CheckupReport report = sysinfo::collect();

    Page page;
    page.id = "system";
    page.title = "母盘系统"; // 与 DFM 的 MlSkinTabSheet1.Caption 一致

    const sysinfo::PartitionInfo* system = systemPartition(report);

    page.fields.push_back({"Lab_Osnamev", "系统名称", osName(report.os)});
    page.fields.push_back({"Lab_Osbitv", "系统位宽", archText(report.os)});
    page.fields.push_back({"Lab_Osntverv", "内核版本", report.os.version.empty() ? "未知" : report.os.version}
    );
    page.fields.push_back({"Lab_SysprepSupportv", "封装支持判定", sysprepSupportText(report.sysprep)});
    page.fields.push_back(
        {"Lab_Osdrivesv",
         "系统分区",
         system == nullptr ? std::string("未识别")
                           : (system->drive + "  " + system->fileSystem + "  " +
                              (system->volumeLabel.empty() ? std::string("(无卷标)") : system->volumeLabel))}
    );
    page.fields.push_back(
        {"Lab_Osusesizev",
         "使用容量",
         system == nullptr ? std::string("未识别")
                           : (formatBytes(system->usedBytes) + " / " + formatBytes(system->totalBytes))}
    );
    page.fields.push_back({"Lab_OsUserv", "当前用户", currentUser()});
    page.fields.push_back(
        {"Lab_OsRelativesizev",
         "相对容量",
         system == nullptr ? std::string("未识别") : formatPercent(system->usedPercent)}
    );
    page.fields.push_back({"Lab_RunEnvironv", "运行环境", runEnvironment()});

    return page;
}

std::vector<Page> allPages()
{
    // M7a 只交付第一页；框架本身支持多页，后续里程碑往这里加。
    return {systemPage()};
}

// 与 DFM 图纸一一对应，验收脚本按这份名单核对「没有漏字段」
std::vector<std::string> expectedControlIds()
{
    return {
        "Lab_Osnamev",
        "Lab_Osbitv",
        "Lab_Osntverv",
        "Lab_SysprepSupportv",
        "Lab_Osdrivesv",
        "Lab_Osusesizev",
        "Lab_OsUserv",
        "Lab_OsRelativesizev",
        "Lab_RunEnvironv",
    };
}

std::string toJson(const std::vector<Page>& pages)
{
    using nlohmann::json;

    json root;
    root["schemaVersion"] = 1;
    json array = json::array();

    for (const Page& page : pages)
    {
        json item;
        item["id"] = page.id;
        item["title"] = page.title;
        json fields = json::array();
        for (const Field& field : page.fields)
        {
            json entry;
            entry["control"] = field.id;
            entry["label"] = field.label;
            entry["value"] = field.value;
            fields.push_back(std::move(entry));
        }
        item["fields"] = std::move(fields);
        array.push_back(std::move(item));
    }

    root["pages"] = std::move(array);
    return root.dump(2) + "\n";
}

} // namespace osp::ui

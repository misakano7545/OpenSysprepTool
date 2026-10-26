// 体检的「系统设置 / 开机启动项 / 系统插件」三组，对齐 SC 的 [Scandata]：
//
//   Ossetinspect + Dormancyon      -> 休眠占用大小
//   optimize_Dllcache              -> Dllcache 占用大小
//   Osstartrun_Sname / _Task       -> 启动项_服务名 / 启动项_计划任务
//   Osobj_BHO / _BE / _ActiveX     -> BHO / BE / ActiveX 插件
//
// 全是本机注册表与文件系统的只读查询，不碰 WMI（避免在坏掉的 WMI 环境里卡住）。

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <windows.h>

#include "core/sysinfo.hpp"
#include "core/win7.hpp"

namespace osp::sysinfo {
namespace {

std::string toUtf8(const wchar_t* text)
{
    if (text == nullptr || text[0] == L'\0')
    {
        return std::string();
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
    {
        return std::string();
    }
    std::string out(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    return out;
}

std::string toUtf8(const std::wstring& text)
{
    return toUtf8(text.c_str());
}

std::filesystem::path system32Dir()
{
    wchar_t buffer[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        return std::filesystem::path(L"C:\\Windows\\System32");
    }
    return std::filesystem::path(std::wstring(buffer, length));
}

// 递归累加目录占用。单个文件取不到大小就跳过，不因为一个坏链接整个失败。
std::uint64_t directoryBytes(const std::filesystem::path& root)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec))
    {
        return 0;
    }
    std::uint64_t total = 0;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec))
    {
        if (ec)
        {
            break;
        }
        std::error_code fileEc;
        if (it->is_regular_file(fileEc))
        {
            const auto size = it->file_size(fileEc);
            if (!fileEc)
            {
                total += size;
            }
        }
    }
    return total;
}

std::string formatMb(std::uint64_t bytes)
{
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return buffer;
}

// 枚举某个键下的子键名
std::vector<std::string> subKeyNames(HKEY root, const wchar_t* path, REGSAM extra)
{
    std::vector<std::string> names;
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, path, 0, KEY_ENUMERATE_SUB_KEYS | KEY_READ | extra, &key) != ERROR_SUCCESS)
    {
        return names;
    }
    for (DWORD index = 0;; ++index)
    {
        wchar_t name[512] = {};
        DWORD length = static_cast<DWORD>(std::size(name));
        const LSTATUS status = RegEnumKeyExW(key, index, name, &length, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS)
        {
            break;
        }
        if (status != ERROR_SUCCESS)
        {
            break;
        }
        names.push_back(toUtf8(std::wstring(name, length)));
    }
    RegCloseKey(key);
    return names;
}

std::string readStringValue(HKEY root, const wchar_t* path, const wchar_t* name, REGSAM extra)
{
    wchar_t buffer[1024] = {};
    DWORD bytes = sizeof(buffer);
    DWORD type = 0;
    if (RegGetValueW(root, path, name, RRF_RT_REG_SZ | extra, &type, buffer, &bytes) != ERROR_SUCCESS)
    {
        return std::string();
    }
    return toUtf8(buffer);
}

std::uint32_t readDwordValue(HKEY root, const wchar_t* path, const wchar_t* name, REGSAM extra)
{
    DWORD value = 0;
    DWORD bytes = sizeof(value);
    DWORD type = 0;
    if (RegGetValueW(root, path, name, RRF_RT_REG_DWORD | extra, &type, &value, &bytes) != ERROR_SUCCESS)
    {
        return 0;
    }
    return value;
}

Check baseCheck(const char* id, const char* severity, const char* title, std::string resolution)
{
    Check check;
    check.id = id;
    check.severity = severity;
    check.title = title;
    check.resolution = std::move(resolution);
    check.detected = false;
    check.evaluated = true;
    return check;
}

} // namespace

// ---------------------------------------------------------------- 系统设置体检

void checkHibernateSpace(std::vector<Check>& checks)
{
    Check check = baseCheck(
        "hibernateSpace",
        "info",
        "休眠占用",
        "不需要休眠可关掉释放空间：powercfg /hibernate off（一键修复会做）"
    );
    // SC 的 Dormancyon 文案就是「系统休眠已开启,占用大小:」。hiberfil.sys 在系统盘根。
    wchar_t systemDrive[MAX_PATH] = {};
    const DWORD length =
        GetEnvironmentVariableW(L"SystemDrive", systemDrive, static_cast<DWORD>(std::size(systemDrive)));
    if (length == 0)
    {
        check.evaluated = false;
        check.evidence = "取不到 %SystemDrive%";
        checks.push_back(std::move(check));
        return;
    }
    const std::filesystem::path hiveFile =
        std::filesystem::path(std::wstring(systemDrive, length)) / L"hiberfil.sys";
    std::error_code ec;
    const bool exists = std::filesystem::exists(hiveFile, ec);
    check.detected = exists;
    if (exists)
    {
        const auto size = std::filesystem::file_size(hiveFile, ec);
        const std::uint64_t bytes = ec ? 0 : static_cast<std::uint64_t>(size);
        check.message = "系统休眠已开启，占用 " + formatMb(bytes);
        check.evidence = win7::toUtf8Path(hiveFile);
        check.autoFix = "cmd:powercfg /hibernate off";
    }
    else
    {
        check.message = "系统休眠未开启，未占用空间";
        check.evidence = win7::toUtf8Path(hiveFile);
    }
    checks.push_back(std::move(check));
}

void checkDllCacheSpace(std::vector<Check>& checks)
{
    Check check = baseCheck(
        "dllCacheSpace", "info", "Dllcache 占用", "封装时勾选「清理 DLLcache 目录并在部署时还原」可减小体积"
    );
    // 位置可由 SfcDllCacheDir 改写，先问注册表，取不到再退回默认路径
    std::string configured = readStringValue(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",
        L"SfcDllCacheDir",
        KEY_WOW64_64KEY
    );
    std::filesystem::path dir;
    if (!configured.empty())
    {
        dir = win7::pathFromUtf8(configured);
    }
    else
    {
        dir = system32Dir() / L"dllcache";
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
    {
        check.message = "本机没有 DLLcache 目录（已由系统清理或本就不存在）";
        check.evidence = win7::toUtf8Path(dir);
        checks.push_back(std::move(check));
        return;
    }

    const std::uint64_t bytes = directoryBytes(dir);
    check.detected = bytes > 0;
    check.message = check.detected ? "DLLcache 占用 " + formatMb(bytes) : "DLLcache 为空";
    check.evidence = win7::toUtf8Path(dir);
    checks.push_back(std::move(check));
}

// ---------------------------------------------------------------- 开机启动项体检

StartupScan fillStartupServices(std::vector<StartupItem>& out)
{
    StartupScan scan;
    // 只收「自动启动的 Win32 服务」：Start==2，Type 里的 0x10（独立进程）或 0x20（共享进程）。
    // 驱动（Type==1/2）不算启动项，收了只会把报告淹掉。
    HKEY services = nullptr;
    const LSTATUS openStatus = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Services",
        0,
        KEY_ENUMERATE_SUB_KEYS | KEY_READ | KEY_WOW64_64KEY,
        &services
    );
    if (openStatus != ERROR_SUCCESS)
    {
        scan.error = "RegOpenKeyEx(Services) 失败，GetLastError=" + std::to_string(openStatus);
        return scan;
    }
    scan.accessible = true;

    std::vector<std::string> names;
    for (DWORD index = 0;; ++index)
    {
        wchar_t name[512] = {};
        DWORD length = static_cast<DWORD>(std::size(name));
        const LSTATUS status =
            RegEnumKeyExW(services, index, name, &length, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS || status != ERROR_SUCCESS)
        {
            break;
        }
        names.push_back(toUtf8(std::wstring(name, length)));
    }
    RegCloseKey(services);

    for (const std::string& name : names)
    {
        // 服务名是 ASCII 键名，直接宽化即可（这里不走 UTF-8 往返，避免多一次转换）
        std::wstring wideName(name.begin(), name.end());
        const std::wstring sub = L"SYSTEM\\CurrentControlSet\\Services\\" + wideName;
        const std::uint32_t start =
            readDwordValue(HKEY_LOCAL_MACHINE, sub.c_str(), L"Start", KEY_WOW64_64KEY);
        const std::uint32_t type = readDwordValue(HKEY_LOCAL_MACHINE, sub.c_str(), L"Type", KEY_WOW64_64KEY);
        if (start != 2 || (type & 0x30u) == 0)
        {
            continue;
        }
        StartupItem item;
        item.scope = "Service";
        item.name = name;
        item.command = readStringValue(HKEY_LOCAL_MACHINE, sub.c_str(), L"ImagePath", KEY_WOW64_64KEY);
        out.push_back(std::move(item));
        ++scan.count;
    }
    return scan;
}

StartupScan fillStartupTasks(std::vector<StartupItem>& out)
{
    StartupScan scan;
    // 计划任务在磁盘上就是 Tasks 目录下的一棵 xml 树，目录结构即任务路径。
    // 该目录的 ACL 只给 SYSTEM，非提权时**列不出来** —— 这与「目录不存在」必须分开：
    // 前者报「无法判定」，后者才能说「没有计划任务」。
    // 注意不能用 std::filesystem::exists 判存在性：权限不足时它返回 false，
    // 会把「读不到」伪装成「不存在」。用 GetFileAttributesW 才准。
    const std::filesystem::path root = system32Dir() / L"Tasks";
    if (::GetFileAttributesW(root.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        scan.accessible = true;
        scan.error = "目录不存在：" + win7::toUtf8Path(root);
        return scan;
    }

    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(root, ec);
    if (ec)
    {
        scan.accessible = false;
        scan.error = "目录存在但打不开（需要管理员权限）：" + ec.message();
        return scan;
    }

    std::vector<std::string> found;
    const std::filesystem::recursive_directory_iterator end;
    for (; it != end; it.increment(ec))
    {
        if (ec)
        {
            scan.accessible = false;
            scan.error = "枚举中断（" + ec.message() + "）：" + win7::toUtf8Path(root);
            return scan;
        }
        std::error_code fileEc;
        if (!it->is_regular_file(fileEc))
        {
            continue;
        }
        std::string relative = win7::toUtf8Path(std::filesystem::relative(it->path(), root, fileEc));
        if (fileEc || relative.empty())
        {
            continue;
        }
        found.push_back(relative);
    }

    scan.accessible = true;
    std::sort(found.begin(), found.end());
    for (const std::string& relative : found)
    {
        StartupItem item;
        item.scope = "Task";
        std::string name = relative;
        std::replace(name.begin(), name.end(), '/', '\\');
        item.name = name;
        item.command = win7::toUtf8Path(root) + "\\" + name;
        out.push_back(std::move(item));
        ++scan.count;
    }
    return scan;
}

// ---------------------------------------------------------------- 启动项覆盖

void checkStartupCoverage(std::vector<Check>& checks, const StartupScan& services, const StartupScan& tasks)
{
    {
        Check check = baseCheck(
            "startupServices",
            "info",
            "启动项_服务名",
            "自动启动的服务过多会拖慢开机；不需要的可在「服务」里改成手动"
        );
        check.evaluated = services.accessible;
        check.detected = services.accessible && services.count > 0;
        if (!services.accessible)
        {
            check.evidence = services.error;
            check.message = "启动项_服务名：无法判定（" + services.error + "）";
        }
        else
        {
            check.message = "启动项_服务名：自动启动 " + std::to_string(services.count) + " 个服务";
            check.evidence = "HKLM\\SYSTEM\\CurrentControlSet\\Services（Start=2 且 Type 含 0x10/0x20）";
        }
        checks.push_back(std::move(check));
    }

    {
        Check check = baseCheck(
            "startupTasks",
            "info",
            "启动项_计划任务",
            "确认计划任务里没有陌生项；不认识的可在任务计划程序里禁用"
        );
        check.evaluated = tasks.accessible;
        check.detected = tasks.accessible && tasks.count > 0;
        if (!tasks.accessible)
        {
            check.evidence = tasks.error;
            check.message = "启动项_计划任务：无法判定（" + tasks.error + "）";
        }
        else
        {
            check.message = "启动项_计划任务：" + std::to_string(tasks.count) + " 个任务";
            check.evidence = win7::toUtf8Path(system32Dir() / L"Tasks");
        }
        checks.push_back(std::move(check));
    }
}

// ---------------------------------------------------------------- 系统插件体检

void checkBrowserPlugins(std::vector<Check>& checks)
{
    // BHO：浏览器帮助对象，装了就会随 IE 一起加载
    {
        Check check = baseCheck(
            "browserBho",
            "warning",
            "BHO 插件",
            "确认不认识的可卸载：注册表里删掉对应的 Browser Helper Objects 子键"
        );
        std::vector<std::string> names;
        for (const auto& name : subKeyNames(
                 HKEY_LOCAL_MACHINE,
                 L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Browser Helper Objects",
                 KEY_WOW64_64KEY
             ))
        {
            names.push_back(name);
        }
        for (const auto& name : subKeyNames(
                 HKEY_LOCAL_MACHINE,
                 L"SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Browser Helper "
                 L"Objects",
                 0
             ))
        {
            names.push_back(name);
        }
        check.detected = !names.empty();
        check.message =
            names.empty() ? "没有 BHO 插件" : "发现 " + std::to_string(names.size()) + " 个 BHO 插件";
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            check.evidence += (i == 0 ? "" : "、") + names[i];
        }
        checks.push_back(std::move(check));
    }

    // BE：IE 的工具栏扩展
    {
        Check check = baseCheck(
            "browserBe",
            "warning",
            "BE 插件",
            "确认不认识的可卸载：删除 Internet Explorer\\Extensions 下对应子键"
        );
        std::vector<std::string> names = subKeyNames(
            HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Internet Explorer\\Extensions", KEY_WOW64_64KEY
        );
        check.detected = !names.empty();
        check.message =
            names.empty() ? "没有 BE 插件" : "发现 " + std::to_string(names.size()) + " 个 BE 插件";
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            check.evidence += (i == 0 ? "" : "、") + names[i];
        }
        checks.push_back(std::move(check));
    }

    // ActiveX：已下载安装到本机的控件，是恶意插件最常见的落点
    {
        Check check = baseCheck(
            "browserActiveX",
            "warning",
            "ActiveX 插件",
            "确认不认识的可卸载：删除 Code Store Database\\Distribution Units 下对应项"
        );
        std::vector<std::string> names = subKeyNames(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Code Store Database\\Distribution Units",
            KEY_WOW64_64KEY
        );
        check.detected = !names.empty();
        check.message = names.empty() ? "没有已安装的 ActiveX 控件"
                                      : "发现 " + std::to_string(names.size()) + " 个 ActiveX 控件";
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            check.evidence += (i == 0 ? "" : "、") + names[i];
        }
        checks.push_back(std::move(check));
    }
}

} // namespace osp::sysinfo

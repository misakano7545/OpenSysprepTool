#include "core/dism.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "core/win7.hpp"

namespace osp::dism {
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

std::string extensionOf(const std::filesystem::path& path)
{
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext;
}

// 去掉尾部反斜杠（保留 "C:\" 这种根路径）
std::string normalizeImage(const std::string& image)
{
    std::string out = image;
    while (out.size() > 3 && (out.back() == '\\' || out.back() == '/'))
    {
        out.pop_back();
    }
    return out;
}

} // namespace

std::vector<DriverPackage> scan(const std::string& utf8Dir, std::string& error)
{
    std::vector<DriverPackage> packages;
    error.clear();

    if (utf8Dir.empty())
    {
        error = "驱动目录为空";
        return packages;
    }

    const std::filesystem::path root = std::filesystem::path(utf8Dir);
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec))
    {
        error = "驱动目录不存在或不是目录：" + utf8Dir;
        return packages;
    }

    // 递归收集，按目录聚合
    std::vector<std::filesystem::path> files;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec))
    {
        if (ec)
        {
            break;
        }
        std::error_code typeEc;
        if (it->is_regular_file(typeEc))
        {
            files.push_back(it->path());
        }
    }

    // 目录 -> 包
    std::vector<std::pair<std::string, DriverPackage>> byDir;
    const auto packageFor = [&byDir](const std::filesystem::path& dir) -> DriverPackage& {
        const std::string key = toUtf8(dir);
        for (auto& item : byDir)
        {
            if (item.first == key)
            {
                return item.second;
            }
        }
        DriverPackage fresh;
        fresh.dir = key;
        byDir.emplace_back(key, std::move(fresh));
        return byDir.back().second;
    };

    for (const std::filesystem::path& file : files)
    {
        const std::string ext = extensionOf(file);
        DriverPackage& package = packageFor(file.parent_path());
        const std::string name = toUtf8(file.filename());

        if (ext == ".inf")
        {
            package.infFiles.push_back(name);
        }
        else if (ext == ".sys")
        {
            package.sysFiles.push_back(name);
        }
        else if (ext == ".cat")
        {
            package.catFiles.push_back(name);
        }
        else
        {
            package.otherFiles.push_back(name);
        }
    }

    for (auto& item : byDir)
    {
        DriverPackage& package = item.second;
        if (!package.hasInf())
        {
            continue; // 模式 A 只认含 .inf 的目录
        }
        std::sort(package.infFiles.begin(), package.infFiles.end());
        std::sort(package.sysFiles.begin(), package.sysFiles.end());
        std::sort(package.catFiles.begin(), package.catFiles.end());
        std::sort(package.otherFiles.begin(), package.otherFiles.end());
        packages.push_back(std::move(package));
    }

    std::sort(packages.begin(), packages.end(), [](const DriverPackage& a, const DriverPackage& b) {
        return a.dir < b.dir;
    });
    return packages;
}

std::string dismPath()
{
    // %SystemRoot% 从系统目录取；取不到退回 C:\Windows
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = GetWindowsDirectoryW(buffer, MAX_PATH);
    std::wstring windows =
        (length > 0 && length < MAX_PATH) ? std::wstring(buffer, length) : std::wstring(L"C:\\Windows");

    const auto toUtf8Wide = [](const std::wstring& wide) {
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
    };

    // dism.exe 是 64 位专有工具：
    //   官方位置      %SystemRoot%\System32\Dism.exe            <- 64 位本体
    //   同目录下的 Dism\ 只是放 provider DLL 的目录，里面没有 exe
    //   32 位进程不关重定向会看到 %SystemRoot%\SysWOW64\Dism.exe <- 32 位版，管不了 64 位映像
    // 所以这里必须走 probeFileNative（内部关掉重定向），并且优先取 System32 下的那个。
    const std::vector<std::wstring> candidates{
        windows + L"\\System32\\Dism.exe",
        windows + L"\\System32\\Dism\\dism.exe",
    };
    for (const std::wstring& candidate : candidates)
    {
        if (win7::probeFileNative(candidate).exists)
        {
            return toUtf8Wide(candidate);
        }
    }
    return toUtf8Wide(candidates.front());
}

std::string
buildAddDriverCommandLine(const std::string& program, const std::string& image, const std::string& infPath)
{
    // 逐字对齐微软文档：dism /image:<挂载点> /add-driver:<inf> /recurse
    // 路径一律加引号 —— 驱动目录名带空格是常态。
    std::string line;
    line += '"';
    line += program;
    line += "\" /image:\"";
    line += normalizeImage(image);
    line += "\" /add-driver:\"";
    line += infPath;
    line += "\" /recurse";
    return line;
}

Plan plan(const std::string& image, const std::string& driverDir)
{
    Plan result;
    result.image = normalizeImage(image);
    result.dismProgram = dismPath();

    if (result.image.empty())
    {
        result.error = "--image 为空";
        return result;
    }

    const win7::NativeFileInfo dismProbe = win7::probeFileNative(toWide(result.dismProgram));
    result.dismExists = dismProbe.exists;
    if (!dismProbe.exists)
    {
        // 不是致命错误：--dry-run 只打印命令行。但真实执行前必须解决。
        result.warnings.push_back("找不到 dism.exe: " + result.dismProgram);
    }

    // 挂载点下能看到 Windows 目录，才像是一个已挂载的 Windows 映像
    const std::string windowsDir = result.image + "\\Windows";
    result.imageLooksLikeWindows = win7::probeFileNative(toWide(windowsDir)).exists;
    if (!result.imageLooksLikeWindows)
    {
        result.warnings.push_back(
            "挂载点下看不到 Windows 目录: " + windowsDir + "（--dry-run 不需要它真实存在）"
        );
    }

    std::string error;
    result.packages = scan(driverDir, error);
    if (!error.empty())
    {
        result.error = error;
        return result;
    }

    if (result.packages.empty())
    {
        result.error = "在 " + driverDir +
            " 下没找到任何含 .inf 的标准驱动包（模式 A 需要 .inf；"
            "裸 .sys + Drv.reg/Drv.pesrs 请用 --srs）";
        return result;
    }

    for (const DriverPackage& package : result.packages)
    {
        if (package.sysFiles.empty())
        {
            result.warnings.push_back("驱动包只有 .inf 没有 .sys: " + package.dir);
        }
        if (package.catFiles.empty())
        {
            result.warnings.push_back(
                "驱动包没有 .cat 签名目录（Win7 上用未签名驱动会失败）: " + package.dir
            );
        }

        for (const std::string& inf : package.infFiles)
        {
            const std::string infPath = package.dir + "\\" + inf;
            Command command;
            command.infPath = infPath;
            command.commandLine = buildAddDriverCommandLine(result.dismProgram, result.image, infPath);
            result.commands.push_back(std::move(command));
        }
    }

    result.ok = true;
    return result;
}

std::string dryRunReport(const Plan& plan)
{
    std::string out;
    out += "模式 A：标准驱动包注入（dism /add-driver）\n";
    out += "  挂载点        : " + plan.image + "\n";
    out += "  驱动目录      : " + std::to_string(plan.packages.size()) + " 个包\n";
    out += "  dism.exe      : " + plan.dismProgram + (plan.dismExists ? "（存在）" : "（**不存在**）") + "\n";
    out += "  挂载点有 Windows 目录: " + std::string(plan.imageLooksLikeWindows ? "是" : "否") + "\n";
    out += "  实现方式      : CreateProcess 调 dism.exe（不链接 DismApi.dll）\n";

    for (const DriverPackage& package : plan.packages)
    {
        out += "\n驱动包 " + package.dir + "\n";
        out += "  .inf : " + std::to_string(package.infFiles.size()) + " 个";
        for (const std::string& name : package.infFiles)
        {
            out += "  " + name;
        }
        out += "\n";
        out += "  .sys : " + std::to_string(package.sysFiles.size()) + " 个\n";
        out += "  .cat : " + std::to_string(package.catFiles.size()) + " 个\n";
    }

    out += "\n将执行的完整命令行（" + std::to_string(plan.commands.size()) + " 条）\n";
    for (const Command& command : plan.commands)
    {
        out += "  " + command.commandLine + "\n";
    }

    if (!plan.warnings.empty())
    {
        out += "\n告警\n";
        for (const std::string& warning : plan.warnings)
        {
            out += "  - " + warning + "\n";
        }
    }

    out += "\n以上为预览（--dry-run）：没有调用 dism，没有改动挂载点。\n";
    return out;
}

std::string toJson(const Plan& plan)
{
    using nlohmann::json;

    json root;
    root["schemaVersion"] = 1;
    root["mode"] = "A";
    root["ok"] = plan.ok;
    root["error"] = plan.error;
    root["image"] = plan.image;
    root["dismProgram"] = plan.dismProgram;
    root["dismExists"] = plan.dismExists;
    root["imageLooksLikeWindows"] = plan.imageLooksLikeWindows;

    json packages = json::array();
    for (const DriverPackage& package : plan.packages)
    {
        json item;
        item["dir"] = package.dir;
        item["infFiles"] = package.infFiles;
        item["sysFiles"] = package.sysFiles;
        item["catFiles"] = package.catFiles;
        item["otherFiles"] = package.otherFiles;
        packages.push_back(std::move(item));
    }
    root["packages"] = std::move(packages);

    json commands = json::array();
    for (const Command& command : plan.commands)
    {
        json item;
        item["infPath"] = command.infPath;
        item["commandLine"] = command.commandLine;
        commands.push_back(std::move(item));
    }
    root["commands"] = std::move(commands);

    root["warnings"] = plan.warnings;
    return root.dump(2) + "\n";
}

bool runCommand(const std::string& commandLine, std::uint32_t& exitCode, std::string& error)
{
    exitCode = 0;
    error.clear();

    std::wstring wide = toWide(commandLine);
    if (wide.empty())
    {
        error = "命令行无法转换为 UTF-16（空串或编码错误）";
        return false;
    }

    std::vector<wchar_t> mutableCommandLine(wide.begin(), wide.end());
    mutableCommandLine.push_back(L'\0');

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION process = {};
    if (CreateProcessW(
            nullptr,
            mutableCommandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &startup,
            &process
        ) == 0)
    {
        error = "CreateProcess 失败，GetLastError=" + std::to_string(GetLastError());
        return false;
    }

    // 这类工具必须等它跑完 —— 不看退出码就往下走是没有意义的
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    exitCode = code;
    return true;
}

} // namespace osp::dism

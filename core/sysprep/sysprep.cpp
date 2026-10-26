#include "core/sysprep.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "core/sysinfo.hpp"
#include "core/win7.hpp"

namespace osp::sysprep {
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

std::string normalizeRoot(const std::string& path)
{
    std::string out = path;
    while (out.size() > 3 && (out.back() == '\\' || out.back() == '/'))
    {
        out.pop_back();
    }
    return out;
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

std::uint16_t readU16(const std::vector<std::uint8_t>& data, std::size_t offset)
{
    if (offset + 2 > data.size())
    {
        return 0;
    }
    return static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
}

std::uint32_t readU32(const std::vector<std::uint8_t>& data, std::size_t offset)
{
    if (offset + 4 > data.size())
    {
        return 0;
    }
    return static_cast<std::uint32_t>(
        data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) | (data[offset + 3] << 24)
    );
}

// 只读 PE 头的头 8 KB 就够，不必把 ntoskrnl.exe 整个读进来。
struct PeVersion
{
    bool ok = false;
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::string error;
};

PeVersion readPeOsVersion(const std::string& utf8Path)
{
    PeVersion result;

    std::ifstream in(osp::win7::pathFromUtf8(utf8Path), std::ios::binary);
    if (!in)
    {
        result.error = "读不到文件";
        return result;
    }
    std::vector<std::uint8_t> head(8 * 1024, 0);
    in.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
    const std::size_t read = static_cast<std::size_t>(in.gcount());
    in.close();
    head.resize(read);

    if (read < 64 || head[0] != 'M' || head[1] != 'Z')
    {
        result.error = "不是 PE 文件（缺 MZ）";
        return result;
    }

    const std::uint32_t peOffset = readU32(head, 0x3C);
    if (peOffset + 24 > read || head[peOffset] != 'P' || head[peOffset + 1] != 'E')
    {
        result.error = "不是 PE 文件（缺 PE 签名）";
        return result;
    }

    const std::size_t optional = peOffset + 24;
    const std::uint16_t magic = readU16(head, optional);
    if (magic != 0x10B && magic != 0x20B)
    {
        result.error = "PE 可选头 magic 未知";
        return result;
    }

    result.major = readU16(head, optional + 40);
    result.minor = readU16(head, optional + 42);
    result.ok = true;
    return result;
}

} // namespace

std::string sysprepPath()
{
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = GetWindowsDirectoryW(buffer, MAX_PATH);
    const std::wstring windows =
        (length > 0 && length < MAX_PATH) ? std::wstring(buffer, length) : std::wstring(L"C:\\Windows");

    const std::wstring exe = windows + L"\\System32\\Sysprep\\sysprep.exe";
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, exe.c_str(), static_cast<int>(exe.size()), nullptr, 0, nullptr, nullptr
    );
    if (size <= 0)
    {
        return std::string();
    }
    std::string text(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, exe.c_str(), static_cast<int>(exe.size()), text.data(), size, nullptr, nullptr
    );
    return text;
}

std::string buildCommandLine(const Options& options, const std::string& program)
{
    // 参数顺序按微软文档的语法行：
    //   Sysprep.exe [/oobe | /audit] [/generalize] [/reboot | /shutdown | /quit]
    //               [/quiet] [/unattend:<file>]
    std::string line = "\"" + program + "\"";

    if (options.generalize)
    {
        line += " /generalize";
    }
    line += options.oobe ? " /oobe" : " /audit";

    if (options.afterAction == "reboot")
    {
        line += " /reboot";
    }
    else if (options.afterAction == "quit")
    {
        line += " /quit";
    }
    else
    {
        line += " /shutdown";
    }

    if (options.quiet)
    {
        line += " /quiet";
    }
    if (!options.unattendPath.empty())
    {
        line += " /unattend:\"" + options.unattendPath + "\"";
    }
    return line;
}

Sha2Check checkSha2Support(const std::string& image)
{
    Sha2Check check;

    if (image.empty())
    {
        check.note = "没有给出 --image，无法离线判断目标系统版本；"
                     "Windows 7 目标上安装 SHA-2 签名的驱动前必须确认 KB4474419";
        return check;
    }

    const std::string root = normalizeRoot(image);
    const std::string kernel = joinPath(root, "Windows\\System32\\ntoskrnl.exe");

    const win7::NativeFileInfo kernelProbe = win7::probeFileNative(toWide(kernel));
    if (!kernelProbe.exists)
    {
        check.note = "找不到 " + kernel + "，无法判断目标系统版本（映像未挂载？）";
        check.evidence = kernel;
        return check;
    }

    const PeVersion version = readPeOsVersion(kernel);
    if (!version.ok)
    {
        check.note = "解析 ntoskrnl.exe 失败: " + version.error;
        return check;
    }

    check.checked = true;
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), "%u.%u", version.major, version.minor);
    check.targetOsVersion = buffer;
    check.targetOsEvidence = kernel + " 的 PE 头 MajorOperatingSystemVersion = " + check.targetOsVersion;

    // 6.1 = Windows 7 / Server 2008 R2；更早的版本也不带 SHA-2 支持
    check.targetIsWindows7 = (version.major < 6) || (version.major == 6 && version.minor <= 1);

    const std::string packagesDir = joinPath(root, "Windows\\servicing\\Packages");

    // 目录枚举同样要避开 WOW64 重定向（镜像里也可能出现 System32 下的路径）
    std::vector<std::string> hits;
    {
        win7::ScopedWow64FsRedirection guard;
        std::error_code ec;
        const std::filesystem::path dir(packagesDir);
        if (std::filesystem::is_directory(dir, ec))
        {
            for (std::filesystem::directory_iterator it(dir, ec), end; it != end; it.increment(ec))
            {
                if (ec)
                {
                    break;
                }
                const std::string name = lowerAscii(toUtf8(it->path().filename()));
                // KB4474419 的独立更新包；累积更新（RollupFix）也会带上它
                if (name.rfind("package_for_kb4474419", 0) == 0 ||
                    name.rfind("package_for_rollupfix", 0) == 0)
                {
                    hits.push_back(toUtf8(it->path()));
                }
            }
        }
        (void)guard.disabled();
    }

    if (hits.empty())
    {
        check.evidence = packagesDir + " 下没有 Package_for_KB4474419* 也没有 Package_for_RollupFix*";
    }
    else
    {
        check.packageFound = true;
        check.evidence =
            hits.front() + (hits.size() > 1 ? "（等 " + std::to_string(hits.size()) + " 个包）" : "");
    }

    if (!check.targetIsWindows7)
    {
        check.ok = true;
        check.note = "目标系统是 " + check.targetOsVersion +
            "，自带 SHA-2 代码签名支持，"
            "不需要 KB4474419";
    }
    else if (check.packageFound)
    {
        check.ok = true;
        check.note = "目标系统是 Windows " + check.targetOsVersion +
            "，已装 KB4474419（或含它的累积更新），可以安装 SHA-2 签名的驱动";
    }
    else
    {
        check.ok = false;
        check.note = "目标系统是 Windows " + check.targetOsVersion +
            "，未发现 KB4474419：2019-08 之后签名的驱动是 SHA-2 签名，"
            "未打该补丁的 Win7 会拒装。请先在目标上安装 KB4474419（需先装 "
            "KB4490628 服务栈更新）";
    }
    return check;
}

Report plan(const Options& options)
{
    return plan(options, {});
}

Report plan(const Options& options, const std::vector<sysinfo::Check>& injectedChecks)
{
    Report report;
    report.program = sysprepPath();

    if (report.program.empty())
    {
        report.error = "推不出 sysprep.exe 的路径";
        return report;
    }

    // 把默认值在这里统一掉：命令行与步骤必须基于同一份取值，
    // 否则会出现「命令写 /shutdown、步骤写 /quit」这种自相矛盾。
    Options effective = options;
    if (effective.afterAction.empty())
    {
        effective.afterAction = "shutdown";
    }

    report.programExists = win7::probeFileNative(toWide(report.program)).exists;
    if (!report.programExists)
    {
        // 32 位进程 + 64 位系统时，不关重定向会看到 SysWOW64，那里没有 Sysprep 目录
        report.warnings.push_back(
            "找不到 " + report.program +
            "（32 位进程下已关掉 WOW64 重定向再探；"
            "若仍找不到，说明本机确实没有 Sysprep 组件）"
        );
    }

    report.commandLine = buildCommandLine(effective, report.program);

    // 逐个参数列出来，便于逐条核对
    {
        std::string token;
        bool inQuotes = false;
        for (std::size_t i = 0; i <= report.commandLine.size(); ++i)
        {
            const bool atEnd = (i == report.commandLine.size());
            const char ch = atEnd ? ' ' : report.commandLine[i];
            if (ch == '"')
            {
                inQuotes = !inQuotes;
                token.push_back(ch);
                continue;
            }
            if (!atEnd && (ch != ' ' || inQuotes))
            {
                token.push_back(ch);
                continue;
            }
            if (!token.empty())
            {
                report.parameters.push_back(token);
                token.clear();
            }
        }
    }

    report.sha2 = checkSha2Support(options.image);
    if (report.sha2.checked && !report.sha2.ok)
    {
        report.blockers.push_back(report.sha2.note);
    }

    // 体检的 critical 项直接当阻塞项：不再让 --sysprep 和 --checkup 各写一套判断。
    // 只有本机（非离线映像）才查 —— 查的是当前系统的状态。
    if (!injectedChecks.empty())
    {
        report.checks = injectedChecks;
        for (const sysinfo::Check& check : injectedChecks)
        {
            if (check.severity == "critical" && check.detected)
            {
                report.blockers.push_back(
                    check.title + "：" + check.message + "（处理：" + check.resolution + "）"
                );
            }
        }
    }
    else if (options.image.empty())
    {
        const sysinfo::CheckupReport checkup = sysinfo::collect();
        for (const sysinfo::Check* check : sysinfo::blockingChecks(checkup))
        {
            report.blockers.push_back(
                check->title + "：" + check->message + "（处理：" + check->resolution + "）"
            );
        }
        report.checks = checkup.checks;
    }

    report.steps.push_back("1. 前置检查      sysprep.exe 存在、目标系统具备 SHA-2 签名支持");
    report.steps.push_back("2. 驱动注入      若本次要注入驱动，必须先完成（dism 或离线 hive）");
    report.steps.push_back("3. 执行封装      " + report.commandLine);
    if (effective.afterAction == "shutdown")
    {
        report.steps.push_back("4. 封装后        关机（/shutdown）");
    }
    else if (effective.afterAction == "reboot")
    {
        report.steps.push_back("4. 封装后        重启（/reboot）");
    }
    else
    {
        report.steps.push_back("4. 封装后        停留在当前界面（/quit）");
    }

    if (!effective.mode.empty())
    {
        report.steps.push_back(
            "封装方式：" + effective.mode + "（三种方式的 sysprep 参数相同，区别在封装前的清理动作）"
        );
    }

    if (report.programExists)
    {
        report.ok = true;
    }
    else if (options.image.empty())
    {
        // 本机没有 Sysprep 组件但仍然可以给出命令行：dry-run 只打印
        report.ok = true;
    }

    return report;
}

std::string dryRunReport(const Report& report)
{
    std::string out;
    out += "sysprep 编排\n";
    out +=
        "  程序          : " + report.program + (report.programExists ? "（存在）" : "（**不存在**）") + "\n";

    out += "\n完整命令行\n  " + report.commandLine + "\n";

    out += "\n参数逐条\n";
    for (const std::string& parameter : report.parameters)
    {
        out += "  " + parameter + "\n";
    }

    out += "\n执行步骤（预告，--dry-run 下一条都没执行）\n";
    for (const std::string& step : report.steps)
    {
        out += "  " + step + "\n";
    }

    out += "\nSHA-2 驱动签名支持检查（KB4474419）\n";
    const Sha2Check& sha2 = report.sha2;
    const auto line = [&out](const std::string& key, const std::string& value) {
        out += "  " + key + (key.size() < 16 ? std::string(16 - key.size(), ' ') : std::string()) + ": " +
            value + "\n";
    };
    line("是否已检查", sha2.checked ? "是" : "否");
    if (sha2.checked)
    {
        line("目标系统版本", sha2.targetOsVersion);
        line("版本证据", sha2.targetOsEvidence);
        line("目标是 Win7 系", sha2.targetIsWindows7 ? "是" : "否");
        if (!sha2.evidence.empty())
        {
            line("补丁证据", sha2.evidence);
        }
        line("已具备 SHA-2 支持", sha2.ok ? "是" : "否");
    }
    else
    {
        // 没给 --image 就没有依据，不要把「未判定」印成「否」
        line("判定", "未判定（需要 --image 指向离线目标）");
    }
    if (!sha2.note.empty())
    {
        line("结论", sha2.note);
    }

    if (!report.blockers.empty())
    {
        out += "\n阻塞项（真实执行前必须先解决）\n";
        for (const std::string& blocker : report.blockers)
        {
            out += "  - " + blocker + "\n";
        }
    }

    if (!report.warnings.empty())
    {
        out += "\n告警\n";
        for (const std::string& warning : report.warnings)
        {
            out += "  - " + warning + "\n";
        }
    }

    out += "\n以上为预览（--dry-run）：没有启动 sysprep，没有改动任何设置。\n";
    return out;
}

std::string toJson(const Report& report)
{
    using nlohmann::json;

    json root;
    root["schemaVersion"] = 1;
    root["ok"] = report.ok;
    root["error"] = report.error;
    root["program"] = report.program;
    root["programExists"] = report.programExists;
    root["commandLine"] = report.commandLine;
    root["parameters"] = report.parameters;
    root["steps"] = report.steps;

    json sha2;
    sha2["checked"] = report.sha2.checked;
    sha2["targetIsWindows7"] = report.sha2.targetIsWindows7;
    sha2["targetOsVersion"] = report.sha2.targetOsVersion;
    sha2["targetOsEvidence"] = report.sha2.targetOsEvidence;
    sha2["packageFound"] = report.sha2.packageFound;
    sha2["evidence"] = report.sha2.evidence;
    sha2["ok"] = report.sha2.ok;
    sha2["note"] = report.sha2.note;
    root["sha2"] = std::move(sha2);

    root["blockers"] = report.blockers;

    json checks = json::array();
    for (const sysinfo::Check& check : report.checks)
    {
        checks.push_back({
            {"id", check.id},
            {"severity", check.severity},
            {"title", check.title},
            {"detected", check.detected},
            {"evaluated", check.evaluated},
            {"message", check.message},
            {"resolution", check.resolution},
            {"evidence", check.evidence},
        });
    }
    root["checks"] = std::move(checks);
    root["warnings"] = report.warnings;
    return root.dump(2) + "\n";
}

} // namespace osp::sysprep

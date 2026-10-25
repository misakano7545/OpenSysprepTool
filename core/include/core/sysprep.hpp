#pragma once

// sysprep 编排（宪章 §8 M6）
//
// 负责：定位 sysprep.exe（64 位专有工具，必须关掉 WOW64 重定向再探）、
// 按微软文档拼出完整命令行、检查目标系统是否具备 SHA-2 驱动签名支持
// （Win7 上必须先装 KB4474419），并在真实执行前把阻塞项列清楚。
//
// `--dry-run` 只计算与打印，不启动任何进程。

#include <string>
#include <vector>

namespace osp::sysprep {

struct Options
{
    std::string image;        // 可选：离线目标根（用于 KB4474419 检查）
    std::string unattendPath; // 生成的 unattend.xml
    std::string afterAction;  // quit / reboot / shutdown
    std::string mode;         // classic / antiRogue / volume（传统 / 防流氓 / 大客户高级）
    bool generalize = true;   // /generalize
    bool oobe = true;         // /oobe（与 /audit 互斥）
    bool quiet = true;        // /quiet
};

// KB4474419 = Win7 的 SHA-2 代码签名支持更新。
// 2019 年 8 月之后签名的驱动一律是 SHA-2，未打该补丁的 Win7 会拒装。
struct Sha2Check
{
    bool checked = false; // 是否真的查过（需要 --image 才查得了）
    bool targetIsWindows7 = false;
    std::string targetOsVersion; // 从 <image>\Windows\System32\ntoskrnl.exe 的 PE 头读
    std::string targetOsEvidence;
    bool packageFound = false;
    std::string evidence; // 命中的 .mum 路径，或"没找到"的说明
    bool ok = false;      // 可以安装 SHA-2 签名的驱动
    std::string note;
};

struct Report
{
    bool ok = false;
    std::string error;
    std::string program; // sysprep.exe 全路径
    bool programExists = false;
    std::string commandLine;
    std::vector<std::string> steps;
    std::vector<std::string> parameters; // 命令行里的每个参数，便于逐条核对
    Sha2Check sha2;
    std::vector<std::string> warnings;
    std::vector<std::string> blockers; // 非空 = 真实执行前必须先解决
};

// %SystemRoot%\System32\Sysprep\sysprep.exe
std::string sysprepPath();

// 与微软文档一致：
//   Sysprep.exe [/oobe | /audit] [/generalize] [/reboot | /shutdown | /quit] [/quiet]
//               [/unattend:<file>]
std::string buildCommandLine(const Options& options, const std::string& program);

// 离线读 <image>\Windows\System32\ntoskrnl.exe 的 PE 头拿目标系统版本，
// 再按版本决定是否需要 KB4474419，并去 servicing\Packages 里找它的 .mum。
Sha2Check checkSha2Support(const std::string& image);

Report plan(const Options& options);

std::string dryRunReport(const Report& report);
std::string toJson(const Report& report);

} // namespace osp::sysprep

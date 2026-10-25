#pragma once

// 模式 A —— 标准驱动包注入（宪章 §8 M6）
//
// 输入形态：`.inf` + `.sys` + `.cat`，即 vmd/ 那类厂商标准驱动包。
// 实现路径：**只用 CreateProcess 调 dism**，绝不链接 DismApi.dll。
//
// 为什么不链接 DismApi.dll：
//   - DismApi 是一套 COM 式的进程内 API，要求调用方自己管理会话与映像挂载，
//     一旦加载就把 dism 的版本兼容问题搬进本进程；
//   - dism.exe 是 64 位专有工具，链接它的 API 会把「32 位进程 + 64 位工具」
//     的重定向问题复杂化；
//   - 命令行接口逐字可打印，--dry-run 能给出**与真实执行完全一致**的命令行，
//     这正是宪章对 M6 的验收要求。
// tests/integration/verify_drivers.py 会断言产物导入表里没有 dismapi.dll。

#include <string>
#include <vector>

namespace osp::dism {

// 一个标准驱动包 = 含 .inf 的那个目录及其内容
struct DriverPackage
{
    std::string dir; // 绝对路径
    std::vector<std::string> infFiles;
    std::vector<std::string> sysFiles;
    std::vector<std::string> catFiles;
    std::vector<std::string> otherFiles;

    bool hasInf() const
    {
        return !infFiles.empty();
    }
};

// 递归扫描目录，返回所有含 .inf 的目录。没有 .inf 的目录不计入模式 A。
std::vector<DriverPackage> scan(const std::string& utf8Dir, std::string& error);

// dism.exe 的位置：%SystemRoot%\System32\Dism\dism.exe。
// 32 位进程查 System32 会被静默重定向到 SysWOW64，而那里没有 Dism 目录，
// 所以必须关掉重定向再探（win7::probeFileNative 内部就是这么做的）。
std::string dismPath();

// 与微软文档一致：dism /image:<挂载点> /add-driver:<inf> /recurse
// 返回的命令行已经带好引号，可直接交给 CreateProcess。
std::string
buildAddDriverCommandLine(const std::string& program, const std::string& image, const std::string& infPath);

struct Command
{
    std::string infPath;
    std::string commandLine;
};

struct Plan
{
    bool ok = false;
    std::string error;
    std::string image;       // 归一化后的挂载点
    std::string dismProgram; // 完整的 dism.exe 路径
    bool dismExists = false;
    bool imageLooksLikeWindows = false; // 挂载点下能看到 Windows 目录
    std::vector<DriverPackage> packages;
    std::vector<Command> commands;
    std::vector<std::string> warnings;
};

// 只做静态计算：读目录、拼命令行。不调用 dism，不碰挂载点。
Plan plan(const std::string& image, const std::string& driverDir);

std::string dryRunReport(const Plan& plan);
std::string toJson(const Plan& plan);

// M6 共用的 CreateProcess 封装：启动一条命令行并等它结束。
// 放在 dism 模块里是因为 M6 只允许改 core/dism、core/srsinject、core/sysprep 三个目录，
// 而 dism 与 sysprep 都要用它。绝不使用 system()。
// 返回 false 表示启动失败；启动成功则 exitCode 是子进程退出码。
bool runCommand(const std::string& commandLine, std::uint32_t& exitCode, std::string& error);

} // namespace osp::dism

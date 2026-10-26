#include "core/tweaks.hpp"

#include <algorithm>

namespace osp::tweaks {
namespace {

const char* const kSessionManager = "SYSTEM\\CurrentControlSet\\Control\\Session Manager";
const char* const kMemoryManagement =
    "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management";
const char* const kDesktop = "Control Panel\\Desktop";
const char* const kExplorerPolicy = "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer";
const char* const kExplorer = "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer";
const char* const kWinlogon = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";

Tweak make(
    const char* id,
    const char* scLabel,
    const char* title,
    Hive hive,
    const char* path,
    const char* name,
    ValueType type,
    const char* value,
    bool requiresReboot
)
{
    Tweak tweak;
    tweak.id = id;
    tweak.scLabel = scLabel;
    tweak.title = title;
    tweak.hive = hive;
    tweak.path = path;
    tweak.name = name;
    tweak.type = type;
    tweak.value = value;
    tweak.requiresReboot = requiresReboot;
    return tweak;
}

std::vector<Tweak> build()
{
    std::vector<Tweak> list;

    // 顺序与 SC 的记录顺序一致。HKCU = SC 序号 2，HKLM = 序号 3。
    list.push_back(make(
        "tweak.diskTimeout",
        "optimize_diskouttime",
        "优化磁盘访问超时时间",
        Hive::LocalMachine,
        kSessionManager,
        "AutoChkTimeOut",
        ValueType::Integer,
        "5",
        true
    ));
    list.push_back(make(
        "tweak.noLowDiskSpaceChecks",
        "",
        "关闭磁盘空间不足提示",
        Hive::CurrentUser,
        kExplorerPolicy,
        "NoLowDiskSpaceChecks",
        ValueType::Integer,
        "1",
        false
    ));
    list.push_back(make(
        "tweak.ioPageLockLimit",
        "optimize_Memory",
        "优化内存配置，加速软件运行",
        Hive::LocalMachine,
        kMemoryManagement,
        "IoPageLockLimit",
        ValueType::Integer,
        "134217728",
        true
    ));
    list.push_back(make(
        "tweak.autoEndTasks",
        "optimize_Closetool",
        "自动关闭无响应的程序",
        Hive::CurrentUser,
        kDesktop,
        "AutoEndTasks",
        ValueType::String,
        "1",
        false
    ));
    list.push_back(make(
        "tweak.desktopProcess",
        "optimize_Pro",
        "为桌面与任务栏使用独立进程",
        Hive::CurrentUser,
        kExplorer,
        "DesktopProcess",
        ValueType::Integer,
        "1",
        true
    ));
    list.push_back(make(
        "tweak.autoRestartShell",
        "",
        "资源管理器崩溃后自动重启外壳",
        Hive::LocalMachine,
        kWinlogon,
        "AutoRestartShell",
        ValueType::Integer,
        "1",
        true
    ));
    list.push_back(make(
        "tweak.hungAppTimeout",
        "optimize_Toolwtime",
        "缩短无响应等待时间",
        Hive::CurrentUser,
        kDesktop,
        "HungAppTimeout",
        ValueType::String,
        "3000",
        false
    ));
    list.push_back(make(
        "tweak.waitToKillAppTimeout",
        "optimize_Toolwtime",
        "缩短结束无响应程序的等待时间",
        Hive::CurrentUser,
        kDesktop,
        "WaitToKillAppTimeout",
        ValueType::String,
        "10000",
        false
    ));
    list.push_back(make(
        "tweak.win32PriorityControl",
        "optimize_Pro",
        "优化处理器资源分配",
        Hive::LocalMachine,
        "SYSTEM\\CurrentControlSet\\Control\\PriorityControl",
        "Win32PriorityControl",
        ValueType::Integer,
        "38",
        true
    ));
    list.push_back(make(
        "tweak.bsodNoReboot",
        "optimize_BsReboot",
        "禁止系统在蓝屏时自动重启",
        Hive::LocalMachine,
        "SYSTEM\\CurrentControlSet\\Control\\CrashControl",
        "AutoReboot",
        ValueType::Integer,
        "0",
        false
    ));
    list.push_back(make(
        "tweak.remoteRegAccess",
        "optimize_EditReg",
        "禁止远程修改注册表",
        Hive::LocalMachine,
        "SYSTEM\\CurrentControlSet\\Control\\SecurePipeServers\\winreg",
        "remoteregaccess",
        ValueType::Integer,
        "1",
        true
    ));
    list.push_back(make(
        "tweak.aeDebugAuto",
        "optimize_AeDebug",
        "关闭系统自动调试",
        Hive::LocalMachine,
        "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AeDebug",
        "Auto",
        ValueType::String,
        "0",
        false
    ));
    list.push_back(make(
        "tweak.ntfsDisableLastAccess",
        "optimize_NTFS",
        "优化文件系统（NTFS）访问时间记录",
        Hive::LocalMachine,
        "SYSTEM\\CurrentControlSet\\Control\\FileSystem",
        "NtfsDisableLastAccessUpdate",
        ValueType::Integer,
        "1",
        false
    ));
    list.push_back(make(
        "tweak.optimalLayout",
        "optimize_Datasave",
        "自动优化数据储存布局",
        Hive::LocalMachine,
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\OptimalLayout",
        "EnableAutoLayout",
        ValueType::Integer,
        "1",
        false
    ));
    list.push_back(make(
        "tweak.maxCachedIcons",
        "optimize_MaxCached",
        "加大系统图标缓冲",
        Hive::LocalMachine,
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer",
        "Max Cached Icons",
        ValueType::String,
        "2000",
        true
    ));
    list.push_back(make(
        "tweak.ieMaxConnections",
        "optimize_IEConnect",
        "增加 IE 并发连接数",
        Hive::CurrentUser,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
        "MaxConnectionsPer1_0Server",
        ValueType::Integer,
        "10",
        false
    ));
    list.push_back(make(
        "tweak.driverSigning",
        "optimize_Drvcat",
        "允许安装未签名的驱动",
        Hive::CurrentUser,
        "Software\\Policies\\Microsoft\\Windows NT\\Driver Signing",
        "BehaviorOnFailedVerify",
        ValueType::Integer,
        "0",
        false
    ));
    list.push_back(make(
        "tweak.usbStorAutoRun",
        "optimize_USBauto",
        "关闭 USB 存储设备自动播放",
        Hive::LocalMachine,
        "SYSTEM\\CurrentControlSet\\Services\\USBSTOR",
        "AutoRun",
        ValueType::Integer,
        "1",
        true
    ));
    list.push_back(make(
        "tweak.winsQueryTimeout",
        "optimize_Winstime",
        "优化网络 WINS 名查询时间",
        Hive::LocalMachine,
        "SYSTEM\\CurrentControlSet\\services\\NetBT\\Parameters",
        "NameSrvQueryTimeout",
        ValueType::Integer,
        "3000",
        true
    ));
    list.push_back(make(
        "tweak.defaultTtl",
        "optimize_NetTTL",
        "优化默认报文寿命（TTL）",
        Hive::LocalMachine,
        "SYSTEM\\CurrentControlSet\\services\\Tcpip\\Parameters",
        "DefaultTTL",
        ValueType::Integer,
        "64",
        true
    ));
    list.push_back(make(
        "tweak.tcp1323Opts",
        "optimize_NetOpts",
        "启用传输单元缓冲区自动调整",
        Hive::LocalMachine,
        "SYSTEM\\CurrentControlSet\\services\\Tcpip\\Parameters",
        "Tcp1323Opts",
        ValueType::Integer,
        "1",
        true
    ));
    list.push_back(make(
        "tweak.shortcutSuffix",
        "optimize_Dellnk",
        "删除快捷方式角标字样",
        Hive::CurrentUser,
        kExplorer,
        "Link",
        ValueType::Binary,
        "00000000",
        true
    ));
    list.push_back(make(
        "tweak.mouseHoverTime",
        "optimize_Mousetime",
        "加快任务栏窗口预览显示速度",
        Hive::CurrentUser,
        "Control Panel\\Mouse",
        "mousehovertime",
        ValueType::String,
        "100",
        false
    ));
    list.push_back(make(
        "tweak.menuShowDelay",
        "optimize_MenuShow",
        "优化开始菜单显示速度",
        Hive::CurrentUser,
        "Control Panel\\desktop",
        "MenuShowDelay",
        ValueType::String,
        "0",
        false
    ));
    list.push_back(make(
        "tweak.disableCad",
        "",
        "允许程序直接操作（取消 Ctrl+Alt+Del 前置要求）",
        Hive::CurrentUser,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\system",
        "disablecad",
        ValueType::Integer,
        "1",
        false
    ));
    list.push_back(make(
        "tweak.bootOptimize",
        "optimize_Boot",
        "自动优化启动分区（启动时碎片整理）",
        Hive::LocalMachine,
        "SOFTWARE\\Microsoft\\Dfrg\\BootOptimizeFunction",
        "Enable",
        ValueType::String,
        "Y",
        false
    ));
    list.push_back(make(
        "tweak.noSimpleNetIdList",
        "",
        "关闭网络连接简单共享提示",
        Hive::CurrentUser,
        kExplorerPolicy,
        "NoSimpleNetIDList",
        ValueType::Integer,
        "1",
        false
    ));

    return list;
}

} // namespace

const std::vector<Tweak>& all()
{
    static const std::vector<Tweak> kList = build();
    return kList;
}

const Tweak* find(const std::string& id)
{
    const std::vector<Tweak>& list = all();
    const auto it = std::find_if(list.begin(), list.end(), [&id](const Tweak& t) {
        return t.id == id;
    });
    return it == list.end() ? nullptr : &*it;
}

const std::vector<std::string>& unresolvedLabels()
{
    // 语言文件里有、二进制里没有单行记录：SC 可能用命令或另有代码路径实现。
    // 这里如实列出来，由 --prepare 的 deferred 段报「尚未实现」而不是假装做了。
    static const std::vector<std::string> kLabels{
        "optimize_Aero",
        "optimize_RefreshFile",
        "optimize_Process",
        "optimize_AutoRefresh",
        "optimize_Cmsgsd",
        "optimize_Sdmsg",
    };
    return kLabels;
}

} // namespace osp::tweaks

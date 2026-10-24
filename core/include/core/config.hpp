#pragma once

#include <string>
#include <vector>

namespace osp::config {

// 配置模型的 schema 版本，结构不兼容变更时递增
inline constexpr int kSchemaVersion = 1;

struct SystemInfo
{
    std::string registeredName;   // 注册名称
    std::string registeredOrg;    // 注册单位
    std::string serialNumber;     // 序列号（需含 '-'）
    std::string computerName;     // 计算机名
    std::string computerNameMode; // fixed / random / prefixRandom
    int bootMenuTimeoutSec = 30;  // 启动菜单时间
};

struct FirstLogon
{
    bool runDeviceManager = false;
    bool rebootAfterRun = false;
    int resolutionX = 1024;
    int resolutionY = 768;
    int refreshRate = 60;
    int colorDepth = 32;
    bool syncIeHomepage = false;
    bool syncIeFavorites = false;
};

struct Optimization
{
    bool pinTaskbarItems = false;
    bool closeNumLock = false;
    bool enableHibernate = false;
    bool closeRebootDialog = false;
    std::string myDocumentsTarget; // 空表示不转移
    std::string pageFileTarget;    // 空表示不转移
    bool autoSetIp = false;
    std::string ipMode;          // fixed / random / range
    std::string networkLocation; // home / work / public
};

// 运行计划：5 个阶段 × 4 种任务类型
struct RunPlanTask
{
    std::string phase; // preinstall / install / postinstall / logon / desktop
    std::string type;  // run / reg / deleteFile / deleteDir
    std::string target;
    std::string arguments;
    std::string windowStyle; // normal / hidden / minimized / maximized
    std::string waitMode;    // wait / noWait / custom
    int waitSeconds = 0;
    std::string cleanup;     // keep / deleteFile
    std::string searchScope; // systemPath / usb / cdrom / allDrives
};

struct DeploySettings
{
    std::string module;     // grid / trueTrans / colorBar / image
    std::string background; // single / multi / systemDefault
    std::string titleText;
    bool skipOobe = false;
    bool autoCreateUser = false;
    std::string userName;
    std::string userGroup;
    bool blockVirtualMachine = false;
};

struct SysprepSettings
{
    std::string mode; // classic
    bool generateNewSid = true;
    bool uninstallDrivers = false;
    bool loadSrsDrivers = false;
    bool transferDesktopFiles = false;
    bool cleanTemp = false;
    bool cleanDllCache = false;
    bool autoLogon = false;
    int autoLogonCount = 1;
    std::string workgroup;
    std::string domain;
    std::string domainUser;
    std::string afterAction; // stay / reboot / shutdown
};

struct Config
{
    int schemaVersion = kSchemaVersion;
    SystemInfo systemInfo;
    FirstLogon firstLogon;
    Optimization optimization;
    std::vector<RunPlanTask> runPlan;
    DeploySettings deploy;
    SysprepSettings sysprep;
};

struct ParseResult
{
    bool ok = false;
    Config config{};
    std::string error{};
};

// 序列化为 UTF-8 JSON 文本（2 空格缩进，键序固定）
std::string toJson(const Config& cfg);

// 从 JSON 文本反序列化；缺字段取默认值，类型不符则返回 ok=false
ParseResult fromJson(const std::string& text);

} // namespace osp::config

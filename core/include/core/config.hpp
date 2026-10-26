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
    // 宽带（PPPoE）连接（SC 的 Cb_CreateConnection）：
    //   connectionMode = silent -> Rb_Silence_nc「静默创建[无界面]」
    //   connectionMode = show   -> Rb_Show_nc「倒计时创建[有界面]」
    bool createConnection = false;
    std::string connectionMode = "show";
    std::string connectionName = "宽带连接";
    std::string connectionUser;
    std::string connectionPassword;
    int connectionCountdownSec = 10;
};

struct Optimization
{
    bool pinTaskbarItems = false;
    bool closeNumLock = false;
    bool enableHibernate = false;
    bool closeRebootDialog = false;
    // 我的文档 / IE 收藏夹转移（SC 的 Cb_MoveDocuments / Cb_MoveFavorites）：
    //   smart=true  -> Rb_SmartMove*：自动选剩余最大的分区，条件不成立则不转移
    //   smart=false -> Rb_DiyMove*：转移到指定目录，条件不成立则退回智能转移
    bool moveDocuments = false;
    bool documentsSmart = true;
    std::string myDocumentsTarget; // 指定转移目录（documentsSmart=false 时用）
    bool moveFavorites = false;
    bool favoritesSmart = true;
    std::string favoritesTarget;
    // 任务栏固定 / 取消固定（SC 的 Cb_Locktaskbar / Cb_Unlocktaskbar）。
    // 只接受用户自己填的文件清单 —— SC 用这两个开关推自家推广快捷方式，
    // 那是宪章 §7 明令禁止的，我们不预置任何条目。
    std::vector<std::string> lockTaskbarFiles;
    std::vector<std::string> unlockTaskbarFiles;
    // 同步封装前 IE 主页（SC 的 Cb_Saveiehp）；ieHomePage 空则取当前用户的主页
    bool saveIeHomePage = false;
    std::string ieHomePage;
    // 默认用户配置单元（NTUSER.DAT）的路径。空 = <系统盘>\Users\Default\NTUSER.DAT。
    // 显式给出来是为了两种真实场景：封装备份盘上目录不在系统盘，以及测试时指向副本。
    std::string defaultUserHive;
    std::string pageFileTarget; // 空表示不转移
    // 智能转移虚拟内存（对齐 SC 的 Rb_SmartMovePageFile：自动选剩余空间最大的分区）。
    // 注意这**不是**「算大小」：SC 的 MovePageFile 只搬位置，大小仍交给系统管理
    // （`0 0`）。证据见 15_spec/功能规格.md §二「智能转移…虚拟内存（智能选最大剩余分区
    // 或 指定目录）」与 Cb_MovePageFileDrive / Rb_SmartMovePageFile。
    bool autoPagefileDrive = false;
    // SC 的系统优化项（33 条里的 27 条有原始数据，见 docs/SC功能对照.md）。
    // 空 = 一项都不做；里面是 tweaks::Tweak::id。
    std::vector<std::string> sysTweaks;
    // 自动设置 IP（SC 的 Cb_AutoSetip + Rb_Setip / Rb_SetRandomip / Lab_ips）
    bool autoSetIp = false;
    std::string ipMode;          // fixed / random / range
    std::string ipAddress;       // fixed：指定 IP
    std::string ipRangeStart;    // range：起始 IP（Lab_Startip）
    std::string ipRangeEnd;      // range：结束 IP（Lab_Endip）
    std::string subnetMask;      // 子网掩码（Lab_SubntMask）
    std::string gateway;         // 默认网关（Lab_DefaultGateway）
    std::string dnsPreferred;    // 首选 DNS（Lab_PreferredDNS）
    std::string dnsAlternate;    // 备用 DNS（Lab_AltenateDNS）
    std::string networkLocation; // home / work / public
    // M7b 新增（可选，默认值不变）：对位 DFM 里没有宿主的两个字段
    bool clearSrsDrivers = false; // 清理多余的 SRS 驱动（Cb_ClearSrs）
    bool setResolution = false;   // 智能设置分辨率（Cb_SetResolution）
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
    std::string searchScope; // systemPath / usbDrive / cdRom / allDrives
    // U 盘 / 光盘按卷标匹配；留空则退回按驱动器类型匹配（见 core/runplan）
    std::string expectedVolumeLabel;
    // 等待超时后的动作：kill 终止该进程 / continue 继续执行后续命令；默认 continue
    std::string timeoutAction;
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
    // M7b 新增（可选，默认值不变）：部署过程页的「部署设置」组。
    // 只承载配置，图形化部署画面按宪章「Direct2D 自绘与换肤后置」不做。
    std::string backgroundImage;  // 部署背景图路径（Ed_PicPath）
    int imageSwitchSeconds = 10;  // 多图切换速度，秒（Ed_PicSpeed，图纸默认 10）
    int deployResolutionX = 0;    // 部署分辨率宽，0 = 不设置（Ed_DeployResolution_Width）
    int deployResolutionY = 0;    // 部署分辨率高，0 = 不设置（Ed_DeployResolution_Height）
    std::string fontColor;        // 部署文字颜色（Ed_DeployFontcolour）
    bool verifyIntegrity = false; // 校验文件防篡改，用户态校验（Cb_Tampering）
    // 防篡改清单（SC 的 Items_Tampering / Items_FileMd5）：部署时逐个校验 MD5。
    // MD5 在生成运行计划时算好写进去，部署端只比对，不再信任现场文件。
    struct TamperEntry
    {
        std::string path; // 绝对路径或相对计划目录
        std::string md5;  // 32 位小写十六进制
    };
    std::vector<TamperEntry> tamperFiles;
    // 被劫持策略（SC 的 SA_Rb_hijack_type1/type2）：continue / terminate
    std::string hijackMode = "terminate";
    std::string hijackTip;            // 被劫持时弹给用户的提示（SA_Lab_ShowTip）
    bool enableAdministrator = false; // 开启 administrator（Cb_Admin）
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
    // 自动登录密码。空密码时 unattend 的 <AutoLogon> 会被 Winlogon 静默忽略，
    // 必须同时写 Winlogon 注册表（AutoAdminLogon + 空 DefaultPassword）才生效。
    std::string autoLogonPassword;
    std::string workgroup;
    std::string domain;
    std::string domainUser;
    std::string afterAction; // stay / reboot / shutdown
    // M7b 新增（可选，默认值不变）
    std::string domainPassword;      // 域用户密码（Ed_Domain_pw）
    bool perServerLicensing = false; // 每服务器授权（Rb_EverySer）
    int maxConnections = 5;          // 同时连接数（Se_Serconut，图纸默认 5）
};

struct OemSettings
{
    // 一个字段都不填时整段不生成，所以默认全空
    std::string manufacturer;    // -> <Manufacturer>（Win7 显示；Win10+ 已弃用但仍读取）
    std::string model;           // -> <Model>
    std::string supportHours;    // -> <SupportHours>（Win7）
    std::string supportPhone;    // -> <SupportPhone>（Win7）
    std::string supportProvider; // -> <SupportProvider>（Win10+ 显示名）
    std::string supportUrl;      // -> <SupportURL>
    // SC 的 SA_Lab_oembgpic「遇品牌电脑则设置相应品牌桌面壁纸」：
    // 用户自己的图片，复制进映像后写进默认用户的桌面壁纸设置。
    // 本仓库不含任何品牌素材（宪章 §5 / §7），路径必须由用户提供。
    std::string wallpaper;
    // SC 的 SA_Lab_oemDeploy「遇品牌电脑则改变相应品牌部署图片」：
    // 部署界面的图片。deploy.backgroundImage 为空时用它。
    std::string deployImage;
    std::string source; // 来源说明，仅用于报告，不参与生成
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
    OemSettings oem;
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

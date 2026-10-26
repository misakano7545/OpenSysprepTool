#pragma once

// OEM 信息：识别本机厂商 + 生成 unattend 的 <OEMInformation>
//
// 对标 SC 的「OEM 智能识别」。它的数据是一棵 Scoem\<品牌>\ 目录，每个品牌两份
// 配置文件：nt6.ini（Vista 之后）与 nt5.ini（XP 时代）。nt6.ini 的形状是
//
//   [Support]        Url= / Tel= / Stime=
//   [General_CHS]    Manufacturer= / Model=
//   [General_EN]     Manufacturer= / Model=
//
// 这五个字段正好对上 unattend 的 OEMInformation 子元素，所以「导入 OEM 档案」
// 与「写进应答文件」是同一张表，不需要中间翻译层。本模块只认这个**格式**，
// 不搬运 SC 的品牌数据（那些图片与文案各有其权利人，见宪章 §6.1）。
//
// 注意 Win10 之后 Manufacturer / Model / Logo / SupportHours / SupportPhone
// 在文档里标注为已弃用，改为 SupportProvider + SupportURL；但 Win7（本项目
// 最低目标）仍然只认老的那几个。所以两边都写：老字段给 Win7，新字段给 Win10+。

#include <string>
#include <vector>

namespace osp::oem {

// 一台机器的 BIOS 标识，来自 HKLM\HARDWARE\DESCRIPTION\System\BIOS。
// 走注册表而不是 WMI：这是系统启动时就填好的静态值，读一次几毫秒，
// 而 WMI 的 Win32_ComputerSystem 在某些机器上要几百毫秒甚至更久。
struct BiosInfo
{
    bool ok = false;
    std::string manufacturer; // SystemManufacturer，如 "LENOVO"
    std::string productName;  // SystemProductName，如 "20UD..."
    std::string version;      // SystemVersion
    std::string family;       // SystemFamily
    std::string biosVendor;   // BIOSVendor
    std::string biosVersion;  // BIOSVersion
    std::string error;
};

BiosInfo readBios();

// OEM 档案：字段与 unattend OEMInformation 一一对应
struct Profile
{
    std::string manufacturer;    // -> <Manufacturer>（Win7 显示用；Win10+ 已弃用但仍被读取）
    std::string model;           // -> <Model>
    std::string supportHours;    // -> <SupportHours>（Win7）
    std::string supportPhone;    // -> <SupportPhone>（Win7）
    std::string supportProvider; // -> <SupportProvider>（Win10+ 显示名，优先于此）
    std::string supportUrl;      // -> <SupportURL>
    std::string source;          // 来源说明：哪份 ini、选了什么语言
};

// 从 SC 的 nt6.ini / nt5.ini 读档案。
// language 取 "CHS" 或 "EN"：优先 [General_<language>]，取不到回退 [General]。
// nt5.ini 只有 [General]，所以一条路径同时覆盖两种代次。
struct LoadResult
{
    bool ok = false;
    std::string error;
    std::vector<std::string> warnings;
    Profile profile;
};

LoadResult loadIni(const std::string& utf8Path, const std::string& language = "CHS");

// 没有 ini 时的兜底：直接用 BIOS 的厂商与型号，只填 manufacturer/model。
Profile fromBios(const BiosInfo& bios);

// 档案是否有可写内容（全空则不该生成 <OEMInformation>）
bool hasContent(const Profile& profile);

// 按 SC 的品牌目录布局推断品牌：dir 下每个子目录视为一个品牌，
// 逐个读它的 nt6.ini（没有就试 nt5.ini），用 Manufacturer 与 BIOS 的厂商名做匹配。
// 返回匹配到的品牌目录名；匹配不上返回空。
struct DetectResult
{
    bool ok = false;
    std::string brand;   // 目录名，如 "Lenovo"
    std::string iniPath; // 命中的 ini 绝对路径
    Profile profile;
    std::vector<std::string> tried;
    std::vector<std::string> warnings;
    std::string error;
};

DetectResult
detectFromSkinDir(const std::string& utf8SkinDir, const BiosInfo& bios, const std::string& language = "CHS");

} // namespace osp::oem

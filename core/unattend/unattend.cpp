#include "core/unattend.hpp"

#include <pugixml.hpp>

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace osp::unattend {
namespace {

// 参考：ADK Unattend.chm
//   Microsoft-Windows-Shell-Setup / -Deployment / -UnattendedJoin 组件页
constexpr const char* kProcessorArchitecture = "amd64";
constexpr const char* kPublicKeyToken = "31bf3856ad364e35";
constexpr const char* kLanguage = "neutral";
constexpr const char* kVersionScope = "nonSxS";

// 参考 ComputerName 页面列出的禁用字符
constexpr const char* kForbiddenNameChars = "{}|~[]\\^':;<=>?@!\"#$%`()+/.,*&";
constexpr std::size_t kMaxComputerNameBytes = 15;

bool isForbiddenNameChar(char c)
{
    return std::strchr(kForbiddenNameChars, c) != nullptr;
}

std::uint32_t fnv1a(const std::string& text)
{
    std::uint32_t hash = 2166136261u;
    for (unsigned char c : text)
    {
        hash ^= static_cast<std::uint32_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

bool validateComputerName(const std::string& name, std::string& error)
{
    if (name.empty())
    {
        error = "ComputerName is empty";
        return false;
    }

    if (name.size() > kMaxComputerNameBytes)
    {
        error = "ComputerName exceeds 15 bytes: \"" + name + "\" (" + std::to_string(name.size()) + " bytes)";
        return false;
    }

    bool allDigits = true;
    for (char c : name)
    {
        if (c == ' ')
        {
            error = "ComputerName contains a space: \"" + name + "\"";
            return false;
        }
        if (isForbiddenNameChar(c))
        {
            error = std::string("ComputerName contains a forbidden character '") + c + "': \"" + name + "\"";
            return false;
        }
        if (c < '0' || c > '9')
        {
            allDigits = false;
        }
    }

    if (allDigits)
    {
        error = "ComputerName cannot consist of digits only: \"" + name + "\"";
        return false;
    }

    return true;
}

pugi::xml_node addComponent(pugi::xml_node settings, const char* name)
{
    pugi::xml_node component = settings.append_child("component");
    component.append_attribute("name") = name;
    component.append_attribute("processorArchitecture") = kProcessorArchitecture;
    component.append_attribute("publicKeyToken") = kPublicKeyToken;
    component.append_attribute("language") = kLanguage;
    component.append_attribute("versionScope") = kVersionScope;
    return component;
}

pugi::xml_node addChild(pugi::xml_node parent, const char* name)
{
    return parent.append_child(name);
}

void addTextChild(pugi::xml_node parent, const char* name, const std::string& value)
{
    addChild(parent, name).text().set(value.c_str());
}

void addTextChild(pugi::xml_node parent, const char* name, int value)
{
    addChild(parent, name).text().set(value);
}

void addTextChild(pugi::xml_node parent, const char* name, bool value)
{
    addChild(parent, name).text().set(value ? "true" : "false");
}

// ---------------------------------------------------------------- AutoLogon 密码

// unattend 的 <AutoLogon><Password><Value> 要求
// base64(UTF-16LE(password + "Password"))，且 PlainText 必须是 false。
std::string base64Encode(const std::vector<std::uint8_t>& bytes)
{
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 2 < bytes.size())
    {
        const std::uint32_t chunk = (static_cast<std::uint32_t>(bytes[i]) << 16) |
            (static_cast<std::uint32_t>(bytes[i + 1]) << 8) | static_cast<std::uint32_t>(bytes[i + 2]);
        out.push_back(table[(chunk >> 18) & 0x3F]);
        out.push_back(table[(chunk >> 12) & 0x3F]);
        out.push_back(table[(chunk >> 6) & 0x3F]);
        out.push_back(table[chunk & 0x3F]);
        i += 3;
    }
    const std::size_t rest = bytes.size() - i;
    if (rest == 1)
    {
        const std::uint32_t chunk = static_cast<std::uint32_t>(bytes[i]) << 16;
        out.push_back(table[(chunk >> 18) & 0x3F]);
        out.push_back(table[(chunk >> 12) & 0x3F]);
        out.append("==");
    }
    else if (rest == 2)
    {
        const std::uint32_t chunk =
            (static_cast<std::uint32_t>(bytes[i]) << 16) | (static_cast<std::uint32_t>(bytes[i + 1]) << 8);
        out.push_back(table[(chunk >> 18) & 0x3F]);
        out.push_back(table[(chunk >> 12) & 0x3F]);
        out.push_back(table[(chunk >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

std::string encodeAutoLogonPassword(const std::string& utf8Password)
{
    const std::string withSuffix = utf8Password + "Password";
    std::vector<std::uint8_t> utf16le;
    const int wide =
        MultiByteToWideChar(CP_UTF8, 0, withSuffix.c_str(), static_cast<int>(withSuffix.size()), nullptr, 0);
    if (wide <= 0)
    {
        return std::string();
    }
    std::wstring buffer(static_cast<std::size_t>(wide), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, withSuffix.c_str(), static_cast<int>(withSuffix.size()), buffer.data(), wide
    );
    utf16le.reserve(buffer.size() * 2);
    for (wchar_t ch : buffer)
    {
        utf16le.push_back(static_cast<std::uint8_t>(ch & 0xFF));
        utf16le.push_back(static_cast<std::uint8_t>((ch >> 8) & 0xFF));
    }
    return base64Encode(utf16le);
}

const char* networkLocationValue(const std::string& raw)
{
    if (raw == "home")
        return "Home";
    if (raw == "work")
        return "Work";
    if (raw == "public")
        return "Other";
    return nullptr;
}

// 落点依据 ADK 离线参考 Unattend.chm：
//   FirstLogonCommands -> SynchronousCommand：「run the first time a user logs on」，
//     「after logon but before the user sees the desktop」，逐条同步等待 -> 登录时
//   LogonCommands     -> AsynchronousCommand：「the first time that a user logs onto
//     the computer」，「The computer does not wait for one command to finish」
//     （不推迟桌面出现）-> 进桌面
// 两个元素的合法 pass 都是 oobeSystem。
bool isFirstLogonPhase(const std::string& phase)
{
    return phase == "logon";
}

bool isLogonCommandsPhase(const std::string& phase)
{
    return phase == "desktop";
}

// 一个字段都没填就不生成 <OEMInformation>：空的 OEM 段会让 OOBE 显示一个
// 没有厂商名的「支持」入口，比不写更糟。
bool hasOemContent(const osp::config::OemSettings& oem)
{
    return !oem.manufacturer.empty() || !oem.model.empty() || !oem.supportHours.empty() ||
        !oem.supportPhone.empty() || !oem.supportProvider.empty() || !oem.supportUrl.empty();
}

std::string buildCommandLine(const osp::config::RunPlanTask& task)
{
    if (task.arguments.empty())
    {
        return task.target;
    }
    return task.target + " " + task.arguments;
}

// SynchronousCommand 的 Order 必须从 1 连续递增
struct CommandList
{
    std::vector<std::string> commandLines;
    std::vector<std::string> descriptions;
};

} // namespace

const std::vector<const char*>& shellSetupSpecializeOrder()
{
    static const std::vector<const char*> kOrder{
        "ComputerName", "ProductKey", "RegisteredOrganization", "RegisteredOwner"
    };
    return kOrder;
}

const std::vector<const char*>& shellSetupOobeSystemOrder()
{
    // 参考 Microsoft-Windows-Shell-Setup 的 oobeSystem 子元素，按文档的字母序：
    // AutoLogon, Display, FirstLogonCommands, LogonCommands, OEMInformation, OOBE, UserAccounts
    static const std::vector<const char*> kOrder{
        "AutoLogon",
        "Display",
        "FirstLogonCommands",
        "LogonCommands",
        "OEMInformation",
        "OOBE",
        "UserAccounts"
    };
    return kOrder;
}

const std::vector<const char*>& oemInformationChildOrder()
{
    // OEMInformation 的子元素，同样按文档字母序。
    // Manufacturer / Model / SupportHours / SupportPhone 在 Win10 文档里标为已弃用，
    // 但 Win7（本项目最低目标）只认它们，且 Win10+ 在缺 SupportProvider 时仍会用
    // Manufacturer，所以两代字段一起写。
    static const std::vector<const char*> kOrder{
        "Manufacturer", "Model", "SupportHours", "SupportPhone", "SupportProvider", "SupportURL"
    };
    return kOrder;
}

const std::vector<const char*>& asynchronousCommandChildOrder()
{
    // 参考 AsynchronousCommand 页：CommandLine / Description / Order，
    // 同页另有一个 RequiresUserInput 标注为 "This setting is not used."，故不生成
    static const std::vector<const char*> kOrder{"CommandLine", "Description", "Order"};
    return kOrder;
}

const std::vector<const char*>& oobeChildOrder()
{
    static const std::vector<const char*> kOrder{
        "HideEULAPage",
        "HideLocalAccountScreen",
        "HideOEMRegistrationScreen",
        "HideOnlineAccountScreens",
        "HideWirelessSetupInOOBE",
        "NetworkLocation",
        "OEMAppID",
        "ProtectYourPC"
    };
    return kOrder;
}

const std::vector<const char*>& autoLogonChildOrder()
{
    static const std::vector<const char*> kOrder{"Domain", "Enabled", "LogonCount", "Password", "Username"};
    return kOrder;
}

const std::vector<const char*>& displayChildOrder()
{
    static const std::vector<const char*> kOrder{
        "ColorDepth", "DPI", "HorizontalResolution", "RefreshRate", "VerticalResolution"
    };
    return kOrder;
}

const std::vector<const char*>& synchronousCommandChildOrder()
{
    static const std::vector<const char*> kOrder{"CommandLine", "Description", "Order", "RequiresUserInput"};
    return kOrder;
}

const std::vector<const char*>& localAccountChildOrder()
{
    static const std::vector<const char*> kOrder{"Description", "DisplayName", "Group", "Name", "Password"};
    return kOrder;
}

bool resolveComputerName(const osp::config::SystemInfo& info, std::string& out, std::string& error)
{
    if (info.computerNameMode == "random")
    {
        // 参考 ComputerName 页面：星号表示由系统随机生成
        out = "*";
        return true;
    }

    if (info.computerNameMode == "prefixRandom")
    {
        const std::uint32_t hash =
            fnv1a(info.computerName + "|" + info.serialNumber + "|" + info.registeredName);
        static const char kHex[] = "0123456789ABCDEF";
        std::string name = info.computerName;
        for (int shift = 0; shift < 16; shift += 4)
        {
            name.push_back(kHex[(hash >> shift) & 0xFu]);
        }
        out = name;
        return validateComputerName(out, error);
    }

    out = info.computerName;
    return validateComputerName(out, error);
}

GenResult generate(const osp::config::Config& cfg)
{
    GenResult result;

    std::string computerName;
    if (!resolveComputerName(cfg.systemInfo, computerName, result.error))
    {
        return result;
    }

    pugi::xml_document doc;
    pugi::xml_node declaration = doc.append_child(pugi::node_declaration);
    declaration.append_attribute("version") = "1.0";
    declaration.append_attribute("encoding") = "utf-8";

    pugi::xml_node root = doc.append_child("unattend");
    root.append_attribute("xmlns") = kNamespace;
    root.append_attribute("xmlns:wcm") = kWcmNamespace;

    // ---------------------------------------------------------- specialize
    pugi::xml_node specialize = root.append_child("settings");
    specialize.append_attribute("pass") = "specialize";

    {
        pugi::xml_node component = addComponent(specialize, "Microsoft-Windows-Shell-Setup");
        // 顺序按 shellSetupSpecializeOrder()
        addTextChild(component, "ComputerName", computerName);

        if (!cfg.systemInfo.serialNumber.empty())
        {
            pugi::xml_node productKey = addChild(component, "ProductKey");
            addTextChild(productKey, "Key", cfg.systemInfo.serialNumber);
        }

        if (!cfg.systemInfo.registeredOrg.empty())
        {
            addTextChild(component, "RegisteredOrganization", cfg.systemInfo.registeredOrg);
        }
        if (!cfg.systemInfo.registeredName.empty())
        {
            addTextChild(component, "RegisteredOwner", cfg.systemInfo.registeredName);
        }
    }

    {
        pugi::xml_node component = addComponent(specialize, "Microsoft-Windows-Deployment");

        // 空密码自动登录：unattend 的 <AutoLogon> 在密码为空时会被 Winlogon 静默忽略，
        // 只能改走 Winlogon 注册表。DefaultPassword 必须**存在**（空 REG_SZ 也算），
        // 否则 AutoAdminLogon 同样不生效。
        std::vector<std::string> commands;
        if (cfg.sysprep.autoLogon && cfg.sysprep.autoLogonPassword.empty())
        {
            const std::string userName = cfg.deploy.userName.empty() ? "Administrator" : cfg.deploy.userName;
            const std::string winlogon = "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";
            commands.push_back("reg add \"" + winlogon + "\" /v AutoAdminLogon /t REG_SZ /d 1 /f");
            commands.push_back(
                "reg add \"" + winlogon + "\" /v DefaultUserName /t REG_SZ /d \"" + userName + "\" /f"
            );
            commands.push_back("reg add \"" + winlogon + "\" /v DefaultPassword /t REG_SZ /d \"\" /f");
            if (cfg.sysprep.autoLogonCount > 0)
            {
                commands.push_back(
                    "reg add \"" + winlogon + "\" /v AutoLogonCount /t REG_DWORD /d " +
                    std::to_string(cfg.sysprep.autoLogonCount) + " /f"
                );
            }
        }

        for (const auto& task : cfg.runPlan)
        {
            if (task.phase == "postinstall")
            {
                commands.push_back(buildCommandLine(task));
            }
        }

        if (!commands.empty())
        {
            pugi::xml_node runSync = addChild(component, "RunSynchronous");
            int order = 1;
            for (const std::string& commandLine : commands)
            {
                pugi::xml_node command = addChild(runSync, "RunSynchronousCommand");
                command.append_attribute("wcm:action") = "add";
                // 顺序按 synchronousCommandChildOrder() 的子集：Order, Path
                addTextChild(command, "Order", order);
                addTextChild(command, "Path", commandLine);
                ++order;
            }
        }
    }

    if (!cfg.sysprep.workgroup.empty() || !cfg.sysprep.domain.empty())
    {
        pugi::xml_node component = addComponent(specialize, "Microsoft-Windows-UnattendedJoin");
        pugi::xml_node identification = addChild(component, "Identification");
        // 参考 Identification 页：JoinDomain 与 JoinWorkgroup 互斥，只能出现一个
        if (!cfg.sysprep.domain.empty())
        {
            addTextChild(identification, "JoinDomain", cfg.sysprep.domain);
        }
        else
        {
            addTextChild(identification, "JoinWorkgroup", cfg.sysprep.workgroup);
        }
    }

    // --------------------------------------------------------- oobeSystem
    pugi::xml_node oobeSystem = root.append_child("settings");
    oobeSystem.append_attribute("pass") = "oobeSystem";

    {
        pugi::xml_node component = addComponent(oobeSystem, "Microsoft-Windows-Shell-Setup");

        // 顺序按 shellSetupOobeSystemOrder()：AutoLogon -> Display ->
        // FirstLogonCommands -> OOBE -> UserAccounts
        if (cfg.sysprep.autoLogon)
        {
            pugi::xml_node autoLogon = addChild(component, "AutoLogon");
            // 顺序按 autoLogonChildOrder()：Domain -> Enabled -> LogonCount -> Password -> Username
            addTextChild(autoLogon, "Enabled", true);
            addTextChild(autoLogon, "LogonCount", cfg.sysprep.autoLogonCount);
            if (!cfg.sysprep.autoLogonPassword.empty())
            {
                // Winlogon 会静默忽略没有 <Password> 的 <AutoLogon>，密码必须按
                // base64(UTF-16LE(密码 + "Password")) 写进 Value，且 PlainText=false
                pugi::xml_node password = addChild(autoLogon, "Password");
                addTextChild(password, "Value", encodeAutoLogonPassword(cfg.sysprep.autoLogonPassword));
                addTextChild(password, "PlainText", false);
            }
            const std::string userName = cfg.deploy.userName.empty() ? "Administrator" : cfg.deploy.userName;
            addTextChild(autoLogon, "Username", userName);
        }

        if (cfg.firstLogon.resolutionX > 0 && cfg.firstLogon.resolutionY > 0)
        {
            pugi::xml_node display = addChild(component, "Display");
            // 顺序按 displayChildOrder()
            if (cfg.firstLogon.colorDepth > 0)
            {
                addTextChild(display, "ColorDepth", cfg.firstLogon.colorDepth);
            }
            addTextChild(display, "HorizontalResolution", cfg.firstLogon.resolutionX);
            if (cfg.firstLogon.refreshRate > 0)
            {
                addTextChild(display, "RefreshRate", cfg.firstLogon.refreshRate);
            }
            addTextChild(display, "VerticalResolution", cfg.firstLogon.resolutionY);
        }

        // 登录时 -> FirstLogonCommands（桌面出现前同步执行）
        CommandList commands;
        if (cfg.firstLogon.runDeviceManager)
        {
            commands.commandLines.emplace_back("devmgmt.msc");
            commands.descriptions.emplace_back("运行设备管理器");
        }
        for (const auto& task : cfg.runPlan)
        {
            if (isFirstLogonPhase(task.phase))
            {
                commands.commandLines.push_back(buildCommandLine(task));
                commands.descriptions.emplace_back(task.phase);
            }
        }

        // 宽带连接（SC 的 Rb_Show_nc「倒计时创建[有界面]」）：首次进桌面时弹创建向导。
        // rasphone -a 就是「添加新连接」向导本身，用户点几下就建好——这就是 SC
        // 那个「询问是否创建」的行为，不需要我们自己画一个倒计时的窗口。
        if (cfg.firstLogon.createConnection &&
            (cfg.firstLogon.connectionMode.empty() || cfg.firstLogon.connectionMode == "show"))
        {
            const std::string name = cfg.firstLogon.connectionName.empty() ? std::string("宽带连接")
                                                                           : cfg.firstLogon.connectionName;
            commands.commandLines.push_back("rasphone.exe -a \"" + name + "\"");
            commands.descriptions.emplace_back("创建宽带连接（询问用户）");
        }

        if (!commands.commandLines.empty())
        {
            pugi::xml_node firstLogon = addChild(component, "FirstLogonCommands");
            int order = 1;
            for (std::size_t i = 0; i < commands.commandLines.size(); ++i)
            {
                pugi::xml_node command = addChild(firstLogon, "SynchronousCommand");
                command.append_attribute("wcm:action") = "add";
                // 顺序按 synchronousCommandChildOrder()
                addTextChild(command, "CommandLine", commands.commandLines[i]);
                addTextChild(command, "Description", commands.descriptions[i]);
                addTextChild(command, "Order", order);
                ++order;
            }
        }

        // 进桌面 -> LogonCommands（异步执行，不推迟桌面出现）
        CommandList logonCommands;
        for (const auto& task : cfg.runPlan)
        {
            if (isLogonCommandsPhase(task.phase))
            {
                logonCommands.commandLines.push_back(buildCommandLine(task));
                logonCommands.descriptions.emplace_back(task.phase);
            }
        }

        if (!logonCommands.commandLines.empty())
        {
            pugi::xml_node logon = addChild(component, "LogonCommands");
            int order = 1;
            for (std::size_t i = 0; i < logonCommands.commandLines.size(); ++i)
            {
                pugi::xml_node command = addChild(logon, "AsynchronousCommand");
                command.append_attribute("wcm:action") = "add";
                // 顺序按 asynchronousCommandChildOrder()
                addTextChild(command, "CommandLine", logonCommands.commandLines[i]);
                addTextChild(command, "Description", logonCommands.descriptions[i]);
                addTextChild(command, "Order", order);
                ++order;
            }
        }

        // OEM 信息：文档字母序里排在 LogonCommands 与 OOBE 之间
        if (hasOemContent(cfg.oem))
        {
            pugi::xml_node oem = addChild(component, "OEMInformation");
            // 顺序按 oemInformationChildOrder()
            if (!cfg.oem.manufacturer.empty())
            {
                addTextChild(oem, "Manufacturer", cfg.oem.manufacturer);
            }
            if (!cfg.oem.model.empty())
            {
                addTextChild(oem, "Model", cfg.oem.model);
            }
            if (!cfg.oem.supportHours.empty())
            {
                addTextChild(oem, "SupportHours", cfg.oem.supportHours);
            }
            if (!cfg.oem.supportPhone.empty())
            {
                addTextChild(oem, "SupportPhone", cfg.oem.supportPhone);
            }
            if (!cfg.oem.supportProvider.empty())
            {
                addTextChild(oem, "SupportProvider", cfg.oem.supportProvider);
            }
            if (!cfg.oem.supportUrl.empty())
            {
                addTextChild(oem, "SupportURL", cfg.oem.supportUrl);
            }
        }

        // SC 的 Cb_SetNetWork 只有「自动设置网络位置」一个勾选框：勾了就按工作网络写。
        // networkLocation 显式给了值时以它为准（命令行 / 高级用法）。
        const std::string requested =
            cfg.optimization.networkLocation.empty() && cfg.optimization.autoNetworkLocation
            ? std::string("work")
            : cfg.optimization.networkLocation;
        const char* location = networkLocationValue(requested);
        if (cfg.deploy.skipOobe || location != nullptr)
        {
            pugi::xml_node oobe = addChild(component, "OOBE");
            // 顺序按 oobeChildOrder()
            if (cfg.deploy.skipOobe)
            {
                addTextChild(oobe, "HideEULAPage", true);
                addTextChild(oobe, "HideOnlineAccountScreens", true);
                addTextChild(oobe, "HideWirelessSetupInOOBE", true);
            }
            if (location != nullptr)
            {
                // 显式构造 std::string：否则 const char* 会优先匹配 bool 重载
                addTextChild(oobe, "NetworkLocation", std::string(location));
            }
        }

        if (cfg.deploy.autoCreateUser && !cfg.deploy.userName.empty())
        {
            pugi::xml_node userAccounts = addChild(component, "UserAccounts");
            pugi::xml_node localAccounts = addChild(userAccounts, "LocalAccounts");
            pugi::xml_node account = addChild(localAccounts, "LocalAccount");
            account.append_attribute("wcm:action") = "add";
            // 顺序按 localAccountChildOrder()
            if (!cfg.deploy.userGroup.empty())
            {
                addTextChild(account, "Group", cfg.deploy.userGroup);
            }
            addTextChild(account, "Name", cfg.deploy.userName);
        }
    }

    std::ostringstream stream;
    doc.save(stream, "  ", pugi::format_default | pugi::format_indent);
    result.xml = stream.str();
    result.ok = true;
    return result;
}

bool writeToFile(const std::string& utf8Path, const std::string& xml, std::string& error)
{
    std::ofstream out(utf8Path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        error = "cannot open for writing: " + utf8Path;
        return false;
    }

    out.write(xml.data(), static_cast<std::streamsize>(xml.size()));
    out.close();

    if (!out)
    {
        error = "write failed: " + utf8Path;
        return false;
    }
    return true;
}

} // namespace osp::unattend

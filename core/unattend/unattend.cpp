#include "core/unattend.hpp"

#include <pugixml.hpp>

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
        error = "ComputerName exceeds 15 bytes: \"" + name + "\" (" +
                std::to_string(name.size()) + " bytes)";
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
            error = std::string("ComputerName contains a forbidden character '") + c +
                    "': \"" + name + "\"";
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

const char* networkLocationValue(const std::string& raw)
{
    if (raw == "home") return "Home";
    if (raw == "work") return "Work";
    if (raw == "public") return "Other";
    return nullptr;
}

bool isFirstLogonPhase(const std::string& phase)
{
    return phase == "desktop" || phase == "logon";
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
        "ComputerName", "ProductKey", "RegisteredOrganization", "RegisteredOwner"};
    return kOrder;
}

const std::vector<const char*>& shellSetupOobeSystemOrder()
{
    static const std::vector<const char*> kOrder{
        "AutoLogon", "Display", "FirstLogonCommands", "OOBE", "UserAccounts"};
    return kOrder;
}

const std::vector<const char*>& oobeChildOrder()
{
    static const std::vector<const char*> kOrder{
        "HideEULAPage", "HideLocalAccountScreen", "HideOEMRegistrationScreen",
        "HideOnlineAccountScreens", "HideWirelessSetupInOOBE", "NetworkLocation",
        "OEMAppID", "ProtectYourPC"};
    return kOrder;
}

const std::vector<const char*>& autoLogonChildOrder()
{
    static const std::vector<const char*> kOrder{
        "Domain", "Enabled", "LogonCount", "Password", "Username"};
    return kOrder;
}

const std::vector<const char*>& displayChildOrder()
{
    static const std::vector<const char*> kOrder{
        "ColorDepth", "DPI", "HorizontalResolution", "RefreshRate", "VerticalResolution"};
    return kOrder;
}

const std::vector<const char*>& synchronousCommandChildOrder()
{
    static const std::vector<const char*> kOrder{
        "CommandLine", "Description", "Order", "RequiresUserInput"};
    return kOrder;
}

const std::vector<const char*>& localAccountChildOrder()
{
    static const std::vector<const char*> kOrder{
        "Description", "DisplayName", "Group", "Name", "Password"};
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
        std::vector<const osp::config::RunPlanTask*> postInstall;
        for (const auto& task : cfg.runPlan)
        {
            if (task.phase == "postinstall")
            {
                postInstall.push_back(&task);
            }
        }

        if (!postInstall.empty())
        {
            pugi::xml_node runSync = addChild(component, "RunSynchronous");
            int order = 1;
            for (const osp::config::RunPlanTask* task : postInstall)
            {
                pugi::xml_node command = addChild(runSync, "RunSynchronousCommand");
                command.append_attribute("wcm:action") = "add";
                // 顺序按 synchronousCommandChildOrder() 的子集：Order, Path
                addTextChild(command, "Order", order);
                addTextChild(command, "Path", buildCommandLine(*task));
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
            // 顺序按 autoLogonChildOrder()
            addTextChild(autoLogon, "Enabled", true);
            addTextChild(autoLogon, "LogonCount", cfg.sysprep.autoLogonCount);
            const std::string userName =
                cfg.deploy.userName.empty() ? "Administrator" : cfg.deploy.userName;
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

        const char* location = networkLocationValue(cfg.optimization.networkLocation);
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

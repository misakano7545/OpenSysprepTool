#include <doctest/doctest.h>

#include <pugixml.hpp>

#include <string>
#include <vector>

#include "core/config.hpp"
#include "core/unattend.hpp"

namespace {

osp::config::Config sampleConfig()
{
    osp::config::Config cfg;
    cfg.schemaVersion = osp::config::kSchemaVersion;

    cfg.systemInfo.registeredName = "OpenSysprep";
    cfg.systemInfo.registeredOrg = "misakano7545";
    cfg.systemInfo.serialNumber = "00000-00000-00000-00000";
    cfg.systemInfo.computerName = "PC-";
    cfg.systemInfo.computerNameMode = "prefixRandom";

    cfg.firstLogon.runDeviceManager = true;
    cfg.firstLogon.resolutionX = 1920;
    cfg.firstLogon.resolutionY = 1080;
    cfg.firstLogon.refreshRate = 60;
    cfg.firstLogon.colorDepth = 32;

    cfg.optimization.networkLocation = "work";

    osp::config::RunPlanTask desktop;
    desktop.phase = "desktop";
    desktop.target = "C:\\Windows\\System32\\notepad.exe";

    osp::config::RunPlanTask postInstall;
    postInstall.phase = "postinstall";
    postInstall.target = "C:\\drivers\\setup.exe";
    postInstall.arguments = "/quiet";

    cfg.runPlan.push_back(desktop);
    cfg.runPlan.push_back(postInstall);

    cfg.deploy.skipOobe = true;
    cfg.deploy.autoCreateUser = true;
    cfg.deploy.userName = "User";
    cfg.deploy.userGroup = "Administrators";

    cfg.sysprep.autoLogon = true;
    cfg.sysprep.autoLogonCount = 3;
    cfg.sysprep.workgroup = "WORKGROUP";

    return cfg;
}

pugi::xml_document parseOk(const std::string& xml)
{
    pugi::xml_document doc;
    const pugi::xml_parse_result result = doc.load_string(xml.c_str());
    REQUIRE(result);
    return doc;
}

pugi::xml_node settingsFor(pugi::xml_node root, const char* pass, const char* component)
{
    for (pugi::xml_node settings : root.children("settings"))
    {
        if (std::string(settings.attribute("pass").value()) != pass)
        {
            continue;
        }
        for (pugi::xml_node node : settings.children("component"))
        {
            if (std::string(node.attribute("name").value()) == component)
            {
                return node;
            }
        }
    }
    return pugi::xml_node();
}

std::vector<std::string> childNames(pugi::xml_node parent)
{
    std::vector<std::string> names;
    for (pugi::xml_node child : parent.children())
    {
        names.emplace_back(child.name());
    }
    return names;
}

// 检查 actual 是 expected 的子序列（顺序一致，允许缺省）
void checkOrderedSubset(const std::vector<std::string>& actual, const std::vector<const char*>& expected)
{
    std::size_t cursor = 0;
    for (const std::string& name : actual)
    {
        bool found = false;
        while (cursor < expected.size())
        {
            if (name == expected[cursor])
            {
                found = true;
                ++cursor;
                break;
            }
            ++cursor;
        }
        INFO("element out of documented order: " << name);
        CHECK(found);
        if (!found)
        {
            return;
        }
    }
}

} // namespace

TEST_CASE("unattend root declares the microsoft namespace and wcm")
{
    const auto generated = osp::unattend::generate(sampleConfig());
    REQUIRE(generated.ok);

    pugi::xml_document doc = parseOk(generated.xml);
    pugi::xml_node root = doc.child("unattend");
    REQUIRE(root);

    CHECK(std::string(root.attribute("xmlns").value()) == osp::unattend::kNamespace);
    CHECK(std::string(root.attribute("xmlns:wcm").value()) == osp::unattend::kWcmNamespace);
    CHECK(std::string(osp::unattend::kNamespace) == "urn:schemas-microsoft-com:unattend");
}

TEST_CASE("unattend components carry the required attributes")
{
    const auto generated = osp::unattend::generate(sampleConfig());
    REQUIRE(generated.ok);
    pugi::xml_document doc = parseOk(generated.xml);

    int components = 0;
    for (pugi::xml_node settings : doc.child("unattend").children("settings"))
    {
        for (pugi::xml_node component : settings.children("component"))
        {
            ++components;
            CHECK(std::string(component.attribute("name").value()).rfind("Microsoft-Windows-", 0) == 0);
            CHECK(std::string(component.attribute("processorArchitecture").value()) == "amd64");
            CHECK(std::string(component.attribute("publicKeyToken").value()) == "31bf3856ad364e35");
            CHECK(std::string(component.attribute("language").value()) == "neutral");
            CHECK(std::string(component.attribute("versionScope").value()) == "nonSxS");
        }
    }
    CHECK(components >= 3);
}

TEST_CASE("shell-setup oobeSystem children follow the documented order")
{
    const auto generated = osp::unattend::generate(sampleConfig());
    REQUIRE(generated.ok);
    pugi::xml_document doc = parseOk(generated.xml);

    pugi::xml_node component =
        settingsFor(doc.child("unattend"), "oobeSystem", "Microsoft-Windows-Shell-Setup");
    REQUIRE(component);

    const std::vector<std::string> names = childNames(component);
    checkOrderedSubset(names, osp::unattend::shellSetupOobeSystemOrder());
    CHECK(names.size() == 5); // AutoLogon, Display, FirstLogonCommands, OOBE, UserAccounts
}

TEST_CASE("nested element order matches the reference child lists")
{
    const auto generated = osp::unattend::generate(sampleConfig());
    REQUIRE(generated.ok);
    pugi::xml_document doc = parseOk(generated.xml);

    pugi::xml_node component =
        settingsFor(doc.child("unattend"), "oobeSystem", "Microsoft-Windows-Shell-Setup");
    REQUIRE(component);

    checkOrderedSubset(childNames(component.child("OOBE")), osp::unattend::oobeChildOrder());
    checkOrderedSubset(childNames(component.child("AutoLogon")), osp::unattend::autoLogonChildOrder());
    checkOrderedSubset(childNames(component.child("Display")), osp::unattend::displayChildOrder());

    pugi::xml_node command = component.child("FirstLogonCommands").child("SynchronousCommand");
    REQUIRE(command);
    checkOrderedSubset(childNames(command), osp::unattend::synchronousCommandChildOrder());

    pugi::xml_node account = component.child("UserAccounts").child("LocalAccounts").child("LocalAccount");
    REQUIRE(account);
    checkOrderedSubset(childNames(account), osp::unattend::localAccountChildOrder());
}

TEST_CASE("computer name honours the configured mode")
{
    SUBCASE("fixed")
    {
        osp::config::Config cfg = sampleConfig();
        cfg.systemInfo.computerNameMode = "fixed";
        cfg.systemInfo.computerName = "OSPT-PC";

        const auto generated = osp::unattend::generate(cfg);
        REQUIRE(generated.ok);

        pugi::xml_document doc = parseOk(generated.xml);
        pugi::xml_node component =
            settingsFor(doc.child("unattend"), "specialize", "Microsoft-Windows-Shell-Setup");
        REQUIRE(component);
        CHECK(std::string(component.child("ComputerName").text().get()) == "OSPT-PC");
    }

    SUBCASE("random uses the documented asterisk")
    {
        osp::config::Config cfg = sampleConfig();
        cfg.systemInfo.computerNameMode = "random";

        const auto generated = osp::unattend::generate(cfg);
        REQUIRE(generated.ok);

        pugi::xml_document doc = parseOk(generated.xml);
        pugi::xml_node component =
            settingsFor(doc.child("unattend"), "specialize", "Microsoft-Windows-Shell-Setup");
        REQUIRE(component);
        CHECK(std::string(component.child("ComputerName").text().get()) == "*");
    }

    SUBCASE("prefixRandom keeps the prefix and is deterministic")
    {
        osp::config::Config cfg = sampleConfig();

        std::string first;
        std::string second;
        std::string error;
        REQUIRE(osp::unattend::resolveComputerName(cfg.systemInfo, first, error));
        REQUIRE(osp::unattend::resolveComputerName(cfg.systemInfo, second, error));

        CHECK(first == second);
        CHECK(first.rfind("PC-", 0) == 0);
        CHECK(first.size() <= 15);
    }
}

TEST_CASE("invalid computer names are rejected")
{
    std::string out;
    std::string error;

    SUBCASE("too long")
    {
        osp::config::SystemInfo info;
        info.computerNameMode = "fixed";
        info.computerName = "THIS-NAME-IS-TOO-LONG";
        CHECK_FALSE(osp::unattend::resolveComputerName(info, out, error));
        CHECK(error.find("15 bytes") != std::string::npos);
    }

    SUBCASE("forbidden character")
    {
        osp::config::SystemInfo info;
        info.computerNameMode = "fixed";
        info.computerName = "PC@HOME";
        CHECK_FALSE(osp::unattend::resolveComputerName(info, out, error));
        CHECK(error.find("forbidden") != std::string::npos);
    }

    SUBCASE("digits only")
    {
        osp::config::SystemInfo info;
        info.computerNameMode = "fixed";
        info.computerName = "12345";
        CHECK_FALSE(osp::unattend::resolveComputerName(info, out, error));
        CHECK(error.find("digits") != std::string::npos);
    }
}

TEST_CASE("autologon carries the configured logon count")
{
    const auto generated = osp::unattend::generate(sampleConfig());
    REQUIRE(generated.ok);
    pugi::xml_document doc = parseOk(generated.xml);

    pugi::xml_node component =
        settingsFor(doc.child("unattend"), "oobeSystem", "Microsoft-Windows-Shell-Setup");
    REQUIRE(component);

    pugi::xml_node autoLogon = component.child("AutoLogon");
    REQUIRE(autoLogon);
    CHECK(std::string(autoLogon.child("LogonCount").text().as_string()) == "3");
    CHECK(std::string(autoLogon.child("Enabled").text().as_string()) == "true");
    CHECK(std::string(autoLogon.child("Username").text().as_string()) == "User");
}

TEST_CASE("first logon commands count matches the config")
{
    const auto generated = osp::unattend::generate(sampleConfig());
    REQUIRE(generated.ok);
    pugi::xml_document doc = parseOk(generated.xml);

    pugi::xml_node component =
        settingsFor(doc.child("unattend"), "oobeSystem", "Microsoft-Windows-Shell-Setup");
    REQUIRE(component);

    // 设备管理器 1 条 + runPlan 中 desktop 阶段 1 条 = 2
    int count = 0;
    int expectedOrder = 1;
    for (pugi::xml_node command : component.child("FirstLogonCommands").children("SynchronousCommand"))
    {
        ++count;
        CHECK(command.child("Order").text().as_int() == expectedOrder);
        ++expectedOrder;
    }
    CHECK(count == 2);
}

TEST_CASE("run synchronous commands are emitted for the postinstall phase only")
{
    const auto generated = osp::unattend::generate(sampleConfig());
    REQUIRE(generated.ok);
    pugi::xml_document doc = parseOk(generated.xml);

    pugi::xml_node component =
        settingsFor(doc.child("unattend"), "specialize", "Microsoft-Windows-Deployment");
    REQUIRE(component);

    int count = 0;
    for (pugi::xml_node command : component.child("RunSynchronous").children("RunSynchronousCommand"))
    {
        ++count;
        CHECK(std::string(command.child("Path").text().as_string()) == "C:\\drivers\\setup.exe /quiet");
    }
    CHECK(count == 1);
}

TEST_CASE("network location maps to the documented values")
{
    const auto generated = osp::unattend::generate(sampleConfig());
    REQUIRE(generated.ok);
    pugi::xml_document doc = parseOk(generated.xml);

    pugi::xml_node component =
        settingsFor(doc.child("unattend"), "oobeSystem", "Microsoft-Windows-Shell-Setup");
    REQUIRE(component);

    pugi::xml_node oobe = component.child("OOBE");
    REQUIRE(oobe);

    const std::string location = oobe.child("NetworkLocation").text().as_string();
    CHECK(location == "Work");
}

TEST_CASE("workgroup and domain are mutually exclusive")
{
    SUBCASE("workgroup only")
    {
        osp::config::Config cfg = sampleConfig();
        const auto generated = osp::unattend::generate(cfg);
        REQUIRE(generated.ok);
        pugi::xml_document doc = parseOk(generated.xml);

        pugi::xml_node component =
            settingsFor(doc.child("unattend"), "specialize", "Microsoft-Windows-UnattendedJoin");
        REQUIRE(component);
        pugi::xml_node identification = component.child("Identification");
        CHECK(identification.child("JoinWorkgroup"));
        CHECK_FALSE(identification.child("JoinDomain"));
    }

    SUBCASE("domain wins when both are set")
    {
        osp::config::Config cfg = sampleConfig();
        cfg.sysprep.domain = "corp.example.com";
        const auto generated = osp::unattend::generate(cfg);
        REQUIRE(generated.ok);
        pugi::xml_document doc = parseOk(generated.xml);

        pugi::xml_node component =
            settingsFor(doc.child("unattend"), "specialize", "Microsoft-Windows-UnattendedJoin");
        REQUIRE(component);
        pugi::xml_node identification = component.child("Identification");
        CHECK(identification.child("JoinDomain"));
        CHECK_FALSE(identification.child("JoinWorkgroup"));
    }
}

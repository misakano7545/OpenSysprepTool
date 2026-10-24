#include "core/config.hpp"

#include <nlohmann/json.hpp>

namespace osp::config {

namespace {

using json = nlohmann::json;

json encode(const Config& cfg)
{
    json j;
    j["schemaVersion"] = cfg.schemaVersion;

    const auto& si = cfg.systemInfo;
    j["systemInfo"] = {
        {"registeredName", si.registeredName},
        {"registeredOrg", si.registeredOrg},
        {"serialNumber", si.serialNumber},
        {"computerName", si.computerName},
        {"computerNameMode", si.computerNameMode},
        {"bootMenuTimeoutSec", si.bootMenuTimeoutSec},
    };

    const auto& fl = cfg.firstLogon;
    j["firstLogon"] = {
        {"runDeviceManager", fl.runDeviceManager},
        {"rebootAfterRun", fl.rebootAfterRun},
        {"resolutionX", fl.resolutionX},
        {"resolutionY", fl.resolutionY},
        {"refreshRate", fl.refreshRate},
        {"colorDepth", fl.colorDepth},
        {"syncIeHomepage", fl.syncIeHomepage},
        {"syncIeFavorites", fl.syncIeFavorites},
    };

    const auto& op = cfg.optimization;
    j["optimization"] = {
        {"pinTaskbarItems", op.pinTaskbarItems},
        {"closeNumLock", op.closeNumLock},
        {"enableHibernate", op.enableHibernate},
        {"closeRebootDialog", op.closeRebootDialog},
        {"myDocumentsTarget", op.myDocumentsTarget},
        {"pageFileTarget", op.pageFileTarget},
        {"autoSetIp", op.autoSetIp},
        {"ipMode", op.ipMode},
        {"networkLocation", op.networkLocation},
    };

    json tasks = json::array();
    for (const auto& t : cfg.runPlan)
    {
        tasks.push_back({
            {"phase", t.phase},
            {"type", t.type},
            {"target", t.target},
            {"arguments", t.arguments},
            {"windowStyle", t.windowStyle},
            {"waitMode", t.waitMode},
            {"waitSeconds", t.waitSeconds},
            {"cleanup", t.cleanup},
            {"searchScope", t.searchScope},
        });
    }
    j["runPlan"] = std::move(tasks);

    const auto& dp = cfg.deploy;
    j["deploy"] = {
        {"module", dp.module},
        {"background", dp.background},
        {"titleText", dp.titleText},
        {"skipOobe", dp.skipOobe},
        {"autoCreateUser", dp.autoCreateUser},
        {"userName", dp.userName},
        {"userGroup", dp.userGroup},
        {"blockVirtualMachine", dp.blockVirtualMachine},
    };

    const auto& sp = cfg.sysprep;
    j["sysprep"] = {
        {"mode", sp.mode},
        {"generateNewSid", sp.generateNewSid},
        {"uninstallDrivers", sp.uninstallDrivers},
        {"loadSrsDrivers", sp.loadSrsDrivers},
        {"transferDesktopFiles", sp.transferDesktopFiles},
        {"cleanTemp", sp.cleanTemp},
        {"cleanDllCache", sp.cleanDllCache},
        {"autoLogon", sp.autoLogon},
        {"autoLogonCount", sp.autoLogonCount},
        {"workgroup", sp.workgroup},
        {"domain", sp.domain},
        {"domainUser", sp.domainUser},
        {"afterAction", sp.afterAction},
    };

    return j;
}

} // namespace

std::string toJson(const Config& cfg)
{
    return encode(cfg).dump(2) + "\n";
}

ParseResult fromJson(const std::string& text)
{
    ParseResult result;

    json j;
    try
    {
        j = json::parse(text);
    }
    catch (const json::exception& e)
    {
        result.error = e.what();
        return result;
    }

    if (!j.is_object())
    {
        result.error = "root element is not a JSON object";
        return result;
    }

    Config cfg;
    cfg.schemaVersion = j.value("schemaVersion", kSchemaVersion);

    try
    {
        if (j.contains("systemInfo"))
        {
            const auto& si = j["systemInfo"];
            cfg.systemInfo.registeredName = si.value("registeredName", std::string{});
            cfg.systemInfo.registeredOrg = si.value("registeredOrg", std::string{});
            cfg.systemInfo.serialNumber = si.value("serialNumber", std::string{});
            cfg.systemInfo.computerName = si.value("computerName", std::string{});
            cfg.systemInfo.computerNameMode = si.value("computerNameMode", std::string{});
            cfg.systemInfo.bootMenuTimeoutSec = si.value("bootMenuTimeoutSec", 30);
        }

        if (j.contains("firstLogon"))
        {
            const auto& fl = j["firstLogon"];
            cfg.firstLogon.runDeviceManager = fl.value("runDeviceManager", false);
            cfg.firstLogon.rebootAfterRun = fl.value("rebootAfterRun", false);
            cfg.firstLogon.resolutionX = fl.value("resolutionX", 1024);
            cfg.firstLogon.resolutionY = fl.value("resolutionY", 768);
            cfg.firstLogon.refreshRate = fl.value("refreshRate", 60);
            cfg.firstLogon.colorDepth = fl.value("colorDepth", 32);
            cfg.firstLogon.syncIeHomepage = fl.value("syncIeHomepage", false);
            cfg.firstLogon.syncIeFavorites = fl.value("syncIeFavorites", false);
        }

        if (j.contains("optimization"))
        {
            const auto& op = j["optimization"];
            cfg.optimization.pinTaskbarItems = op.value("pinTaskbarItems", false);
            cfg.optimization.closeNumLock = op.value("closeNumLock", false);
            cfg.optimization.enableHibernate = op.value("enableHibernate", false);
            cfg.optimization.closeRebootDialog = op.value("closeRebootDialog", false);
            cfg.optimization.myDocumentsTarget = op.value("myDocumentsTarget", std::string{});
            cfg.optimization.pageFileTarget = op.value("pageFileTarget", std::string{});
            cfg.optimization.autoSetIp = op.value("autoSetIp", false);
            cfg.optimization.ipMode = op.value("ipMode", std::string{});
            cfg.optimization.networkLocation = op.value("networkLocation", std::string{});
        }

        if (j.contains("runPlan"))
        {
            const auto& tasks = j["runPlan"];
            if (!tasks.is_array())
            {
                result.error = "runPlan is not an array";
                return result;
            }
            for (const auto& t : tasks)
            {
                RunPlanTask task;
                task.phase = t.value("phase", std::string{});
                task.type = t.value("type", std::string{});
                task.target = t.value("target", std::string{});
                task.arguments = t.value("arguments", std::string{});
                task.windowStyle = t.value("windowStyle", std::string{});
                task.waitMode = t.value("waitMode", std::string{});
                task.waitSeconds = t.value("waitSeconds", 0);
                task.cleanup = t.value("cleanup", std::string{});
                task.searchScope = t.value("searchScope", std::string{});
                cfg.runPlan.push_back(std::move(task));
            }
        }

        if (j.contains("deploy"))
        {
            const auto& dp = j["deploy"];
            cfg.deploy.module = dp.value("module", std::string{});
            cfg.deploy.background = dp.value("background", std::string{});
            cfg.deploy.titleText = dp.value("titleText", std::string{});
            cfg.deploy.skipOobe = dp.value("skipOobe", false);
            cfg.deploy.autoCreateUser = dp.value("autoCreateUser", false);
            cfg.deploy.userName = dp.value("userName", std::string{});
            cfg.deploy.userGroup = dp.value("userGroup", std::string{});
            cfg.deploy.blockVirtualMachine = dp.value("blockVirtualMachine", false);
        }

        if (j.contains("sysprep"))
        {
            const auto& sp = j["sysprep"];
            cfg.sysprep.mode = sp.value("mode", std::string{});
            cfg.sysprep.generateNewSid = sp.value("generateNewSid", true);
            cfg.sysprep.uninstallDrivers = sp.value("uninstallDrivers", false);
            cfg.sysprep.loadSrsDrivers = sp.value("loadSrsDrivers", false);
            cfg.sysprep.transferDesktopFiles = sp.value("transferDesktopFiles", false);
            cfg.sysprep.cleanTemp = sp.value("cleanTemp", false);
            cfg.sysprep.cleanDllCache = sp.value("cleanDllCache", false);
            cfg.sysprep.autoLogon = sp.value("autoLogon", false);
            cfg.sysprep.autoLogonCount = sp.value("autoLogonCount", 1);
            cfg.sysprep.workgroup = sp.value("workgroup", std::string{});
            cfg.sysprep.domain = sp.value("domain", std::string{});
            cfg.sysprep.domainUser = sp.value("domainUser", std::string{});
            cfg.sysprep.afterAction = sp.value("afterAction", std::string{});
        }
    }
    catch (const json::exception& e)
    {
        result.error = e.what();
        return result;
    }

    result.ok = true;
    result.config = std::move(cfg);
    return result;
}

} // namespace osp::config

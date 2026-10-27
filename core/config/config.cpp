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
        {"createConnection", fl.createConnection},
        {"connectionMode", fl.connectionMode},
        {"connectionName", fl.connectionName},
        {"connectionUser", fl.connectionUser},
        {"connectionPassword", fl.connectionPassword},
        {"connectionCountdownSec", fl.connectionCountdownSec},
    };

    const auto& op = cfg.optimization;
    j["optimization"] = {
        {"pinTaskbarItems", op.pinTaskbarItems},
        {"closeNumLock", op.closeNumLock},
        {"enableHibernate", op.enableHibernate},
        {"closeRebootDialog", op.closeRebootDialog},
        {"myDocumentsTarget", op.myDocumentsTarget},
        {"pageFileTarget", op.pageFileTarget},
        {"autoPagefileDrive", op.autoPagefileDrive},
        {"pageFileSmart", op.pageFileSmart},
        {"pageFileDrive", op.pageFileDrive},
        {"pageFileSizeMode", op.pageFileSizeMode},
        {"pageFileMinMultiplier", op.pageFileMinMultiplier},
        {"pageFileMaxMultiplier", op.pageFileMaxMultiplier},
        {"pageFileMinFixedMb", op.pageFileMinFixedMb},
        {"pageFileMaxFixedMb", op.pageFileMaxFixedMb},
        {"autoSetIp", op.autoSetIp},
        {"ipMode", op.ipMode},
        {"networkLocation", op.networkLocation},
        {"clearSrsDrivers", op.clearSrsDrivers},
        {"setResolution", op.setResolution},
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
            {"expectedVolumeLabel", t.expectedVolumeLabel},
            {"timeoutAction", t.timeoutAction},
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
        {"backgroundImage", dp.backgroundImage},
        {"imageSwitchSeconds", dp.imageSwitchSeconds},
        {"deployResolutionX", dp.deployResolutionX},
        {"deployResolutionY", dp.deployResolutionY},
        {"fontColor", dp.fontColor},
        {"verifyIntegrity", dp.verifyIntegrity},
        {"hijackMode", dp.hijackMode},
        {"hijackTip", dp.hijackTip},
        {"hijackShowTip", dp.hijackShowTip},
        {"enableAdministrator", dp.enableAdministrator},
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
        {"autoLogonPassword", sp.autoLogonPassword},
        {"workgroup", sp.workgroup},
        {"domain", sp.domain},
        {"domainUser", sp.domainUser},
        {"restartSilent", sp.restartSilent},
        {"afterAction", sp.afterAction},
        {"domainPassword", sp.domainPassword},
        {"perServerLicensing", sp.perServerLicensing},
        {"maxConnections", sp.maxConnections},
    };

    // OEM 信息：全空时也写出空串，保证 JSON 往返逐字稳定（不靠「键缺失」表达默认值）
    const OemSettings& om = cfg.oem;
    {
        nlohmann::json tamper = nlohmann::json::array();
        for (const auto& entry : dp.tamperFiles)
        {
            nlohmann::json item;
            item["path"] = entry.path;
            item["md5"] = entry.md5;
            if (!entry.tip.empty())
            {
                item["tip"] = entry.tip;
            }
            tamper.push_back(std::move(item));
        }
        j["deploy"]["tamperFiles"] = std::move(tamper);
    }

    j["oem"] = {
        {"manufacturer", om.manufacturer},
        {"model", om.model},
        {"supportHours", om.supportHours},
        {"supportPhone", om.supportPhone},
        {"supportProvider", om.supportProvider},
        {"supportUrl", om.supportUrl},
        {"logo", om.logo},
        {"ieTitle", om.ieTitle},
        {"autoBrandInfo", om.autoBrandInfo},
        {"wallpaper", om.wallpaper},
        {"deployImage", om.deployImage},
        {"source", om.source},
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
            cfg.firstLogon.createConnection = fl.value("createConnection", false);
            cfg.firstLogon.connectionMode = fl.value("connectionMode", std::string("show"));
            cfg.firstLogon.connectionName = fl.value("connectionName", std::string("宽带连接"));
            cfg.firstLogon.connectionUser = fl.value("connectionUser", std::string{});
            cfg.firstLogon.connectionPassword = fl.value("connectionPassword", std::string{});
            cfg.firstLogon.connectionCountdownSec = fl.value("connectionCountdownSec", 10);
        }

        if (j.contains("optimization"))
        {
            const auto& op = j["optimization"];
            cfg.optimization.pinTaskbarItems = op.value("pinTaskbarItems", false);
            cfg.optimization.closeNumLock = op.value("closeNumLock", false);
            cfg.optimization.enableHibernate = op.value("enableHibernate", false);
            cfg.optimization.closeRebootDialog = op.value("closeRebootDialog", false);
            cfg.optimization.myDocumentsTarget = op.value("myDocumentsTarget", std::string{});
            cfg.optimization.moveDocuments = op.value("moveDocuments", false);
            cfg.optimization.documentsSmart = op.value("documentsSmart", true);
            cfg.optimization.moveFavorites = op.value("moveFavorites", false);
            cfg.optimization.favoritesSmart = op.value("favoritesSmart", true);
            cfg.optimization.favoritesTarget = op.value("favoritesTarget", std::string{});
            cfg.optimization.lockTaskbarFiles = op.value("lockTaskbarFiles", std::vector<std::string>{});
            cfg.optimization.unlockTaskbarFiles = op.value("unlockTaskbarFiles", std::vector<std::string>{});
            cfg.optimization.saveIeHomePage = op.value("saveIeHomePage", false);
            cfg.optimization.ieHomePage = op.value("ieHomePage", std::string{});
            cfg.optimization.defaultUserHive = op.value("defaultUserHive", std::string{});
            cfg.optimization.pageFileTarget = op.value("pageFileTarget", std::string{});
            cfg.optimization.autoPagefileDrive = op.value("autoPagefileDrive", false);
            cfg.optimization.pageFileSmart = op.value("pageFileSmart", true);
            cfg.optimization.pageFileDrive = op.value("pageFileDrive", std::string{});
            cfg.optimization.pageFileSizeMode = op.value("pageFileSizeMode", std::string{});
            cfg.optimization.pageFileMinMultiplier = op.value("pageFileMinMultiplier", 0);
            cfg.optimization.pageFileMaxMultiplier = op.value("pageFileMaxMultiplier", 0);
            cfg.optimization.pageFileMinFixedMb = op.value("pageFileMinFixedMb", 0);
            cfg.optimization.pageFileMaxFixedMb = op.value("pageFileMaxFixedMb", 0);
            cfg.optimization.sysTweaks = op.value("sysTweaks", std::vector<std::string>{});
            cfg.optimization.autoSetIp = op.value("autoSetIp", false);
            cfg.optimization.ipMode = op.value("ipMode", std::string{});
            cfg.optimization.ipAddress = op.value("ipAddress", std::string{});
            cfg.optimization.ipRangeStart = op.value("ipRangeStart", std::string{});
            cfg.optimization.ipRangeEnd = op.value("ipRangeEnd", std::string{});
            cfg.optimization.subnetMask = op.value("subnetMask", std::string{});
            cfg.optimization.gateway = op.value("gateway", std::string{});
            cfg.optimization.dnsPreferred = op.value("dnsPreferred", std::string{});
            cfg.optimization.dnsAlternate = op.value("dnsAlternate", std::string{});
            cfg.optimization.ipRandomBase = op.value("ipRandomBase", std::string{});
            cfg.optimization.ipRandomOctetStart = op.value("ipRandomOctetStart", 0);
            cfg.optimization.ipRandomOctetEnd = op.value("ipRandomOctetEnd", 0);
            cfg.optimization.autoNetworkLocation = op.value("autoNetworkLocation", false);
            cfg.optimization.networkLocation = op.value("networkLocation", std::string{});
            cfg.optimization.clearSrsDrivers = op.value("clearSrsDrivers", false);
            cfg.optimization.setResolution = op.value("setResolution", false);
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
                task.expectedVolumeLabel = t.value("expectedVolumeLabel", std::string{});
                task.timeoutAction = t.value("timeoutAction", std::string{});
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
            cfg.deploy.backgroundImage = dp.value("backgroundImage", std::string{});
            cfg.deploy.imageSwitchSeconds = dp.value("imageSwitchSeconds", 10);
            cfg.deploy.deployResolutionX = dp.value("deployResolutionX", 0);
            cfg.deploy.deployResolutionY = dp.value("deployResolutionY", 0);
            cfg.deploy.fontColor = dp.value("fontColor", std::string{});
            cfg.deploy.verifyIntegrity = dp.value("verifyIntegrity", false);
            cfg.deploy.hijackMode = dp.value("hijackMode", std::string("terminate"));
            cfg.deploy.hijackTip = dp.value("hijackTip", std::string{});
            cfg.deploy.hijackShowTip = dp.value("hijackShowTip", true);
            cfg.deploy.tamperFiles.clear();
            if (dp.contains("tamperFiles") && dp["tamperFiles"].is_array())
            {
                for (const auto& item : dp["tamperFiles"])
                {
                    osp::config::DeploySettings::TamperEntry entry;
                    entry.path = item.value("path", std::string{});
                    entry.md5 = item.value("md5", std::string{});
                    entry.tip = item.value("tip", std::string{});
                    if (!entry.path.empty())
                    {
                        cfg.deploy.tamperFiles.push_back(std::move(entry));
                    }
                }
            }
            cfg.deploy.enableAdministrator = dp.value("enableAdministrator", false);
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
            cfg.sysprep.autoLogonPassword = sp.value("autoLogonPassword", std::string{});
            cfg.sysprep.workgroup = sp.value("workgroup", std::string{});
            cfg.sysprep.domain = sp.value("domain", std::string{});
            cfg.sysprep.domainUser = sp.value("domainUser", std::string{});
            cfg.sysprep.restartSilent = sp.value("restartSilent", false);
            cfg.sysprep.afterAction = sp.value("afterAction", std::string{});
            cfg.sysprep.domainPassword = sp.value("domainPassword", std::string{});
            cfg.sysprep.perServerLicensing = sp.value("perServerLicensing", false);
            cfg.sysprep.maxConnections = sp.value("maxConnections", 5);
        }

        if (j.contains("oem"))
        {
            const auto& om = j["oem"];
            cfg.oem.manufacturer = om.value("manufacturer", std::string{});
            cfg.oem.model = om.value("model", std::string{});
            cfg.oem.supportHours = om.value("supportHours", std::string{});
            cfg.oem.supportPhone = om.value("supportPhone", std::string{});
            cfg.oem.supportProvider = om.value("supportProvider", std::string{});
            cfg.oem.supportUrl = om.value("supportUrl", std::string{});
            cfg.oem.logo = om.value("logo", std::string{});
            cfg.oem.ieTitle = om.value("ieTitle", std::string{});
            cfg.oem.autoBrandInfo = om.value("autoBrandInfo", false);
            cfg.oem.wallpaper = om.value("wallpaper", std::string{});
            cfg.oem.deployImage = om.value("deployImage", std::string{});
            cfg.oem.source = om.value("source", std::string{});
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
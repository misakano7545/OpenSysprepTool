#include "core/prepare.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/regio.hpp"

namespace {

osp::prepare::Options testOptions()
{
    osp::prepare::Options options;
    options.systemDrive = "C:\\";
    options.userProfile = "C:\\Users\\Tester";
    options.publicProfile = "C:\\Users\\Public";
    options.windowsDir = "C:\\Windows";
    options.tempDir = "C:\\Users\\Tester\\AppData\\Local\\Temp";
    options.defaultUserProfile = "C:\\Users\\Default";
    return options;
}

const osp::prepare::Action* findAction(const osp::prepare::Plan& plan, const std::string& id)
{
    for (const osp::prepare::Action& action : plan.actions)
    {
        if (action.id == id)
        {
            return &action;
        }
    }
    return nullptr;
}

bool hasDeferred(const osp::prepare::Plan& plan, const std::string& field)
{
    return std::any_of(plan.deferred.begin(), plan.deferred.end(), [&field](const auto& item) {
        return item.sourceField == field;
    });
}

} // namespace

TEST_CASE("prepare: default config only sets the boot menu timeout")
{
    osp::config::Config cfg;
    const osp::prepare::Plan plan = osp::prepare::build(cfg, testOptions());
    CHECK(plan.ok);
    // 默认配置里的启动菜单时间不再是死字段：它会生成一个 BCD 动作
    REQUIRE(plan.actions.size() == 1);
    CHECK(plan.actions[0].id == "systemInfo.bootMenuTimeoutSec");
    CHECK(plan.actions[0].kind == osp::prepare::ActionKind::BcdTimeout);
    CHECK(plan.actions[0].dword == 30);
    CHECK(plan.deferred.empty());
    CHECK(plan.blockers.empty());
    CHECK(plan.warnings.empty());
}

TEST_CASE("prepare: boot menu timeout 0 means do not touch")
{
    osp::config::Config cfg;
    cfg.systemInfo.bootMenuTimeoutSec = 0;
    const osp::prepare::Plan plan = osp::prepare::build(cfg, testOptions());
    CHECK(plan.ok);
    CHECK(plan.actions.empty());
    CHECK(plan.warnings.size() == 1);
}

TEST_CASE("prepare: optimization fields become explicit actions")
{
    osp::config::Config cfg;
    cfg.optimization.closeNumLock = true;
    cfg.optimization.enableHibernate = true;
    cfg.optimization.closeRebootDialog = true;
    cfg.optimization.pageFileTarget = "D:\\";

    const osp::prepare::Plan plan = osp::prepare::build(cfg, testOptions());
    REQUIRE(plan.ok);

    const osp::prepare::Action* numLock = findAction(plan, "optimization.closeNumLock");
    REQUIRE(numLock != nullptr);
    CHECK(numLock->hive == "HKU\\.DEFAULT");
    CHECK(numLock->key == "Control Panel\\Keyboard");
    CHECK(numLock->valueName == "InitialKeyboardIndicators");
    CHECK(numLock->text == "2");

    const osp::prepare::Action* hibernate = findAction(plan, "optimization.enableHibernate");
    REQUIRE(hibernate != nullptr);
    CHECK(hibernate->kind == osp::prepare::ActionKind::RunCommand);
    CHECK(hibernate->program == "C:\\Windows\\System32\\powercfg.exe");
    CHECK(hibernate->arguments == "/hibernate on");

    REQUIRE(findAction(plan, "optimization.closeRebootDialog.reasonOn") != nullptr);
    REQUIRE(findAction(plan, "optimization.closeRebootDialog.reasonUi") != nullptr);

    const osp::prepare::Action* pageFile = findAction(plan, "optimization.pageFileTarget");
    REQUIRE(pageFile != nullptr);
    CHECK(pageFile->requiresReboot);
    REQUIRE(pageFile->multi.size() == 1);
    CHECK(pageFile->multi[0] == "D:\\pagefile.sys 0 0");
}

TEST_CASE("prepare: unimplemented fields are reported with reasons")
{
    osp::config::Config cfg;
    cfg.optimization.myDocumentsTarget = "D:\\Docs";
    cfg.optimization.autoSetIp = true;
    cfg.optimization.pinTaskbarItems = true;
    cfg.firstLogon.syncIeHomepage = true;
    cfg.firstLogon.syncIeFavorites = true;
    cfg.deploy.verifyIntegrity = true;
    cfg.deploy.blockVirtualMachine = true;
    cfg.sysprep.uninstallDrivers = true;
    cfg.sysprep.loadSrsDrivers = true;
    cfg.sysprep.perServerLicensing = true;

    const osp::prepare::Plan plan = osp::prepare::build(cfg, testOptions());
    REQUIRE(plan.ok);

    CHECK(hasDeferred(plan, "optimization.myDocumentsTarget"));
    CHECK(hasDeferred(plan, "optimization.autoSetIp"));
    CHECK(hasDeferred(plan, "optimization.pinTaskbarItems"));
    CHECK(hasDeferred(plan, "firstLogon.syncIeHomepage"));
    CHECK(hasDeferred(plan, "deploy.verifyIntegrity"));
    CHECK(hasDeferred(plan, "deploy.blockVirtualMachine"));
    CHECK(hasDeferred(plan, "sysprep.uninstallDrivers"));
    CHECK(hasDeferred(plan, "sysprep.loadSrsDrivers"));
    CHECK(hasDeferred(plan, "sysprep.perServerLicensing"));

    // 收藏夹同步是实现了的，不应该出现在 deferred 里
    CHECK_FALSE(hasDeferred(plan, "firstLogon.syncIeFavorites"));
    REQUIRE(findAction(plan, "firstLogon.syncIeFavorites") != nullptr);
}

TEST_CASE("prepare: cleanup fields become file and command actions")
{
    osp::config::Config cfg;
    cfg.deploy.enableAdministrator = true;
    cfg.sysprep.transferDesktopFiles = true;
    cfg.sysprep.cleanTemp = true;
    cfg.sysprep.cleanDllCache = true;

    const osp::prepare::Plan plan = osp::prepare::build(cfg, testOptions());
    REQUIRE(plan.ok);

    const osp::prepare::Action* admin = findAction(plan, "deploy.enableAdministrator");
    REQUIRE(admin != nullptr);
    CHECK(admin->arguments == "user Administrator /active:yes");

    const osp::prepare::Action* desktop = findAction(plan, "sysprep.transferDesktopFiles");
    REQUIRE(desktop != nullptr);
    CHECK(desktop->kind == osp::prepare::ActionKind::MoveTree);
    CHECK(desktop->path == "C:\\Users\\Tester\\Desktop");
    CHECK(desktop->destination == "C:\\Users\\Public\\Desktop");

    const osp::prepare::Action* userTemp = findAction(plan, "sysprep.cleanTemp.user");
    REQUIRE(userTemp != nullptr);
    CHECK(userTemp->irreversible);

    REQUIRE(findAction(plan, "sysprep.cleanTemp.windows") != nullptr);
    const osp::prepare::Action* sfc = findAction(plan, "sysprep.cleanDllCache");
    REQUIRE(sfc != nullptr);
    CHECK(sfc->arguments == "/purgecache");
    CHECK(sfc->irreversible);
}

TEST_CASE("prepare: plan JSON and dry-run report contain the actions")
{
    osp::config::Config cfg;
    cfg.optimization.closeNumLock = true;
    cfg.optimization.pageFileTarget = "E:";

    const osp::prepare::Plan plan = osp::prepare::build(cfg, testOptions());
    const std::string json = osp::prepare::toJson(plan);
    CHECK(json.find("optimization.closeNumLock") != std::string::npos);
    CHECK(json.find("PagingFiles") != std::string::npos);
    CHECK(json.find("E:\\\\pagefile.sys 0 0") != std::string::npos);

    const std::string report = osp::prepare::dryRunReport(plan);
    CHECK(report.find("optimization.closeNumLock") != std::string::npos);
    CHECK(report.find("RESULT: DRY-RUN OK") != std::string::npos);
}

TEST_CASE("prepare: journal JSON round-trip")
{
    osp::prepare::Journal journal;
    journal.createdAt = "2026-10-26 10:00:00.000";

    osp::prepare::JournalEntry registry;
    registry.actionId = "optimization.closeNumLock";
    registry.kind = "registry";
    registry.hive = "HKU\\.DEFAULT";
    registry.key = "Control Panel\\Keyboard";
    registry.valueName = "InitialKeyboardIndicators";
    registry.existed = true;
    registry.valueType = "sz";
    registry.text = "2147483648";
    journal.entries.push_back(registry);

    osp::prepare::JournalEntry move;
    move.actionId = "sysprep.transferDesktopFiles";
    move.kind = "move";
    move.from = "C:\\Users\\Tester\\Desktop\\a.txt";
    move.to = "C:\\Users\\Public\\Desktop\\a.txt";
    journal.entries.push_back(move);

    osp::prepare::JournalEntry copy;
    copy.actionId = "firstLogon.syncIeFavorites";
    copy.kind = "copy";
    copy.path = "C:\\Users\\Default\\Favorites\\x.url";
    journal.entries.push_back(copy);

    const std::string text = osp::prepare::journalToJson(journal);
    osp::prepare::Journal parsed;
    std::string error;
    REQUIRE(osp::prepare::journalFromJson(text, parsed, error));
    REQUIRE(parsed.entries.size() == 3);
    CHECK(parsed.createdAt == journal.createdAt);
    CHECK(parsed.entries[0].kind == "registry");
    CHECK(parsed.entries[0].existed);
    CHECK(parsed.entries[0].text == "2147483648");
    CHECK(parsed.entries[1].from == move.from);
    CHECK(parsed.entries[2].path == copy.path);
}

TEST_CASE("prepare: execute and rollback registry actions")
{
    const std::string key = "Software\\OpenSysprepTool\\PrepareTest";
    std::string error;
    osp::regio::deleteTree(osp::regio::Hive::CurrentUser, key, error);

    std::vector<osp::prepare::Action> actions;

    osp::prepare::Action text;
    text.kind = osp::prepare::ActionKind::RegistrySet;
    text.id = "test.text";
    text.hive = "HKCU";
    text.key = key;
    text.valueName = "Text";
    text.valueType = "sz";
    text.text = "value";
    actions.push_back(text);

    osp::prepare::Action number;
    number.kind = osp::prepare::ActionKind::RegistrySet;
    number.id = "test.number";
    number.hive = "HKCU";
    number.key = key;
    number.valueName = "Number";
    number.valueType = "dword";
    number.dword = 7;
    actions.push_back(number);

    const osp::prepare::ExecResult applied = osp::prepare::executeActions(actions);
    REQUIRE(applied.ok);
    CHECK(applied.applied == 2);
    CHECK(applied.journal.entries.size() == 2);
    CHECK_FALSE(applied.journal.entries[0].existed);

    osp::regio::Key handle = osp::regio::open(osp::regio::Hive::CurrentUser, key, false, error);
    REQUIRE(handle.valid());
    osp::regio::Value value;
    bool exists = false;
    CHECK(osp::regio::readValue(handle, "Text", value, exists, error));
    CHECK(exists);
    CHECK(value.text == "value");
    CHECK(osp::regio::readValue(handle, "Number", value, exists, error));
    CHECK(exists);
    CHECK(value.dword == 7);
    handle.close();

    const osp::prepare::ExecResult reverted = osp::prepare::rollback(applied.journal, false);
    REQUIRE(reverted.ok);

    handle = osp::regio::open(osp::regio::Hive::CurrentUser, key, false, error);
    if (handle.valid())
    {
        CHECK(osp::regio::readValue(handle, "Text", value, exists, error));
        CHECK_FALSE(exists);
    }
    handle.close();

    osp::regio::deleteTree(osp::regio::Hive::CurrentUser, key, error);
}

TEST_CASE("prepare: execute stops at the first failure and keeps the journal")
{
    std::vector<osp::prepare::Action> actions;

    osp::prepare::Action bad;
    bad.kind = osp::prepare::ActionKind::RegistrySet;
    bad.id = "test.badHive";
    bad.hive = "HKCR";
    bad.key = "Whatever";
    bad.valueName = "X";
    bad.valueType = "sz";
    bad.text = "x";
    actions.push_back(bad);

    const osp::prepare::ExecResult result = osp::prepare::executeActions(actions);
    CHECK_FALSE(result.ok);
    CHECK(result.failed == 1);
    CHECK(result.applied == 0);
    CHECK_FALSE(result.error.empty());
}

TEST_CASE("prepare: anti-rogue mode cleans the promotion payloads")
{
    osp::config::Config cfg;
    cfg.sysprep.mode = "antiRogue";
    const osp::prepare::Plan plan = osp::prepare::build(cfg, testOptions());
    REQUIRE(plan.ok);

    const osp::prepare::Action* softInst = findAction(plan, "sysprep.antiRogue.softInst");
    REQUIRE(softInst != nullptr);
    CHECK(softInst->kind == osp::prepare::ActionKind::DeleteTree);
    CHECK(softInst->path == "C:\\SoftInst");
    CHECK(softInst->irreversible);

    const osp::prepare::Action* hklm = findAction(plan, "sysprep.antiRogue.runOnce.hklm");
    REQUIRE(hklm != nullptr);
    CHECK(hklm->kind == osp::prepare::ActionKind::RegistryDelete);
    CHECK(hklm->valueName == "inst.vbs");
    REQUIRE(findAction(plan, "sysprep.antiRogue.runOnce.hkcu") != nullptr);

    // classic 不产生任何清理动作
    osp::config::Config classic;
    classic.sysprep.mode = "classic";
    const osp::prepare::Plan plain = osp::prepare::build(classic, testOptions());
    CHECK(findAction(plain, "sysprep.antiRogue.softInst") == nullptr);
}

TEST_CASE("prepare: volume mode warns that it behaves like classic")
{
    osp::config::Config cfg;
    cfg.sysprep.mode = "volume";
    const osp::prepare::Plan plan = osp::prepare::build(cfg, testOptions());
    REQUIRE(plan.ok);
    const bool warned = std::any_of(plan.warnings.begin(), plan.warnings.end(), [](const std::string& text) {
        return text.find("大客户高级") != std::string::npos;
    });
    CHECK(warned);
}

TEST_CASE("prepare: hibernate carries an undo command")
{
    osp::config::Config cfg;
    cfg.optimization.enableHibernate = true;
    const osp::prepare::Plan plan = osp::prepare::build(cfg, testOptions());
    const osp::prepare::Action* hibernate = findAction(plan, "optimization.enableHibernate");
    REQUIRE(hibernate != nullptr);
    CHECK(hibernate->undoCommand.find("/hibernate off") != std::string::npos);
}

TEST_CASE("prepare: self-test writes and restores HKCU values")
{
    std::string report;
    std::string error;
    CHECK(osp::prepare::selfTest(report, error));
    CHECK(report.find("prepare selftest OK") != std::string::npos);
    CHECK(error.empty());
}

TEST_CASE("prepare: privacy traces and update cache become cleanup actions")
{
    osp::config::Config cfg;
    cfg.optimization.clearPrivacyTraces = true;
    cfg.optimization.clearUpdateCache = true;

    const osp::prepare::Plan plan = osp::prepare::build(cfg, testOptions());
    REQUIRE(plan.ok);

    const char* expected[] = {
        "optimization.privacy.typedUrls",
        "optimization.privacy.runMru",
        "optimization.privacy.recentDocs",
        "optimization.privacy.lastVisited",
        "optimization.privacy.openSave",
        "optimization.privacy.trayNotify",
        "optimization.privacy.winrar",
    };
    for (const char* id : expected)
    {
        const osp::prepare::Action* action = findAction(plan, id);
        REQUIRE(action != nullptr);
        CHECK(action->kind == osp::prepare::ActionKind::RegistryDeleteKey);
        CHECK(action->hive == "HKCU");
        CHECK(action->irreversible);
    }

    const osp::prepare::Action* cache = findAction(plan, "optimization.clearUpdateCache");
    REQUIRE(cache != nullptr);
    CHECK(cache->path == std::string("C:\\Windows") + "\\SoftwareDistribution\\Download");
    CHECK(cache->irreversible);
}

TEST_CASE("prepare: deleting a registry key is idempotent")
{
    const std::string key = "Software\\OpenSysprepTool\\NoSuchKey";
    std::string error;
    osp::regio::deleteTree(osp::regio::Hive::CurrentUser, key, error);

    osp::prepare::Action action;
    action.kind = osp::prepare::ActionKind::RegistryDeleteKey;
    action.id = "test.deleteKey";
    action.hive = "HKCU";
    action.key = key;
    action.valueType = "sz";

    const osp::prepare::ExecResult result = osp::prepare::executeActions({action});
    CHECK(result.ok);
    CHECK(result.applied == 1);
    CHECK(result.journal.entries.empty()); // 不可回滚的动作不进 journal
}

TEST_CASE("prepare: pre-sysprep cleanup actions are built only when enabled")
{
    osp::config::Config cfg;
    const osp::prepare::Plan plain = osp::prepare::build(cfg, testOptions());
    for (const char* id : {
             "sysprep.stopSysprepProcess",
             "sysprep.cleanShadowCopies",
             "sysprep.cleanComponentStore",
             "sysprep.removeUnattendResidue.Panther\\unattend.xml",
         })
    {
        CHECK(findAction(plain, id) == nullptr);
    }

    cfg.sysprep.stopSysprepProcess = true;
    cfg.sysprep.cleanShadowCopies = true;
    cfg.sysprep.cleanEventLogs = true;
    cfg.sysprep.cleanComponentStore = true;
    cfg.sysprep.removeUnattendResidue = true;

    const osp::prepare::Plan plan = osp::prepare::build(cfg, testOptions());
    REQUIRE(plan.ok);

    const osp::prepare::Action* stop = findAction(plan, "sysprep.stopSysprepProcess");
    REQUIRE(stop != nullptr);
    CHECK(stop->arguments == "/F /IM sysprep.exe /T");
    CHECK(stop->tolerateNonZeroExit);

    const osp::prepare::Action* shadow = findAction(plan, "sysprep.cleanShadowCopies");
    REQUIRE(shadow != nullptr);
    CHECK(shadow->arguments == "delete shadows /all /quiet");
    CHECK(shadow->irreversible);

    // 四个固定日志各一条
    for (const char* log : {"System", "Application", "Setup", "Windows PowerShell"})
    {
        const std::string id = std::string("sysprep.cleanEventLogs.") + log;
        const osp::prepare::Action* action = findAction(plan, id);
        REQUIRE(action != nullptr);
        CHECK(action->program.find("wevtutil.exe") != std::string::npos);
    }

    const osp::prepare::Action* dism = findAction(plan, "sysprep.cleanComponentStore");
    REQUIRE(dism != nullptr);
    CHECK(dism->arguments == "/online /cleanup-image /startcomponentcleanup");
    // 故意不带 /resetbase
    CHECK(dism->arguments.find("resetbase") == std::string::npos);

    const osp::prepare::Action* panther =
        findAction(plan, "sysprep.removeUnattendResidue.Panther\\unattend.xml");
    REQUIRE(panther != nullptr);
    CHECK(panther->path == std::string("C:\\Windows") + "\\Panther\\unattend.xml");

    const osp::prepare::Action* rootUnattend = findAction(plan, "sysprep.removeUnattendResidue.systemDrive");
    REQUIRE(rootUnattend != nullptr);
    CHECK(rootUnattend->path == std::string("C:\\") + "unattend.xml");
}

TEST_CASE("prepare: delete tree removes a single file as well as directory contents")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "ospt_prepare_delete_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path file = dir / "residue.xml";
    {
        std::ofstream out(file);
        out << "x";
    }
    REQUIRE(fs::exists(file));

    osp::prepare::Action action;
    action.kind = osp::prepare::ActionKind::DeleteTree;
    action.id = "test.deleteFile";
    action.path = (dir / "residue.xml").string();

    const osp::prepare::ExecResult result = osp::prepare::executeActions({action});
    CHECK(result.ok);
    CHECK_FALSE(fs::exists(file));

    // 再跑一次：不存在时跳过而不是报错
    const osp::prepare::ExecResult again = osp::prepare::executeActions({action});
    CHECK(again.ok);
    CHECK_FALSE(again.warnings.empty());

    fs::remove_all(dir, ec);
}

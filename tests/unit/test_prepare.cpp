#include "core/prepare.hpp"

#include <doctest/doctest.h>

#include <algorithm>
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

TEST_CASE("prepare: default config has no actions and no deferred items")
{
    osp::config::Config cfg;
    const osp::prepare::Plan plan = osp::prepare::build(cfg, testOptions());
    CHECK(plan.ok);
    CHECK(plan.actions.empty());
    CHECK(plan.deferred.empty());
    CHECK(plan.blockers.empty());
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

TEST_CASE("prepare: self-test writes and restores HKCU values")
{
    std::string report;
    std::string error;
    CHECK(osp::prepare::selfTest(report, error));
    CHECK(report.find("prepare selftest OK") != std::string::npos);
    CHECK(error.empty());
}

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

    // 我的文档转移已在 B3 落地（离线 hive），不再是推迟项
    CHECK_FALSE(hasDeferred(plan, "optimization.myDocumentsTarget"));
    // 自动 IP 已在 B5 落地（netsh 动作），不再是推迟项
    CHECK_FALSE(hasDeferred(plan, "optimization.autoSetIp"));
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

TEST_CASE("prepare: 智能转移虚拟内存选剩余空间最大的分区")
{
    // SC 的 Rb_SmartMovePageFile 就是这件事：只搬位置，大小仍交给系统（0 0）
    {
        osp::config::Config cfg;
        cfg.optimization.autoPagefileDrive = true;
        osp::prepare::Options options = osp::prepare::defaultOptions();
        options.systemDrive = "C:\\";
        options.pagefileCandidates = {
            {"D:", 100ull * 1024 * 1024 * 1024}, {"E:", 300ull * 1024 * 1024 * 1024}
        };

        const osp::prepare::Plan plan = osp::prepare::build(cfg, options);
        REQUIRE(plan.ok);
        const osp::prepare::Action* action = findAction(plan, "optimization.autoPagefileDrive");
        REQUIRE(action != nullptr);
        CHECK(action->requiresReboot);
        REQUIRE(action->multi.size() == 1);
        CHECK(action->multi[0] == "E:\\pagefile.sys 0 0");
    }

    // 并列时按盘符取前者，保证同样输入同样输出
    {
        osp::config::Config cfg;
        cfg.optimization.autoPagefileDrive = true;
        osp::prepare::Options options = osp::prepare::defaultOptions();
        options.systemDrive = "C:\\";
        options.pagefileCandidates = {{"E:", 200ull}, {"D:", 200ull}};

        const osp::prepare::Plan plan = osp::prepare::build(cfg, options);
        REQUIRE(plan.ok);
        const osp::prepare::Action* action = findAction(plan, "optimization.autoPagefileDrive");
        REQUIRE(action != nullptr);
        CHECK(action->multi[0] == "D:\\pagefile.sys 0 0");
    }

    // 没有别的固定磁盘：退回系统盘，并明确告警而不是静默失败
    {
        osp::config::Config cfg;
        cfg.optimization.autoPagefileDrive = true;
        osp::prepare::Options options = osp::prepare::defaultOptions();
        options.systemDrive = "C:\\";
        options.pagefileCandidates = {}; // 空 = 退回现场枚举；本机有别的盘就会选中它

        const osp::prepare::Plan plan = osp::prepare::build(cfg, options);
        CHECK(plan.ok);
        const osp::prepare::Action* action = findAction(plan, "optimization.autoPagefileDrive");
        REQUIRE(action != nullptr);
        CHECK_FALSE(action->multi.empty());
        CHECK(action->multi[0].find("pagefile.sys 0 0") != std::string::npos);
    }
}

TEST_CASE("prepare: 指定分区与智能转移同时开启时以指定为准并告警")
{
    osp::config::Config cfg;
    cfg.optimization.autoPagefileDrive = true;
    cfg.optimization.pageFileTarget = "F:\\";
    osp::prepare::Options options = osp::prepare::defaultOptions();
    options.systemDrive = "C:\\";
    options.pagefileCandidates = {{"E:", 999ull * 1024 * 1024 * 1024}};

    const osp::prepare::Plan plan = osp::prepare::build(cfg, options);
    REQUIRE(plan.ok);
    // 动作落在「指定分区」这条腿上
    const osp::prepare::Action* action = findAction(plan, "optimization.pageFileTarget");
    REQUIRE(action != nullptr);
    CHECK(action->multi[0] == "F:\\pagefile.sys 0 0");
    CHECK(findAction(plan, "optimization.autoPagefileDrive") == nullptr);

    bool warned = false;
    for (const std::string& warning : plan.warnings)
    {
        if (warning.find("同时开启") != std::string::npos)
        {
            warned = true;
        }
    }
    CHECK(warned);
}

TEST_CASE("prepare: 两个开关都不开时不碰 PagingFiles")
{
    osp::config::Config cfg;
    osp::prepare::Options options = osp::prepare::defaultOptions();
    options.systemDrive = "C:\\";
    options.pagefileCandidates = {{"D:", 1024ull * 1024 * 1024}};

    const osp::prepare::Plan plan = osp::prepare::build(cfg, options);
    REQUIRE(plan.ok);
    CHECK(findAction(plan, "optimization.pageFileTarget") == nullptr);
    CHECK(findAction(plan, "optimization.autoPagefileDrive") == nullptr);
}

TEST_CASE("prepare: 我的文档与收藏夹走离线 hive，智能/指定两种目标都对")
{
    // 指定目录（Rb_Diy）：直接用配置里的路径
    {
        osp::config::Config cfg;
        cfg.optimization.moveDocuments = true;
        cfg.optimization.documentsSmart = false;
        cfg.optimization.myDocumentsTarget = "D:\Docs";
        cfg.optimization.moveFavorites = true;
        cfg.optimization.favoritesSmart = false;
        cfg.optimization.favoritesTarget = "D:\Favs";

        osp::prepare::Options options = osp::prepare::defaultOptions();
        const osp::prepare::Plan plan = osp::prepare::build(cfg, options);
        REQUIRE(plan.ok);

        const osp::prepare::Action* docs = findAction(plan, "optimization.myDocumentsTarget");
        REQUIRE(docs != nullptr);
        CHECK(docs->kind == osp::prepare::ActionKind::HiveSet);
        CHECK(docs->hiveFile.find("NTUSER.DAT") != std::string::npos);
        CHECK(docs->key.find("User Shell Folders") != std::string::npos);
        CHECK(docs->valueName == "Personal");
        CHECK(docs->text == "D:\Docs");
        CHECK(docs->valueType == "expandSz");

        const osp::prepare::Action* favs = findAction(plan, "optimization.moveFavorites");
        REQUIRE(favs != nullptr);
        CHECK(favs->valueName == "Favorites");
        CHECK(favs->text == "D:\Favs");
    }

    // 智能转移（Rb_Smart）：选剩余最大的非系统盘
    {
        osp::config::Config cfg;
        cfg.optimization.moveDocuments = true;
        cfg.optimization.documentsSmart = true;
        cfg.optimization.myDocumentsTarget = "D:\Ignored";

        osp::prepare::Options options = osp::prepare::defaultOptions();
        options.pagefileCandidates = {
            {"E:", 500ull * 1024 * 1024 * 1024}, {"F:", 900ull * 1024 * 1024 * 1024}
        };
        const osp::prepare::Plan plan = osp::prepare::build(cfg, options);
        REQUIRE(plan.ok);

        const osp::prepare::Action* docs = findAction(plan, "optimization.myDocumentsTarget");
        REQUIRE(docs != nullptr);
        // 智能模式下忽略显式路径，改用剩余最大的 F:
        CHECK(docs->text == "F:\\My Documents");
    }

    // 没有第二个固定盘：按 SC 的说明「条件不成立则不转移」
    {
        osp::config::Config cfg;
        cfg.optimization.moveDocuments = true;
        cfg.optimization.documentsSmart = true;

        osp::prepare::Options options = osp::prepare::defaultOptions();
        options.pagefileCandidates = {}; // 现场枚举；本机有别的盘就会选到它
        const osp::prepare::Plan plan = osp::prepare::build(cfg, options);
        CHECK(plan.ok);
        // 无论是否转移成功，都不许抛异常或产出空路径的动作
        const osp::prepare::Action* docs = findAction(plan, "optimization.myDocumentsTarget");
        if (docs != nullptr)
        {
            CHECK(!docs->text.empty());
        }
    }
}

TEST_CASE("prepare: IE 主页同步写进默认 hive，且写不进去只告警不中断")
{
    osp::config::Config cfg;
    cfg.optimization.saveIeHomePage = true;
    cfg.optimization.ieHomePage = "http://example.com/osp";

    osp::prepare::Options options = osp::prepare::defaultOptions();
    const osp::prepare::Plan plan = osp::prepare::build(cfg, options);
    REQUIRE(plan.ok);

    const osp::prepare::Action* ie = findAction(plan, "optimization.saveIeHomePage");
    REQUIRE(ie != nullptr);
    CHECK(ie->kind == osp::prepare::ActionKind::HiveSet);
    CHECK(ie->valueName == "Start Page");
    CHECK(ie->text == "http://example.com/osp");
    // Win10/11 上这个键带拒绝写入的 ACL，必须允许降级为告警 —— 否则整份封装会失败
    CHECK(ie->tolerateFailure);
}

TEST_CASE("prepare: 默认用户 hive 路径可覆盖")
{
    osp::config::Config cfg;
    cfg.optimization.moveDocuments = true;
    cfg.optimization.documentsSmart = false;
    cfg.optimization.myDocumentsTarget = "D:\Docs";
    cfg.optimization.defaultUserHive = "T:\\copy\\ntuser.dat";

    osp::prepare::Options options = osp::prepare::defaultOptions();
    const osp::prepare::Plan plan = osp::prepare::build(cfg, options);
    REQUIRE(plan.ok);
    const osp::prepare::Action* docs = findAction(plan, "optimization.myDocumentsTarget");
    REQUIRE(docs != nullptr);
    CHECK(docs->hiveFile == "T:\\copy\\ntuser.dat");
}

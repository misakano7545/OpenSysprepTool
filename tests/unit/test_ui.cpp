// UI 页面模型单测（宪章 §8 M7b）
//
// 覆盖：7 个页签的清单与 DFM 图纸一致 / 红线字段一律不出现 /
// 下拉取值来自 core/runplan 或 2026-10-25 裁决 / 配置 -> 页面 -> 配置 往返一致 /
// 任务草稿的读回与写回。窗口层由 --ui-smoke-test 覆盖。

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "core/config.hpp"
#include "core/runplan.hpp"
#include "ui.hpp"

namespace {

// 一份写死的体检报告：母盘系统页里有若干显示值直接来自实时系统状态
// （系统分区已用容量、已用比例），现场采集时两次渲染都可能不同，
// 所以要逐字比较渲染结果就得先把这份报告钉住。
osp::sysinfo::CheckupReport fixedReport()
{
    osp::sysinfo::CheckupReport report;
    report.generatedAtUtc = "2026-01-01T00:00:00Z";
    report.hostName = "OSPT-TEST";
    report.os.productName = "Windows 10 Pro";
    report.os.editionId = "Professional";
    report.os.version = "10.0.19045.4046";
    report.os.architecture = "x64";
    report.os.is64BitOs = true;
    report.sysprep.available = true;

    osp::sysinfo::PartitionInfo system;
    system.drive = "C:";
    system.volumeLabel = "SYSTEM";
    system.fileSystem = "NTFS";
    system.driveType = "fixed";
    system.totalBytes = 200ull * 1024 * 1024 * 1024;
    system.usedBytes = 100ull * 1024 * 1024 * 1024;
    system.freeBytes = system.totalBytes - system.usedBytes;
    system.usedPercent = 50.0;
    system.systemDrive = true;
    report.partitions.push_back(system);
    return report;
}

// 某页面的全部字段（扁平）
std::vector<osp::ui::Field> fieldsOf(const osp::ui::Page& page)
{
    std::vector<osp::ui::Field> all;
    for (const osp::ui::Group& group : page.groups)
    {
        for (const osp::ui::Field& field : group.fields)
        {
            all.push_back(field);
        }
    }
    return all;
}

const osp::ui::Field* fieldOf(const osp::ui::Page& page, const std::string& id)
{
    return osp::ui::findField(page, id);
}

bool contains(const std::vector<std::string>& values, const std::string& needle)
{
    return std::find(values.begin(), values.end(), needle) != values.end();
}

} // namespace

TEST_CASE("交付的页签清单与 DFM 的页对象一一对应")
{
    const osp::config::Config cfg;
    const std::vector<osp::ui::Page> pages = osp::ui::pagesFromConfig(cfg, fixedReport());

    REQUIRE(pages.size() == static_cast<std::size_t>(osp::ui::kConfigPageCount));

    const std::vector<std::string> ids = {"system", "target", "tasks", "deploy", "sysprep", "run", "done"};
    const std::vector<std::string> titles = {
        "母盘系统", "目标系统", "计划任务", "部署过程", "封装过程", "运行", "完成"
    };
    const std::vector<std::string> sheets = {
        "MlSkinTabSheet1",
        "MlSkinTabSheet2",
        "MlSkinTabSheet3",
        "MlSkinTabSheet4",
        "MlSkinTabSheet5",
        "MlSkinTabSheet6",
        "MlSkinTabSheet7"
    };

    for (std::size_t i = 0; i < pages.size(); ++i)
    {
        CHECK(pages[i].id == ids[i]);
        CHECK(pages[i].title == titles[i]);
        CHECK(pages[i].dfmSheet == sheets[i]);
    }

    // 更新/更新中/End/Temp 不是配置页（2026-10-25 裁决：依赖 SC 服务器的两页不做）
    for (const osp::ui::Page& page : pages)
    {
        CHECK(page.dfmSheet != "MlSkinTabSheet8");
        CHECK(page.dfmSheet != "MlSkinTabSheet9");
        CHECK(page.dfmSheet != "MlSkinTabSheet10");
        CHECK(page.dfmSheet != "MlSkinTabSheet11");
    }
}

TEST_CASE("每个页面的字段与走查名单一致，且字段名互不重复")
{
    const osp::config::Config cfg;
    const std::vector<osp::ui::Page> pages = osp::ui::pagesFromConfig(cfg, fixedReport());

    std::set<std::string> seen;
    for (const osp::ui::Page& page : pages)
    {
        const std::vector<std::string> expected = osp::ui::expectedControlIds(page.id);
        const std::vector<osp::ui::Field> fields = fieldsOf(page);

        CHECK(fields.size() == expected.size());
        // 只比集合，不比顺序：坐标表是生成的，且「我方扩展字段」固定被挪到页面末尾
        // （留在原分组里会被多加一次组框原点偏移）。顺序在 DFM 那边由生成器保证。
        std::vector<std::string> actualIds;
        actualIds.reserve(fields.size());
        for (const osp::ui::Field& field : fields)
        {
            actualIds.push_back(field.id);
        }
        std::vector<std::string> sortedExpected = expected;
        std::sort(sortedExpected.begin(), sortedExpected.end());
        std::sort(actualIds.begin(), actualIds.end());
        CHECK(actualIds == sortedExpected);
        for (const osp::ui::Field& field : fields)
        {
            // 控件名不能跨页重复，否则走查与冒烟驱动都会认错控件
            CHECK(seen.insert(field.id).second);
            // 每个控件都要带文字。图纸绝对布局里的 Lab_*（运行/完成页）本身就是标签，
            // Caption 记在 value 上、label 为空是正常的，所以两者有一即可。
            // doctest 不允许在 CHECK 里写 || 复合表达式（Expression Too Complex），先算成 bool。
            const bool hasText = !field.label.empty() || !field.value.empty();
            CHECK(hasText);
        }
    }
}

TEST_CASE("宪章 §7 红线字段不出现在任何页面上")
{
    const osp::config::Config cfg;
    const std::vector<osp::ui::Page> pages = osp::ui::pagesFromConfig(cfg, fixedReport());

    const std::vector<std::string> excluded = osp::ui::excludedControlIds();
    const std::vector<std::string> forbidden = osp::ui::forbiddenBindings();

    // 名单本身要覆盖到这几类
    CHECK(contains(excluded, "Gb_ScTools"));         // 系统总裁产品整组
    CHECK(contains(excluded, "Mkb_Ceounion"));       // 装机联盟账户
    CHECK(contains(excluded, "Ed_Ceounionuid"));     // 联盟计费 ID
    CHECK(contains(excluded, "Cb_Saveiehp"));        // 同步封装前 IE 主页
    CHECK(contains(excluded, "Cb_SaveieFavorites")); // 同步封装前 IE 收藏夹
    // 任务栏固定机制已在 B9 实现（只接受用户列出的文件），所以它不再属于「不做」
    CHECK(contains(excluded, "Rb_Sysprep_Type2")); // 联盟封装方式
    CHECK(contains(excluded, "Rb_Sysprep_Type3")); // 会员封装方式
    CHECK(contains(excluded, "Btn_Bugs"));         // 错误报告
    CHECK(contains(excluded, "Btn_Update"));       // 更新页
    CHECK(contains(excluded, "Page_Nodelfile"));   // 需内核驱动，§7 禁止
    CHECK(contains(excluded, "Btn_Execute"));      // 立即执行按钮不在界面上（要命令行）

    // 已经实现的不许留在「不做」名单里 —— M7 时期的名单就犯过这个错，
    // 把后来做出来的 Ctrl+F11、OEM 设置、部署标题、虚拟内存转移都列成了不做。
    for (const char* done :
         {"Cb_ResolutionSos",
          "Cb_CloseRestartbox",
          "Cb_DeployTitle",
          "Btn_OemSet",
          "Cb_MovePageFile",
          "Cb_MoveDocuments",
          "Cb_MoveFavorites",
          // B9 落地：任务栏固定/取消固定（只接受用户列出的文件）
          "Cb_Locktaskbar",
          "Cb_Unlocktaskbar"})
    {
        CHECK_FALSE(contains(excluded, done));
    }

    for (const osp::ui::Page& page : pages)
    {
        for (const osp::ui::Field& field : fieldsOf(page))
        {
            CHECK(!contains(excluded, field.id));
            if (!field.binding.empty())
            {
                CHECK(!contains(forbidden, field.binding));
            }
        }
    }
}

TEST_CASE("交互控件的取值来源都在 note 里写明，且可编辑字段都绑到配置模型")
{
    const osp::config::Config cfg;
    const std::vector<osp::ui::Page> pages = osp::ui::pagesFromConfig(cfg, fixedReport());

    std::size_t bound = 0;
    for (const osp::ui::Page& page : pages)
    {
        for (const osp::ui::Field& field : fieldsOf(page))
        {
            // 只读展示与运行期字段不需要 note
            if (field.kind != osp::ui::FieldKind::Info && field.kind != osp::ui::FieldKind::Progress)
            {
                CHECK(!field.note.empty());
            }
            if (!field.binding.empty())
            {
                ++bound;
                CHECK(field.kind != osp::ui::FieldKind::Info);
            }
        }
    }
    // 绑到配置模型的字段数量下限（防止哪天误删大批绑定）
    CHECK(bound >= 40);
}

TEST_CASE("运行期页面（运行/完成）不绑定配置模型")
{
    const osp::config::Config cfg;
    const std::vector<osp::ui::Page> pages = osp::ui::pagesFromConfig(cfg, fixedReport());

    for (const osp::ui::Page& page : pages)
    {
        const bool runtime = (page.id == "run" || page.id == "done");
        CHECK(page.configPage == !runtime);
        if (runtime)
        {
            for (const osp::ui::Field& field : fieldsOf(page))
            {
                CHECK(field.binding.empty());
            }
        }
    }
}

TEST_CASE("运行方式/运行等待/执行路径/任务类型的取值就是 core/runplan 的四个维度")
{
    const osp::config::Config cfg;
    const std::vector<osp::ui::Page> pages = osp::ui::pagesFromConfig(cfg, fixedReport());
    const osp::ui::Page* tasks = osp::ui::findPage(pages, "tasks");
    REQUIRE(tasks != nullptr);

    const osp::ui::Field* taskType = fieldOf(*tasks, "Cb_Tasktype");
    REQUIRE(taskType != nullptr);
    REQUIRE(taskType->optionIds.size() == osp::runplan::allTaskTypes().size());
    for (std::size_t i = 0; i < taskType->optionIds.size(); ++i)
    {
        CHECK(taskType->optionIds[i] == osp::runplan::taskTypeId(osp::runplan::allTaskTypes()[i]));
    }

    const osp::ui::Field* runMode = fieldOf(*tasks, "Cb_RunMode");
    REQUIRE(runMode != nullptr);
    REQUIRE(runMode->optionIds.size() == osp::runplan::allWindowStyles().size());
    for (std::size_t i = 0; i < runMode->optionIds.size(); ++i)
    {
        CHECK(runMode->optionIds[i] == osp::runplan::windowStyleId(osp::runplan::allWindowStyles()[i]));
    }

    const osp::ui::Field* runWait = fieldOf(*tasks, "Cb_RunWait");
    REQUIRE(runWait != nullptr);
    REQUIRE(runWait->optionIds.size() == osp::runplan::allWaitStrategies().size());
    for (std::size_t i = 0; i < runWait->optionIds.size(); ++i)
    {
        CHECK(runWait->optionIds[i] == osp::runplan::waitStrategyId(osp::runplan::allWaitStrategies()[i]));
    }

    const osp::ui::Field* runPath = fieldOf(*tasks, "Cb_RunPath");
    REQUIRE(runPath != nullptr);
    REQUIRE(runPath->optionIds.size() == osp::runplan::allPathKinds().size());
    for (std::size_t i = 0; i < runPath->optionIds.size(); ++i)
    {
        CHECK(runPath->optionIds[i] == osp::runplan::pathKindId(osp::runplan::allPathKinds()[i]));
    }
}

TEST_CASE("五个阶段按钮的标签与 core/runplan 的阶段名一致")
{
    const osp::config::Config cfg;
    const std::vector<osp::ui::Page> pages = osp::ui::pagesFromConfig(cfg, fixedReport());
    const osp::ui::Page* tasks = osp::ui::findPage(pages, "tasks");
    REQUIRE(tasks != nullptr);

    const std::vector<osp::runplan::Phase>& phases = osp::runplan::allPhases();
    REQUIRE(phases.size() == 5);
    for (std::size_t i = 0; i < phases.size(); ++i)
    {
        const osp::ui::Field* field = fieldOf(*tasks, "Btn_RunPlan" + std::to_string(i + 1));
        REQUIRE(field != nullptr);
        CHECK(field->kind == osp::ui::FieldKind::Radio);
        CHECK(field->label == osp::runplan::phaseLabel(phases[i]));
        CHECK(field->checked == (i == 0)); // 默认落在第一个阶段
    }
}

TEST_CASE("善后处理的四个取值按 2026-10-25 裁决落定")
{
    const osp::config::Config cfg;
    const std::vector<osp::ui::Page> pages = osp::ui::pagesFromConfig(cfg, fixedReport());
    const osp::ui::Page* tasks = osp::ui::findPage(pages, "tasks");
    REQUIRE(tasks != nullptr);

    const osp::ui::Field* after = fieldOf(*tasks, "Cb_RunAfter");
    REQUIRE(after != nullptr);
    REQUIRE(after->optionIds.size() == 4);
    CHECK(after->optionIds[0] == "keep");
    CHECK(after->optionIds[1] == "deleteFile");
    CHECK(after->optionIds[2] == "deleteDir");
    CHECK(after->optionIds[3] == "reboot");
    CHECK(after->options[0] == "不处理");
    CHECK(after->options[1] == "删除文件");
    CHECK(after->options[2] == "删除目录");
    CHECK(after->options[3] == "重启");
}

TEST_CASE("分辨率下拉的条目就是 DFM 里 Cb_Resolution 的 11 条")
{
    const osp::config::Config cfg;
    const std::vector<osp::ui::Page> pages = osp::ui::pagesFromConfig(cfg, fixedReport());
    const osp::ui::Page* target = osp::ui::findPage(pages, "target");
    REQUIRE(target != nullptr);

    const osp::ui::Field* resolution = fieldOf(*target, "Cb_Resolution");
    REQUIRE(resolution != nullptr);
    REQUIRE(resolution->options.size() == 11);
    // 条目逐字取自 DFM 的 Items.Strings：分隔符是 *，不是 x
    CHECK(resolution->options[0] == "800*600");
    CHECK(resolution->options[1] == "1024*768");
    CHECK(resolution->options[10] == "1920*1200");
}

TEST_CASE("配置 -> 页面 -> 配置 往返一致")
{
    // 只改「有绑定」的字段，这样未绑定的字段在两次渲染里都是默认值，页面可以逐字比较
    osp::config::Config source;
    source.systemInfo.registeredName = "小御坂";
    source.systemInfo.registeredOrg = "OpenSysprepTool";
    source.systemInfo.serialNumber = "AAAAA-BBBBB-CCCCC-DDDDD-EEEEE";
    source.systemInfo.computerName = "OSPT-";
    source.systemInfo.computerNameMode = "prefixRandom";
    source.systemInfo.bootMenuTimeoutSec = 5;
    source.firstLogon.runDeviceManager = true;
    source.firstLogon.rebootAfterRun = true;
    source.firstLogon.resolutionX = 1280;
    source.firstLogon.resolutionY = 1024;
    source.firstLogon.refreshRate = 75;
    source.firstLogon.colorDepth = 16;
    source.optimization.autoSetIp = true;
    source.optimization.closeNumLock = true;
    source.optimization.enableHibernate = true;
    source.optimization.clearSrsDrivers = true;
    source.optimization.setResolution = true;
    source.optimization.autoNetworkLocation = true;
    source.deploy.backgroundImage = "O:\\bg\\a.jpg";
    source.deploy.imageSwitchSeconds = 25;
    source.deploy.deployResolutionX = 1024;
    source.deploy.deployResolutionY = 768;
    source.deploy.fontColor = "#102030";
    source.deploy.titleText = "装机中，请勿断电";
    source.deploy.skipOobe = true;
    source.deploy.autoCreateUser = true;
    source.deploy.blockVirtualMachine = true;
    source.deploy.verifyIntegrity = true;
    source.deploy.enableAdministrator = true;
    source.sysprep.mode = "classic";
    source.sysprep.uninstallDrivers = true;
    source.sysprep.loadSrsDrivers = true;
    source.sysprep.generateNewSid = false;
    source.sysprep.transferDesktopFiles = true;
    source.sysprep.cleanTemp = true;
    source.sysprep.cleanDllCache = true;
    source.sysprep.workgroup = "OSPTWG";
    source.sysprep.domain = "osp.local";
    source.sysprep.domainUser = "administrator";
    source.sysprep.domainPassword = "s3cret";
    source.sysprep.perServerLicensing = true;
    source.sysprep.maxConnections = 25;
    source.sysprep.afterAction = "reboot";

    const std::string before = osp::ui::toJson(osp::ui::pagesFromConfig(source, fixedReport()));

    osp::config::Config restored;
    for (const osp::ui::Page& page : osp::ui::pagesFromConfig(source, fixedReport()))
    {
        osp::ui::applyTo(page, restored);
    }

    // 关键字段逐项核对
    CHECK(restored.systemInfo.registeredName == source.systemInfo.registeredName);
    CHECK(restored.systemInfo.computerNameMode == source.systemInfo.computerNameMode);
    CHECK(restored.systemInfo.bootMenuTimeoutSec == 5);
    CHECK(restored.firstLogon.runDeviceManager);
    CHECK(restored.firstLogon.resolutionX == 1280);
    CHECK(restored.firstLogon.resolutionY == 1024);
    CHECK(restored.firstLogon.refreshRate == 75);
    CHECK(restored.firstLogon.colorDepth == 16);
    CHECK(restored.optimization.autoNetworkLocation);
    CHECK(restored.optimization.clearSrsDrivers);
    CHECK(restored.optimization.setResolution);
    CHECK(restored.deploy.backgroundImage == source.deploy.backgroundImage);
    CHECK(restored.deploy.imageSwitchSeconds == 25);
    CHECK(restored.deploy.deployResolutionX == 1024);
    CHECK(restored.deploy.deployResolutionY == 768);
    CHECK(restored.deploy.fontColor == "#102030");
    CHECK(restored.deploy.titleText == source.deploy.titleText);
    CHECK(restored.deploy.enableAdministrator);
    CHECK(restored.deploy.verifyIntegrity);
    CHECK(!restored.sysprep.generateNewSid);
    CHECK(restored.sysprep.workgroup == "OSPTWG");
    CHECK(restored.sysprep.domain == "osp.local");
    CHECK(restored.sysprep.domainUser == "administrator");
    CHECK(restored.sysprep.domainPassword == "s3cret");
    CHECK(restored.sysprep.perServerLicensing);
    CHECK(restored.sysprep.maxConnections == 25);
    CHECK(restored.sysprep.afterAction == "reboot");

    // 再渲染一遍应当与第一次逐字一致。
    // 必须注入固定报告：母盘系统页里有「已用容量 / 已用比例」这种随时间变动的显示值，
    // 现场采集时两次渲染可能差 0.1 GB（本机实测过 122.1 → 122.0），断言就会偶发失败。
    CHECK(osp::ui::toJson(osp::ui::pagesFromConfig(restored, fixedReport())) == before);
}

TEST_CASE("注入固定报告后，渲染结果逐字可复现（母盘系统页的易变值不再影响断言）")
{
    const osp::config::Config cfg;
    const osp::ui::Page a = osp::ui::systemPage(fixedReport());
    const osp::ui::Page b = osp::ui::systemPage(fixedReport());
    CHECK(osp::ui::toJson(std::vector<osp::ui::Page>{a}) == osp::ui::toJson(std::vector<osp::ui::Page>{b}));
    CHECK(osp::ui::toJson(std::vector<osp::ui::Page>{a}) == osp::ui::toJson(std::vector<osp::ui::Page>{b}));

    // 固定报告里的值确实被渲染出来了，不是「都空着所以相等」
    const std::string json = osp::ui::toJson(osp::ui::pagesFromConfig(cfg, fixedReport()));
    CHECK(json.find("100.0 GB / 200.0 GB") != std::string::npos);
    CHECK(json.find("50.0%") != std::string::npos);
}

TEST_CASE("工作组与加入域互斥：选工作组会清掉域信息")
{
    osp::config::Config cfg;
    cfg.sysprep.domain = "corp.local";
    cfg.sysprep.domainUser = "admin";
    cfg.sysprep.domainPassword = "pw";

    std::vector<osp::ui::Page> pages = osp::ui::pagesFromConfig(cfg, fixedReport());
    osp::ui::Page* sysprep = osp::ui::findPage(pages, "sysprep");
    REQUIRE(sysprep != nullptr);

    // 页面初值：domain 非空 -> 加入域被选中
    REQUIRE(fieldOf(*sysprep, "Rb_Domain") != nullptr);
    CHECK(fieldOf(*sysprep, "Rb_Domain")->checked);
    CHECK(!fieldOf(*sysprep, "Rb_WorkGroup")->checked);

    // 改选工作组并填名字
    osp::ui::findField(*sysprep, "Rb_WorkGroup")->checked = true;
    osp::ui::findField(*sysprep, "Rb_Domain")->checked = false;
    osp::ui::findField(*sysprep, "Ed_WorkGroup")->value = "WORKGROUP";

    osp::ui::applyTo(*sysprep, cfg);
    CHECK(cfg.sysprep.domain.empty());
    CHECK(cfg.sysprep.domainUser.empty());
    CHECK(cfg.sysprep.domainPassword.empty());
    CHECK(cfg.sysprep.workgroup == "WORKGROUP");
}

TEST_CASE("计划任务页的草稿行能读成一条运行计划任务")
{
    const osp::config::Config cfg;
    std::vector<osp::ui::Page> pages = osp::ui::pagesFromConfig(cfg, fixedReport());
    osp::ui::Page* tasks = osp::ui::findPage(pages, "tasks");
    REQUIRE(tasks != nullptr);

    // 阶段选到第 5 个（进桌面），类型选到删除目录，运行方式选到隐藏，等待选到自定义
    for (int i = 1; i <= 5; ++i)
    {
        osp::ui::findField(*tasks, "Btn_RunPlan" + std::to_string(i))->checked = (i == 5);
    }
    osp::ui::findField(*tasks, "Cb_Tasktype")->comboIndex = 3;
    osp::ui::findField(*tasks, "Cb_RunMode")->comboIndex = 1;
    osp::ui::findField(*tasks, "Cb_RunWait")->comboIndex = 2;
    osp::ui::findField(*tasks, "Cb_RunAfter")->comboIndex = 2;
    osp::ui::findField(*tasks, "Cb_RunPath")->comboIndex = 1;
    osp::ui::findField(*tasks, "Sed_WaitTime")->value = "30";
    osp::ui::findField(*tasks, "Ed_RunPath")->value = "tools\\install.cmd";
    osp::ui::findField(*tasks, "Ed_CmdLine")->value = "/silent";

    const osp::config::RunPlanTask task = osp::ui::taskFromPage(*tasks);
    CHECK(task.phase == "desktop");
    CHECK(task.type == "deleteDir");
    CHECK(task.windowStyle == "hidden");
    CHECK(task.waitMode == "custom");
    CHECK(task.waitSeconds == 30);
    CHECK(task.cleanup == "deleteDir");
    CHECK(task.searchScope == "usbDrive");
    CHECK(task.target == "tools\\install.cmd");
    CHECK(task.arguments == "/silent");

    // 写回页面：再读一次应当一致
    osp::ui::fillTaskPage(*tasks, task);
    const osp::config::RunPlanTask again = osp::ui::taskFromPage(*tasks);
    CHECK(again.phase == task.phase);
    CHECK(again.type == task.type);
    CHECK(again.windowStyle == task.windowStyle);
    CHECK(again.waitMode == task.waitMode);
    CHECK(again.waitSeconds == task.waitSeconds);
    CHECK(again.target == task.target);
    CHECK(again.arguments == task.arguments);
}

TEST_CASE("任务列表的行数与运行计划条数一致")
{
    osp::config::Config cfg;
    osp::config::RunPlanTask first;
    first.phase = "logon";
    first.type = "run";
    first.target = "a.cmd";
    first.arguments = "/a";
    cfg.runPlan.push_back(first);

    osp::config::RunPlanTask second;
    second.phase = "desktop";
    second.type = "reg";
    second.target = "b.reg";
    cfg.runPlan.push_back(second);

    const std::vector<osp::ui::Page> pages = osp::ui::pagesFromConfig(cfg, fixedReport());
    const osp::ui::Page* tasks = osp::ui::findPage(pages, "tasks");
    REQUIRE(tasks != nullptr);

    const osp::ui::Field* list = fieldOf(*tasks, "Lv_Tasklist");
    REQUIRE(list != nullptr);
    REQUIRE(list->rows.size() == 2);
    CHECK(list->columns.size() == 4);
    CHECK(list->rows[0][0] == "登录时"); // 阶段显示成中文标签
    CHECK(list->rows[0][2] == "a.cmd");
    CHECK(list->rows[1][0] == "进桌面");
    CHECK(list->rows[1][1] == "reg");
}

TEST_CASE("toJson 报告带页面、绑定与不做清单")
{
    const std::string json = osp::ui::toJson(osp::ui::pagesFromConfig(osp::config::Config(), fixedReport()));

    CHECK(json.find("\"schemaVersion\": 2") != std::string::npos);
    CHECK(json.find("\"configPageCount\": 7") != std::string::npos);
    for (const char* id : {"system", "target", "tasks", "deploy", "sysprep", "run", "done"})
    {
        CHECK(json.find(std::string("\"id\": \"") + id + "\"") != std::string::npos);
    }
    CHECK(json.find("\"excludedControls\"") != std::string::npos);
    CHECK(json.find("\"forbiddenBindings\"") != std::string::npos);
    CHECK(json.find("Gb_ScTools") != std::string::npos);
    CHECK(json.find("\"kind\": \"progress\"") != std::string::npos);
    CHECK(json.find("\"kind\": \"list\"") != std::string::npos);
    CHECK(json.find("\"binding\": \"deploy.fontColor\"") != std::string::npos);
}

TEST_CASE("无头跳过的标记常量稳定（CI 按它判断是否显式声明）")
{
    CHECK(std::string(osp::ui::kSkipMarker) == "SMOKE-SKIP");
}

TEST_CASE("全部配置页能合并成一份配置，运行期页面不参与")
{
    std::vector<osp::ui::Page> pages = osp::ui::pagesFromConfig(osp::config::Config(), fixedReport());

    osp::ui::findField(*osp::ui::findPage(pages, "target"), "Ed_Owner")->value = "小御坂";
    osp::ui::findField(*osp::ui::findPage(pages, "target"), "Cb_ShowDevmgmt")->checked = true;
    osp::ui::findField(*osp::ui::findPage(pages, "deploy"), "Ed_DeployFontcolour")->value = "#ABCDEF";
    // 域信息只有在「加入域」被选中时才会写进配置：选中「工作组」时域输入会被清掉
    osp::ui::findField(*osp::ui::findPage(pages, "sysprep"), "Rb_WorkGroup")->checked = false;
    osp::ui::findField(*osp::ui::findPage(pages, "sysprep"), "Rb_Domain")->checked = true;
    osp::ui::findField(*osp::ui::findPage(pages, "sysprep"), "Ed_Domain_name")->value = "corp.local";
    osp::ui::findField(*osp::ui::findPage(pages, "sysprep"), "Ed_Domain_pw")->value = "pw123";

    const osp::config::Config merged = osp::ui::configFromPages(pages);
    CHECK(merged.systemInfo.registeredName == "小御坂");
    CHECK(merged.firstLogon.runDeviceManager);
    CHECK(merged.deploy.fontColor == "#ABCDEF");
    CHECK(merged.sysprep.domain == "corp.local");
    CHECK(merged.sysprep.domainPassword == "pw123");

    // 与「逐页 applyTo」的结果必须一致：configFromPages 只是把这一步包起来
    osp::config::Config manual;
    for (const osp::ui::Page& page : pages)
    {
        if (page.configPage)
        {
            osp::ui::applyTo(page, manual);
        }
    }
    CHECK(osp::config::toJson(merged) == osp::config::toJson(manual));

    // 运行 / 完成页在模型里就不带绑定，合并时也不会把它们的值写进配置
    for (const char* id : {"run", "done"})
    {
        const osp::ui::Page* page = osp::ui::findPage(pages, id);
        REQUIRE(page != nullptr);
        CHECK(!page->configPage);
    }
}

TEST_CASE("配置文件读写往返一致，错误路径都给出原因")
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ospt-ui-unit";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path goodPath = dir / "roundtrip.json";

    osp::config::Config cfg;
    cfg.systemInfo.registeredName = "小御坂";
    cfg.systemInfo.bootMenuTimeoutSec = 7;
    cfg.deploy.fontColor = "#102030";
    cfg.sysprep.afterAction = "reboot";

    std::string error;
    CHECK(osp::ui::saveConfigFile(goodPath.string(), cfg, error));
    CHECK(error.empty());

    osp::config::Config loaded;
    CHECK(osp::ui::loadConfigFile(goodPath.string(), loaded, error));
    CHECK(error.empty());
    CHECK(loaded.systemInfo.registeredName == "小御坂");
    CHECK(loaded.systemInfo.bootMenuTimeoutSec == 7);
    CHECK(loaded.deploy.fontColor == "#102030");
    CHECK(osp::config::toJson(loaded) == osp::config::toJson(cfg));

    // 非法 JSON：必须拒绝并说明原因
    const std::filesystem::path badJson = dir / "broken.json";
    {
        std::ofstream out(badJson, std::ios::binary | std::ios::trunc);
        out << "{ this is not json ";
    }
    osp::config::Config ignored;
    error.clear();
    CHECK_FALSE(osp::ui::loadConfigFile(badJson.string(), ignored, error));
    CHECK(!error.empty());

    // 空文件
    const std::filesystem::path emptyPath = dir / "empty.json";
    {
        std::ofstream out(emptyPath, std::ios::binary | std::ios::trunc);
    }
    error.clear();
    CHECK_FALSE(osp::ui::loadConfigFile(emptyPath.string(), ignored, error));
    CHECK(!error.empty());

    // 不存在的文件
    error.clear();
    CHECK_FALSE(osp::ui::loadConfigFile((dir / "no-such-file.json").string(), ignored, error));
    CHECK(!error.empty());

    // schemaVersion 不符
    const std::filesystem::path wrongSchema = dir / "wrong-schema.json";
    {
        std::ofstream out(wrongSchema, std::ios::binary | std::ios::trunc);
        out << "{\"schemaVersion\": 99}";
    }
    error.clear();
    CHECK_FALSE(osp::ui::loadConfigFile(wrongSchema.string(), ignored, error));
    CHECK(error.find("schemaVersion") != std::string::npos);

    // 保存到不存在的目录：必须失败并给出原因，而不是崩
    error.clear();
    CHECK_FALSE(osp::ui::saveConfigFile((dir / "no-such-dir" / "x.json").string(), cfg, error));
    CHECK(!error.empty());
}

TEST_CASE("系统名称不会把版本名重复拼两遍")
{
    // Windows 11 上 ProductName 报 "Windows 10 Enterprise"，EditionID 又是 "Enterprise"，
    // 早期实现直接拼接会显示成「Windows 10 Enterprise Enterprise」
    const osp::ui::Page page = osp::ui::systemPage();
    const osp::ui::Field* field = osp::ui::findField(page, "Lab_Osnamev");
    REQUIRE(field != nullptr);

    std::istringstream stream(field->value);
    const std::vector<std::string> words{
        std::istream_iterator<std::string>(stream), std::istream_iterator<std::string>()
    };
    REQUIRE(!words.empty());
    for (std::size_t i = 1; i < words.size(); ++i)
    {
        CHECK(words[i] != words[i - 1]);
    }
}

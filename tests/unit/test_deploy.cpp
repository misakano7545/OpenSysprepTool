// M5：部署运行时的单元测试
//
// 重点是那条「防漂移」断言：M4 产出的 runplan.json 与 M5 读回来的模型必须逐字段
// 相等。两个模块各自演进时，只有这条断言能拦住它们悄悄错位。

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/config.hpp"
#include "core/deploy.hpp"
#include "core/runplan.hpp"

using namespace osp;

namespace {

std::filesystem::path makeTempDir(const std::string& name)
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void writeText(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

// runplan::PlanResult 没有 taskCount 成员（它只在 toJson 里临时算），测试自己数
int totalTasksOf(const std::vector<runplan::PhasePlan>& phases)
{
    int total = 0;
    for (const runplan::PhasePlan& phase : phases)
    {
        total += static_cast<int>(phase.tasks.size());
    }
    return total;
}

// 一份覆盖全部阶段、任务类型、运行方式与等待策略的配置
config::Config sampleConfig()
{
    config::Config cfg;
    cfg.schemaVersion = config::kSchemaVersion;

    const auto add = [&cfg](
                         const char* phase,
                         const char* type,
                         const char* target,
                         const char* args,
                         const char* style,
                         const char* wait,
                         int seconds,
                         const char* cleanup,
                         const char* scope,
                         const char* action
                     ) {
        config::RunPlanTask task;
        task.phase = phase;
        task.type = type;
        task.target = target;
        task.arguments = args;
        task.windowStyle = style;
        task.waitMode = wait;
        task.waitSeconds = seconds;
        task.cleanup = cleanup;
        task.searchScope = scope;
        task.timeoutAction = action;
        cfg.runPlan.push_back(task);
    };

    add("preinstall", "reg", "harness\\oem.reg", "", "hidden", "wait", 0, "keep", "usbDrive", "continue");
    add("install", "run", "setup.exe", "/quiet", "normal", "custom", 30, "keep", "cdRom", "continue");
    add("postinstall",
        "run",
        "drivers\\install.cmd",
        "",
        "minimized",
        "wait",
        0,
        "keep",
        "usbDrive",
        "continue");
    add("postinstall",
        "deleteFile",
        "logs\\setup.log",
        "",
        "maximized",
        "noWait",
        0,
        "keep",
        "allDrives",
        "continue");
    add("logon", "deleteDir", "temp\\cache", "", "hidden", "custom", 10, "keep", "allDrives", "kill");
    add("desktop", "run", "tools\\final.exe", "", "normal", "noWait", 0, "deleteFile", "usbDrive", "continue"
    );

    return cfg;
}

// 与 tests/integration/fixtures/m4_plan.json 同构的模拟驱动器表
runplan::DriveTable sampleDrives()
{
    runplan::DriveTable table;
    table.source = "file";

    const auto add = [&table](const char* root, const char* label, const char* type, bool system) {
        runplan::DriveEntry entry;
        entry.root = root;
        entry.volumeLabel = label;
        entry.driveType = type;
        entry.systemDrive = system;
        table.drives.push_back(entry);
    };

    add("C:\\", "System", "fixed", true);
    add("D:\\", "Data", "fixed", false);
    add("E:\\", "OSPT_USB", "removable", false);
    add("F:\\", "SPARE", "removable", false);
    add("G:\\", "OSPT_CD", "cdrom", false);
    return table;
}

} // namespace

TEST_CASE("读不到 runplan.json 时给出可操作的错误")
{
    const std::filesystem::path dir = makeTempDir("osp-deploy-missing");

    const deploy::Plan plan = deploy::load(dir.string());
    CHECK_FALSE(plan.ok);
    CHECK(plan.error.find("runplan.json") != std::string::npos);
    // 错误信息要告诉人下一步怎么办
    CHECK(plan.error.find("--gen-runplan") != std::string::npos);
}

TEST_CASE("runplan.json 格式错时拒绝，而不是给出半成品计划")
{
    const std::filesystem::path dir = makeTempDir("osp-deploy-badjson");

    writeText(dir / "runplan.json", "{ 这不是 JSON ");
    const deploy::Plan broken = deploy::load(dir.string());
    CHECK_FALSE(broken.ok);
    CHECK(broken.error.find("JSON") != std::string::npos);

    writeText(dir / "runplan.json", "{\"schemaVersion\": 1}");
    const deploy::Plan noPhases = deploy::load(dir.string());
    CHECK_FALSE(noPhases.ok);
    CHECK(noPhases.error.find("phases") != std::string::npos);

    writeText(dir / "runplan.json", "{\"phases\": []}");
    const deploy::Plan empty = deploy::load(dir.string());
    CHECK_FALSE(empty.ok);
}

TEST_CASE("M4 产物与 M5 读取端逐字段一致（防漂移）")
{
    const std::filesystem::path dir = makeTempDir("osp-deploy-roundtrip");

    const runplan::PlanResult built = runplan::build(sampleConfig(), sampleDrives());
    REQUIRE(built.ok);
    REQUIRE(built.phases.size() == 5);

    writeText(dir / "runplan.json", runplan::toJson(built));

    const deploy::Plan loaded = deploy::load(dir.string());
    REQUIRE(loaded.ok);
    CHECK(loaded.planFile.find("runplan.json") != std::string::npos);

    CHECK(loaded.phases.size() == built.phases.size());
    CHECK(loaded.taskCount == totalTasksOf(built.phases));
    CHECK(loaded.warnings.size() == built.warnings.size());

    std::size_t taskTotal = 0;
    for (std::size_t p = 0; p < built.phases.size() && p < loaded.phases.size(); ++p)
    {
        const runplan::PhasePlan& source = built.phases[p];
        const deploy::Phase& target = loaded.phases[p];

        const std::string label = std::string("阶段 ") + runplan::phaseId(source.phase);
        INFO(label);
        CHECK(target.id == runplan::phaseId(source.phase));
        CHECK(target.label == runplan::phaseLabel(source.phase));
        CHECK(target.script == source.scriptName);
        CHECK(target.taskCount == static_cast<int>(source.tasks.size()));

        REQUIRE(target.tasks.size() == source.tasks.size());
        for (std::size_t t = 0; t < source.tasks.size(); ++t)
        {
            const runplan::ResolvedTask& a = source.tasks[t];
            const deploy::Task& b = target.tasks[t];
            ++taskTotal;

            const std::string taskLabel = std::string("任务 ") + std::to_string(a.index);
            INFO(taskLabel);
            CHECK(b.index == a.index);
            CHECK(b.type == runplan::taskTypeId(a.type));
            CHECK(b.sourceTarget == a.sourceTarget);
            CHECK(b.resolvedPath == a.resolvedPath);
            CHECK(b.searchedDrive == a.searchedDrive);
            CHECK(b.resolvedFromDriveTable == a.resolvedFromDriveTable);
            CHECK(b.isCleanupCommand == a.isCleanupCommand);
            CHECK(b.commandLine == a.commandLine);
            CHECK(b.windowStyle == runplan::windowStyleId(a.windowStyle));
            CHECK(b.showWindow == a.showWindow);
            CHECK(b.waitStrategy == runplan::waitStrategyId(a.waitStrategy));
            CHECK(b.creationFlags == a.creationFlags);
            CHECK(b.creationFlagsName == a.creationFlagsName);
            CHECK(b.waitMode == a.waitMode);
            CHECK(b.timeoutMs == a.timeoutMs);
            CHECK(b.timeoutAction == a.timeoutAction);
        }
    }
    CHECK(taskTotal == static_cast<std::size_t>(totalTasksOf(built.phases)));
    CHECK(loaded.taskCount == static_cast<int>(taskTotal));
}

TEST_CASE("任务类型的动作描述覆盖全部四种")
{
    CHECK(std::string(deploy::taskTypeLabel("run")) == "运行程序");
    CHECK(std::string(deploy::taskTypeLabel("reg")) == "导入注册表");
    CHECK(std::string(deploy::taskTypeLabel("deleteFile")) == "删除文件");
    CHECK(std::string(deploy::taskTypeLabel("deleteDir")) == "删除目录");
    CHECK(std::string(deploy::taskTypeLabel("unknown")) == "未知任务类型");
}

TEST_CASE("等待策略的描述把三种模式与两种超时动作都说清楚")
{
    const auto make = [](const char* mode, int ms, const char* action) {
        deploy::Task task;
        task.waitMode = mode;
        task.timeoutMs = ms;
        task.timeoutAction = action;
        return task;
    };

    CHECK(deploy::describeWait(make("none", 0, "continue")) == "不等待");
    CHECK(deploy::describeWait(make("infinite", 0, "continue")) == "等待完成");

    const std::string killed = deploy::describeWait(make("timeout", 3000, "kill"));
    CHECK(killed.find("3000ms") != std::string::npos);
    CHECK(killed.find("终止") != std::string::npos);

    const std::string continued = deploy::describeWait(make("timeout", 1500, "continue"));
    CHECK(continued.find("1500ms") != std::string::npos);
    CHECK(continued.find("继续") != std::string::npos);
}

TEST_CASE("dry-run 报告逐阶段逐任务说明将要做什么")
{
    const runplan::PlanResult built = runplan::build(sampleConfig(), sampleDrives());
    REQUIRE(built.ok);

    const std::filesystem::path dir = makeTempDir("osp-deploy-dryrun");
    writeText(dir / "runplan.json", runplan::toJson(built));

    const deploy::Plan plan = deploy::load(dir.string());
    REQUIRE(plan.ok);

    const std::string report = deploy::dryRunReport(plan);

    // 五个阶段都有标题
    for (int i = 1; i <= 5; ++i)
    {
        CHECK(report.find("[" + std::to_string(i) + "/5]") != std::string::npos);
    }

    // 四种动作描述都出现
    CHECK(report.find("运行程序") != std::string::npos);
    CHECK(report.find("导入注册表") != std::string::npos);
    CHECK(report.find("删除文件") != std::string::npos);
    CHECK(report.find("删除目录") != std::string::npos);

    // 每条任务都带命令行、等待、标志、窗口四行
    CHECK(report.find("命令：") != std::string::npos);
    CHECK(report.find("等待：") != std::string::npos);
    CHECK(report.find("标志：") != std::string::npos);
    CHECK(report.find("窗口：") != std::string::npos);

    // 不等待的必须带上 CREATE_NEW_PROCESS_GROUP
    CHECK(report.find("CREATE_NEW_PROCESS_GROUP") != std::string::npos);

    // 善后处理追加的删除命令要标出来
    CHECK(report.find("善后处理") != std::string::npos);

    // 结尾必须明确声明没有副作用
    CHECK(report.find("没有启动任何进程") != std::string::npos);
}

TEST_CASE("执行报告的 JSON 结构稳定")
{
    deploy::RunResult result;
    result.ok = false;
    result.totalTasks = 3;
    result.executedTasks = 2;
    result.failedTasks = 1;
    result.abortedAtPhase = "install";
    result.abortedAtIndex = 2;
    result.elapsedMs = 1234;

    deploy::TaskOutcome outcome;
    outcome.index = 1;
    outcome.phase = "install";
    outcome.commandLine = "setup.exe /quiet";
    outcome.launched = true;
    outcome.pid = 4242;
    outcome.exitCode = 0;
    outcome.waitResult = "timeout-killed";
    outcome.elapsedMs = 999;
    outcome.ok = true;
    outcome.message = "等待超时 3000ms，已按 kill 终止进程";
    result.outcomes.push_back(outcome);
    result.rollbackRecord.push_back("[install #1] setup.exe /quiet -> timeout-killed");

    const std::string text = deploy::toJson(result);

    for (const char* key :
         {"\"ok\"",
          "\"totalTasks\"",
          "\"executedTasks\"",
          "\"failedTasks\"",
          "\"abortedAtPhase\"",
          "\"abortedAtIndex\"",
          "\"elapsedMs\"",
          "\"outcomes\"",
          "\"rollbackRecord\"",
          "\"waitResult\"",
          "\"commandLine\"",
          "\"exitCode\"",
          "\"pid\""})
    {
        const std::string label = std::string("缺少字段 ") + key;
        INFO(label);
        CHECK(text.find(key) != std::string::npos);
    }

    CHECK(text.find("timeout-killed") != std::string::npos);
    CHECK(text.find("install") != std::string::npos);
}

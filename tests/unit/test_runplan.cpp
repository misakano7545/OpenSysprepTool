#include <windows.h>

#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "core/config.hpp"
#include "core/runplan.hpp"

namespace {

osp::runplan::DriveTable sampleDrives()
{
    osp::runplan::DriveTable table;
    table.source = "file";

    osp::runplan::DriveEntry system;
    system.root = "C:\\";
    system.volumeLabel = "System";
    system.driveType = "fixed";
    system.systemDrive = true;
    table.drives.push_back(system);

    osp::runplan::DriveEntry data;
    data.root = "D:\\";
    data.volumeLabel = "Data";
    data.driveType = "fixed";
    table.drives.push_back(data);

    osp::runplan::DriveEntry usb;
    usb.root = "E:\\";
    usb.volumeLabel = "OSPT_USB";
    usb.driveType = "removable";
    table.drives.push_back(usb);

    osp::runplan::DriveEntry spare;
    spare.root = "F:\\";
    spare.volumeLabel = "SPARE";
    spare.driveType = "removable";
    table.drives.push_back(spare);

    osp::runplan::DriveEntry cd;
    cd.root = "G:\\";
    cd.volumeLabel = "OSPT_CD";
    cd.driveType = "cdrom";
    table.drives.push_back(cd);

    return table;
}

osp::config::RunPlanTask makeTask(
    const char* phase,
    const char* type,
    const char* windowStyle,
    const char* waitMode,
    const char* searchScope
)
{
    osp::config::RunPlanTask task;
    task.phase = phase;
    task.type = type;
    task.windowStyle = windowStyle;
    task.waitMode = waitMode;
    task.searchScope = searchScope;
    task.cleanup = "keep";
    task.arguments = "";
    task.timeoutAction = "continue";

    if (std::string(waitMode) == "custom")
    {
        task.waitSeconds = 5;
    }

    // systemPath 需要绝对路径，其余路径类型用相对路径
    task.target = (std::string(searchScope) == "systemPath") ? "C:\\tools\\app.exe" : "tools\\app.exe";
    return task;
}

osp::runplan::PlanResult buildOne(const osp::config::RunPlanTask& task)
{
    osp::config::Config cfg;
    cfg.runPlan.push_back(task);
    return osp::runplan::build(cfg, sampleDrives());
}

} // namespace

TEST_CASE("the five dimensions multiply out to 960 combinations")
{
    using namespace osp::runplan;

    CHECK(kPhaseCount == 5);
    CHECK(kTaskTypeCount == 4);
    CHECK(kWindowStyleCount == 4);
    CHECK(kWaitStrategyCount == 3);
    CHECK(kPathKindCount == 4);
    CHECK(kCombinationCount == 960);

    CHECK(allPhases().size() == 5);
    CHECK(allTaskTypes().size() == 4);
    CHECK(allWindowStyles().size() == 4);
    CHECK(allWaitStrategies().size() == 3);
    CHECK(allPathKinds().size() == 4);
}

TEST_CASE("every one of the 960 combinations resolves to the documented mapping")
{
    using namespace osp::runplan;

    int combinations = 0;
    int tasksSeen = 0;

    for (Phase phase : allPhases())
    {
        for (TaskType type : allTaskTypes())
        {
            for (WindowStyle style : allWindowStyles())
            {
                for (WaitStrategy wait : allWaitStrategies())
                {
                    for (PathKind kind : allPathKinds())
                    {
                        ++combinations;

                        const osp::config::RunPlanTask task = makeTask(
                            phaseId(phase),
                            taskTypeId(type),
                            windowStyleId(style),
                            waitStrategyId(wait),
                            pathKindId(kind)
                        );
                        const PlanResult plan = buildOne(task);

                        INFO(
                            "组合: " << std::string(phaseId(phase)) << " / " << std::string(taskTypeId(type))
                                     << " / " << std::string(windowStyleId(style)) << " / "
                                     << std::string(waitStrategyId(wait)) << " / "
                                     << std::string(pathKindId(kind))
                        );
                        REQUIRE(plan.ok);

                        // 任务落在对应阶段
                        std::size_t phaseTaskCount = 0;
                        for (const PhasePlan& item : plan.phases)
                        {
                            if (item.phase == phase)
                            {
                                phaseTaskCount = item.tasks.size();
                            }
                            else
                            {
                                CHECK(item.tasks.empty());
                            }
                        }
                        CHECK(phaseTaskCount > 0);

                        for (const PhasePlan& item : plan.phases)
                        {
                            for (const ResolvedTask& resolved : item.tasks)
                            {
                                ++tasksSeen;

                                // 运行方式 -> STARTUPINFO.wShowWindow
                                CHECK(resolved.windowStyle == style);
                                CHECK(resolved.showWindow == showWindowValue(style));

                                // 等待策略 -> 两列表
                                const WaitMapping expected = waitMapping(wait, resolved.timeoutMs);
                                CHECK(resolved.creationFlags == expected.creationFlags);
                                CHECK(resolved.waitMode == std::string(expected.waitMode));

                                if (wait == WaitStrategy::NoWait)
                                {
                                    CHECK(resolved.creationFlags == CREATE_NEW_PROCESS_GROUP);
                                    CHECK(resolved.waitMode == "none");
                                }
                                else if (wait == WaitStrategy::WaitExit)
                                {
                                    CHECK(resolved.creationFlags == 0u);
                                    CHECK(resolved.waitMode == "infinite");
                                }
                                else
                                {
                                    CHECK(resolved.creationFlags == 0u);
                                    CHECK(resolved.waitMode == "timeout");
                                    CHECK(resolved.timeoutMs == 5000);
                                    CHECK(resolved.timeoutAction == "continue");
                                }

                                // 任务类型 -> 命令行形态
                                const std::string& line = resolved.commandLine;
                                switch (type)
                                {
                                case TaskType::Run: {
                                    // 驱动器表里任一盘符都算合法；doctest 无法分解 ||，先算成布尔量
                                    const char* const kRoots[] = {"C:\\", "D:\\", "E:\\", "F:\\", "G:\\"};
                                    bool startsOnKnownDrive = false;
                                    for (const char* root : kRoots)
                                    {
                                        if (line.rfind(root, 0) == 0)
                                        {
                                            startsOnKnownDrive = true;
                                            break;
                                        }
                                    }
                                    CHECK(startsOnKnownDrive);
                                    break;
                                }
                                case TaskType::ImportReg:
                                    CHECK(line.rfind("reg.exe import ", 0) == 0);
                                    break;
                                case TaskType::RemoveFile:
                                    CHECK(line.rfind("cmd /c del /f /q ", 0) == 0);
                                    break;
                                case TaskType::RemoveDir:
                                    CHECK(line.rfind("cmd /c rd /s /q ", 0) == 0);
                                    break;
                                }
                            }
                        }

                        // 路径类型 -> 解析来源
                        const std::vector<ResolvedTask>& phaseTasks =
                            plan.phases[static_cast<std::size_t>(phase)].tasks;
                        REQUIRE_FALSE(phaseTasks.empty());
                        const ResolvedTask& first = phaseTasks.front();
                        if (kind == PathKind::SystemPath)
                        {
                            CHECK_FALSE(first.resolvedFromDriveTable);
                            CHECK(first.resolvedPath == "C:\\tools\\app.exe");
                        }
                        else
                        {
                            CHECK(first.resolvedFromDriveTable);
                            CHECK(first.searchedDrive.empty() == false);
                        }
                    }
                }
            }
        }
    }

    CHECK(combinations == kCombinationCount);
    CHECK(tasksSeen >= combinations);
}

TEST_CASE("wait strategy table matches the charter two-column mapping")
{
    using namespace osp::runplan;

    const WaitMapping noWait = waitMapping(WaitStrategy::NoWait, 0);
    CHECK(noWait.creationFlags == CREATE_NEW_PROCESS_GROUP);
    CHECK(std::string(noWait.creationFlagsName) == "CREATE_NEW_PROCESS_GROUP");
    CHECK(std::string(noWait.waitMode) == "none");

    const WaitMapping waitExit = waitMapping(WaitStrategy::WaitExit, 0);
    CHECK(waitExit.creationFlags == 0u);
    CHECK(std::string(waitExit.creationFlagsName) == "0");
    CHECK(std::string(waitExit.waitMode) == "infinite");

    const WaitMapping timeout = waitMapping(WaitStrategy::WaitTimeout, 12345);
    CHECK(timeout.creationFlags == 0u);
    CHECK(std::string(timeout.waitMode) == "timeout");
    CHECK(timeout.waitDescription.find("12345") != std::string::npos);
}

TEST_CASE("drive selection honours the volume label with a type fallback")
{
    using namespace osp::runplan;

    const DriveTable table = sampleDrives();

    CHECK(selectDrives(table, PathKind::UsbDrive).size() == 2);
    CHECK(selectDrives(table, PathKind::UsbDrive, "OSPT_USB").size() == 1);
    CHECK(std::string(selectDrives(table, PathKind::UsbDrive, "OSPT_USB").front()->root) == "E:\\");
    CHECK(std::string(selectDrives(table, PathKind::UsbDrive, "SPARE").front()->root) == "F:\\");
    // 卷标匹配不到时返回空，不退回按类型
    CHECK(selectDrives(table, PathKind::UsbDrive, "NOPE").empty());

    CHECK(selectDrives(table, PathKind::CdRom).size() == 1);
    CHECK(std::string(selectDrives(table, PathKind::CdRom).front()->root) == "G:\\");
    CHECK(selectDrives(table, PathKind::SystemPath).size() == 1);
    CHECK(std::string(selectDrives(table, PathKind::SystemPath).front()->root) == "C:\\");
    CHECK(selectDrives(table, PathKind::AllDrives).size() == 5);
}

TEST_CASE("path resolution replaces the drive and never uses the local machine")
{
    using namespace osp::runplan;

    SUBCASE("usb drive with label")
    {
        osp::config::RunPlanTask task = makeTask("postinstall", "run", "normal", "wait", "usbDrive");
        task.expectedVolumeLabel = "OSPT_USB";

        const PlanResult plan = buildOne(task);
        REQUIRE(plan.ok);
        CHECK(plan.phases[2].tasks.size() == 1);
        CHECK(plan.phases[2].tasks.front().resolvedPath == "E:\\tools\\app.exe");
    }

    SUBCASE("all drives expands to one command per drive")
    {
        const PlanResult plan = buildOne(makeTask("logon", "deleteDir", "hidden", "wait", "allDrives"));
        REQUIRE(plan.ok);
        CHECK(plan.phases[3].tasks.size() == 5);
        CHECK(plan.phases[3].tasks.front().resolvedPath == "C:\\tools\\app.exe");
        CHECK(plan.phases[3].tasks.back().resolvedPath == "G:\\tools\\app.exe");
    }

    SUBCASE("usb drive without any candidate is an error")
    {
        osp::config::RunPlanTask task = makeTask("logon", "run", "normal", "wait", "usbDrive");
        task.expectedVolumeLabel = "NO_SUCH_LABEL";
        const PlanResult plan = buildOne(task);
        CHECK_FALSE(plan.ok);
        CHECK(plan.error.find("找不到候选") != std::string::npos);
    }

    SUBCASE("system path requires an absolute path")
    {
        osp::config::RunPlanTask task = makeTask("logon", "run", "normal", "wait", "systemPath");
        task.target = "tools\\app.exe";
        const PlanResult plan = buildOne(task);
        CHECK_FALSE(plan.ok);
        CHECK(plan.error.find("绝对路径") != std::string::npos);
    }
}

TEST_CASE("cleanup appends a delete command after the task")
{
    using namespace osp::runplan;

    osp::config::RunPlanTask task = makeTask("desktop", "run", "normal", "noWait", "systemPath");
    task.cleanup = "deleteFile";

    const PlanResult plan = buildOne(task);
    REQUIRE(plan.ok);
    REQUIRE(plan.phases[4].tasks.size() == 2);

    const ResolvedTask& source = plan.phases[4].tasks[0];
    const ResolvedTask& cleanup = plan.phases[4].tasks[1];

    CHECK_FALSE(source.isCleanupCommand);
    CHECK(cleanup.isCleanupCommand);
    CHECK(cleanup.index == 2);
    CHECK(cleanup.type == TaskType::RemoveFile);
    CHECK(cleanup.commandLine == "cmd /c del /f /q \"C:\\tools\\app.exe\"");
    // 不等待的任务，其善后命令仍按等待完成执行，避免文件被占用
    CHECK(cleanup.waitStrategy == WaitStrategy::WaitExit);
    CHECK(cleanup.waitMode == "infinite");
}

TEST_CASE("invalid values are rejected with a reason")
{
    using namespace osp::runplan;

    SUBCASE("unknown phase")
    {
        const PlanResult plan = buildOne(makeTask("nope", "run", "normal", "wait", "systemPath"));
        CHECK_FALSE(plan.ok);
        CHECK(plan.error.find("未知阶段") != std::string::npos);
    }

    SUBCASE("unknown task type")
    {
        const PlanResult plan = buildOne(makeTask("logon", "nope", "normal", "wait", "systemPath"));
        CHECK_FALSE(plan.ok);
        CHECK(plan.error.find("未知任务类型") != std::string::npos);
    }

    SUBCASE("unknown window style")
    {
        const PlanResult plan = buildOne(makeTask("logon", "run", "nope", "wait", "systemPath"));
        CHECK_FALSE(plan.ok);
        CHECK(plan.error.find("未知运行方式") != std::string::npos);
    }

    SUBCASE("unknown wait strategy")
    {
        const PlanResult plan = buildOne(makeTask("logon", "run", "normal", "nope", "systemPath"));
        CHECK_FALSE(plan.ok);
        CHECK(plan.error.find("未知等待策略") != std::string::npos);
    }

    SUBCASE("unknown path kind")
    {
        const PlanResult plan = buildOne(makeTask("logon", "run", "normal", "wait", "nope"));
        CHECK_FALSE(plan.ok);
        CHECK(plan.error.find("未知路径类型") != std::string::npos);
    }

    SUBCASE("custom wait without seconds")
    {
        osp::config::RunPlanTask task = makeTask("logon", "run", "normal", "custom", "systemPath");
        task.waitSeconds = 0;
        const PlanResult plan = buildOne(task);
        CHECK_FALSE(plan.ok);
        CHECK(plan.error.find("waitSeconds") != std::string::npos);
    }

    SUBCASE("unknown timeout action")
    {
        osp::config::RunPlanTask task = makeTask("logon", "run", "normal", "custom", "systemPath");
        task.timeoutAction = "explode";
        const PlanResult plan = buildOne(task);
        CHECK_FALSE(plan.ok);
        CHECK(plan.error.find("timeoutAction") != std::string::npos);
    }

    SUBCASE("unknown cleanup")
    {
        osp::config::RunPlanTask task = makeTask("logon", "run", "normal", "wait", "systemPath");
        task.cleanup = "nope";
        const PlanResult plan = buildOne(task);
        CHECK_FALSE(plan.ok);
        CHECK(plan.error.find("善后处理") != std::string::npos);
    }
}

TEST_CASE("plan json carries the drive table and the two wait columns")
{
    using namespace osp::runplan;

    const PlanResult plan = buildOne(makeTask("logon", "run", "normal", "custom", "usbDrive"));
    REQUIRE(plan.ok);

    const nlohmann::json parsed = nlohmann::json::parse(toJson(plan), nullptr, false);
    REQUIRE_FALSE(parsed.is_discarded());

    CHECK(parsed["schemaVersion"].get<int>() == 1);

    const nlohmann::json& table = parsed["driveTable"];
    const std::string source = table["source"].get<std::string>();
    CHECK(source == "file");
    CHECK(table["drives"].size() == 5);

    const nlohmann::json& phases = parsed["phases"];
    CHECK(phases.size() == 5);
    const std::string firstPhase = phases[0]["phase"].get<std::string>();
    const std::string lastPhase = phases[4]["phase"].get<std::string>();
    CHECK(firstPhase == "preinstall");
    CHECK(lastPhase == "desktop");

    const nlohmann::json& task = phases[3]["tasks"][0];
    CHECK(task["creationFlags"].get<std::uint32_t>() == 0u);
    CHECK(task["waitMode"].get<std::string>() == "timeout");
    CHECK(task["timeoutMs"].get<int>() == 5000);
    CHECK(task["timeoutAction"].get<std::string>() == "continue");
    CHECK(task["showWindow"].get<int>() == SW_SHOWNORMAL);
}

TEST_CASE("phase scripts name every task and honour the no-wait flag")
{
    using namespace osp::runplan;

    osp::config::Config cfg;
    cfg.runPlan.push_back(makeTask("postinstall", "run", "normal", "noWait", "systemPath"));
    cfg.runPlan.push_back(makeTask("postinstall", "run", "hidden", "wait", "systemPath"));

    const PlanResult plan = build(cfg, sampleDrives());
    REQUIRE(plan.ok);

    const std::string script = phaseScript(plan, plan.phases[2]);
    CHECK(script.find("@echo off") == 0);
    CHECK(script.find("rem [1] run  wait=noWait") != std::string::npos);
    CHECK(script.find("start \"\" /b C:\\tools\\app.exe") != std::string::npos);
    CHECK(script.find("rem [2] run  wait=wait") != std::string::npos);

    // 空阶段也要有脚本，且写明没有任务
    const std::string empty = phaseScript(plan, plan.phases[0]);
    CHECK(empty.find("本阶段没有任务") != std::string::npos);
}

TEST_CASE("plan file can carry the drive table inline")
{
    using namespace osp::runplan;

    std::string error;
    DriveTable table;

    SUBCASE("nested under driveTable")
    {
        const std::string plan =
            R"({"schemaVersion":1,"driveTable":{"drives":[{"root":"E:\\","driveType":"removable"}]}})";
        REQUIRE(driveTableFromPlan(plan, table, error));
        CHECK(table.source == "file");
        CHECK(table.drives.size() == 1);
    }

    SUBCASE("top-level drives array")
    {
        const std::string plan = R"({"schemaVersion":1,"drives":[{"root":"G:\\","driveType":"cdrom"}]})";
        REQUIRE(driveTableFromPlan(plan, table, error));
        CHECK(table.drives.front().driveType == "cdrom");
    }

    SUBCASE("absent field is not an error")
    {
        const std::string plan = R"({"schemaVersion":1,"runPlan":[]})";
        CHECK_FALSE(driveTableFromPlan(plan, table, error));
        CHECK(error.empty());
    }

    SUBCASE("malformed embedded table reports a reason")
    {
        const std::string plan = R"({"drives":[{"driveType":"fixed"}]})";
        CHECK_FALSE(driveTableFromPlan(plan, table, error));
        CHECK(error.find("root") != std::string::npos);
    }
}

TEST_CASE("drive table parsing accepts an array or an object with drives")
{
    using namespace osp::runplan;

    std::string error;
    DriveTable table;

    REQUIRE(parseDriveTable(R"({"drives":[{"root":"X:\\","driveType":"fixed"}]})", table, error));
    CHECK(table.source == "file");
    CHECK(table.drives.size() == 1);
    CHECK(table.drives.front().root == "X:\\");

    REQUIRE(parseDriveTable(R"([{"root":"Y:\\","driveType":"cdrom","volumeLabel":"L"}])", table, error));
    CHECK(table.drives.front().volumeLabel == "L");

    CHECK_FALSE(parseDriveTable("not json", table, error));
    CHECK_FALSE(error.empty());

    CHECK_FALSE(parseDriveTable(R"({"drives":[{"driveType":"fixed"}]})", table, error));
    CHECK(error.find("root") != std::string::npos);

    CHECK_FALSE(parseDriveTable(R"({"drives":[{"root":"Z:\\"}]})", table, error));
    CHECK(error.find("driveType") != std::string::npos);
}

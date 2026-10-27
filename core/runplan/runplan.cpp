#include "core/runplan.hpp"

#include "core/win7.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/sysinfo.hpp"

namespace osp::runplan {
namespace {

// ---------------------------------------------------------------- 枚举表

const std::vector<Phase>& phaseTable()
{
    static const std::vector<Phase> kTable{
        Phase::PreInstall, Phase::Install, Phase::PostInstall, Phase::Logon, Phase::Desktop
    };
    return kTable;
}

const std::vector<TaskType>& taskTypeTable()
{
    static const std::vector<TaskType> kTable{
        TaskType::Run, TaskType::ImportReg, TaskType::RemoveFile, TaskType::RemoveDir
    };
    return kTable;
}

const std::vector<WindowStyle>& windowStyleTable()
{
    static const std::vector<WindowStyle> kTable{
        WindowStyle::Normal, WindowStyle::Hidden, WindowStyle::Minimized, WindowStyle::Maximized
    };
    return kTable;
}

const std::vector<WaitStrategy>& waitStrategyTable()
{
    static const std::vector<WaitStrategy> kTable{
        WaitStrategy::WaitExit, WaitStrategy::NoWait, WaitStrategy::WaitTimeout
    };
    return kTable;
}

const std::vector<PathKind>& pathKindTable()
{
    static const std::vector<PathKind> kTable{
        PathKind::SystemPath, PathKind::UsbDrive, PathKind::CdRom, PathKind::AllDrives
    };
    return kTable;
}

bool equalsIgnoreCase(const std::string& a, const char* b)
{
    if (a.size() != std::char_traits<char>::length(b))
    {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
        {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------- 路径小工具

bool isHexColor(const std::string& text)
{
    if (text.size() != 7 || text[0] != '#')
    {
        return false;
    }
    for (std::size_t i = 1; i < text.size(); ++i)
    {
        if (std::isxdigit(static_cast<unsigned char>(text[i])) == 0)
        {
            return false;
        }
    }
    return true;
}

bool hasDrivePrefix(const std::string& path)
{
    return path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) != 0 && path[1] == ':';
}

bool hasEnvPrefix(const std::string& path)
{
    return path.size() >= 3 && path[0] == '%' && path.find('%', 1) != std::string::npos;
}

bool isUncPath(const std::string& path)
{
    return path.size() >= 2 && path[0] == '\\' && path[1] == '\\';
}

// 去掉盘符与前导分隔符，得到相对部分
std::string stripDrive(const std::string& path)
{
    std::size_t start = 0;
    if (hasDrivePrefix(path))
    {
        start = 2;
    }
    while (start < path.size() && (path[start] == '\\' || path[start] == '/'))
    {
        ++start;
    }
    return path.substr(start);
}

// 把相对路径拼到盘符根上（root 形如 "E:\\"）
std::string joinRoot(const std::string& root, const std::string& relative)
{
    std::string base = root;
    if (base.empty() || (base.back() != '\\' && base.back() != '/'))
    {
        base.push_back('\\');
    }
    return base + relative;
}

std::string quote(const std::string& value)
{
    return "\"" + value + "\"";
}

// ---------------------------------------------------------------- 命令行

std::string buildCommandLine(TaskType type, const std::string& path, const std::string& arguments)
{
    switch (type)
    {
    case TaskType::Run:
        return arguments.empty() ? path : path + " " + arguments;
    case TaskType::ImportReg:
        return "reg.exe import " + quote(path);
    case TaskType::RemoveFile:
        return "cmd /c del /f /q " + quote(path);
    case TaskType::RemoveDir:
        return "cmd /c rd /s /q " + quote(path);
    }
    return path;
}

std::uint32_t timeoutMsOf(int waitSeconds)
{
    return static_cast<std::uint32_t>(waitSeconds) * 1000u;
}

} // namespace

// ---------------------------------------------------------------- 枚举访问

const std::vector<Phase>& allPhases()
{
    return phaseTable();
}

const std::vector<TaskType>& allTaskTypes()
{
    return taskTypeTable();
}

const std::vector<WindowStyle>& allWindowStyles()
{
    return windowStyleTable();
}

const std::vector<WaitStrategy>& allWaitStrategies()
{
    return waitStrategyTable();
}

const std::vector<PathKind>& allPathKinds()
{
    return pathKindTable();
}

const char* phaseId(Phase phase)
{
    switch (phase)
    {
    case Phase::PreInstall:
        return "preinstall";
    case Phase::Install:
        return "install";
    case Phase::PostInstall:
        return "postinstall";
    case Phase::Logon:
        return "logon";
    case Phase::Desktop:
        return "desktop";
    }
    return "unknown";
}

const char* phaseLabel(Phase phase)
{
    switch (phase)
    {
    case Phase::PreInstall:
        return "部署前";
    case Phase::Install:
        return "部署中";
    case Phase::PostInstall:
        return "部署后";
    case Phase::Logon:
        return "登录时";
    case Phase::Desktop:
        return "进桌面";
    }
    return "未知";
}

const char* taskTypeId(TaskType type)
{
    switch (type)
    {
    case TaskType::Run:
        return "run";
    case TaskType::ImportReg:
        return "reg";
    case TaskType::RemoveFile:
        return "deleteFile";
    case TaskType::RemoveDir:
        return "deleteDir";
    }
    return "unknown";
}

const char* windowStyleId(WindowStyle style)
{
    switch (style)
    {
    case WindowStyle::Normal:
        return "normal";
    case WindowStyle::Hidden:
        return "hidden";
    case WindowStyle::Minimized:
        return "minimized";
    case WindowStyle::Maximized:
        return "maximized";
    }
    return "unknown";
}

const char* waitStrategyId(WaitStrategy strategy)
{
    switch (strategy)
    {
    case WaitStrategy::WaitExit:
        return "wait";
    case WaitStrategy::NoWait:
        return "noWait";
    case WaitStrategy::WaitTimeout:
        return "custom";
    }
    return "unknown";
}

const char* pathKindId(PathKind kind)
{
    switch (kind)
    {
    case PathKind::SystemPath:
        return "systemPath";
    case PathKind::UsbDrive:
        return "usbDrive";
    case PathKind::CdRom:
        return "cdRom";
    case PathKind::AllDrives:
        return "allDrives";
    }
    return "unknown";
}

bool parsePhase(const std::string& text, Phase& out, std::string& error)
{
    for (Phase phase : phaseTable())
    {
        if (equalsIgnoreCase(text, phaseId(phase)))
        {
            out = phase;
            return true;
        }
    }
    error = "未知阶段: \"" + text + "\"（允许: preinstall/install/postinstall/logon/desktop）";
    return false;
}

// Phase 的枚举顺序与 phaseTable() 一致，可直接作为下标；这里集中一处以免散落强转
std::size_t phaseIndex(Phase phase)
{
    const std::vector<Phase>& table = phaseTable();
    for (std::size_t i = 0; i < table.size(); ++i)
    {
        if (table[i] == phase)
        {
            return i;
        }
    }
    return 0;
}

bool parseTaskType(const std::string& text, TaskType& out, std::string& error)
{
    for (TaskType type : taskTypeTable())
    {
        if (equalsIgnoreCase(text, taskTypeId(type)))
        {
            out = type;
            return true;
        }
    }
    error = "未知任务类型: \"" + text + "\"（允许: run/reg/deleteFile/deleteDir）";
    return false;
}

bool parseWindowStyle(const std::string& text, WindowStyle& out, std::string& error)
{
    for (WindowStyle style : windowStyleTable())
    {
        if (equalsIgnoreCase(text, windowStyleId(style)))
        {
            out = style;
            return true;
        }
    }
    error = "未知运行方式: \"" + text + "\"（允许: normal/hidden/minimized/maximized）";
    return false;
}

bool parseWaitStrategy(const std::string& text, WaitStrategy& out, std::string& error)
{
    for (WaitStrategy strategy : waitStrategyTable())
    {
        if (equalsIgnoreCase(text, waitStrategyId(strategy)))
        {
            out = strategy;
            return true;
        }
    }
    error = "未知等待策略: \"" + text + "\"（允许: wait/noWait/custom）";
    return false;
}

bool parsePathKind(const std::string& text, PathKind& out, std::string& error)
{
    for (PathKind kind : pathKindTable())
    {
        if (equalsIgnoreCase(text, pathKindId(kind)))
        {
            out = kind;
            return true;
        }
    }
    error = "未知路径类型: \"" + text + "\"（允许: systemPath/usbDrive/cdRom/allDrives）";
    return false;
}

// ---------------------------------------------------------------- 映射表

WaitMapping waitMapping(WaitStrategy strategy, int timeoutMs)
{
    WaitMapping mapping{};
    switch (strategy)
    {
    case WaitStrategy::NoWait:
        // 宪章 §8 M4：不等待 -> CREATE_NEW_PROCESS_GROUP，便于后续可选的整组终止
        mapping.creationFlags = CREATE_NEW_PROCESS_GROUP;
        mapping.creationFlagsName = "CREATE_NEW_PROCESS_GROUP";
        mapping.waitMode = "none";
        mapping.waitDescription = "立即 CloseHandle(hProcess)，不调用 WaitForSingleObject";
        break;
    case WaitStrategy::WaitExit:
        mapping.creationFlags = 0;
        mapping.creationFlagsName = "0";
        mapping.waitMode = "infinite";
        mapping.waitDescription = "WaitForSingleObject(hProcess, INFINITE) 后 GetExitCodeProcess";
        break;
    case WaitStrategy::WaitTimeout:
        mapping.creationFlags = 0;
        mapping.creationFlagsName = "0";
        mapping.waitMode = "timeout";
        mapping.waitDescription = "WaitForSingleObject(hProcess, " + std::to_string(timeoutMs) +
            ")，超时后按 timeoutAction 决定终止或继续，两种结果都写日志";
        break;
    }
    return mapping;
}

std::uint16_t showWindowValue(WindowStyle style)
{
    switch (style)
    {
    case WindowStyle::Normal:
        return SW_SHOWNORMAL;
    case WindowStyle::Hidden:
        return SW_HIDE;
    case WindowStyle::Minimized:
        return SW_SHOWMINIMIZED;
    case WindowStyle::Maximized:
        return SW_SHOWMAXIMIZED;
    }
    return SW_SHOWNORMAL;
}

const char* showWindowName(WindowStyle style)
{
    switch (style)
    {
    case WindowStyle::Normal:
        return "SW_SHOWNORMAL";
    case WindowStyle::Hidden:
        return "SW_HIDE";
    case WindowStyle::Minimized:
        return "SW_SHOWMINIMIZED";
    case WindowStyle::Maximized:
        return "SW_SHOWMAXIMIZED";
    }
    return "SW_SHOWNORMAL";
}

// ---------------------------------------------------------------- 驱动器表

DriveTable enumerateLocalDrives()
{
    DriveTable table;
    table.source = "local";

    const osp::sysinfo::CheckupReport report = osp::sysinfo::collect();
    for (const osp::sysinfo::PartitionInfo& part : report.partitions)
    {
        // "C:" -> "C:\"
        if (part.drive.size() < 2)
        {
            continue;
        }
        DriveEntry entry;
        entry.root = part.drive + "\\";
        entry.volumeLabel = part.volumeLabel;
        entry.driveType = part.driveType;
        entry.systemDrive = part.systemDrive;
        table.drives.push_back(std::move(entry));
    }
    return table;
}

bool parseDriveTable(const std::string& json, DriveTable& out, std::string& error)
{
    nlohmann::json parsed;
    try
    {
        parsed = nlohmann::json::parse(json);
    }
    catch (const nlohmann::json::exception& e)
    {
        error = std::string("驱动器表 JSON 解析失败: ") + e.what();
        return false;
    }

    const nlohmann::json* list = nullptr;
    if (parsed.is_array())
    {
        list = &parsed;
    }
    else if (parsed.is_object() && parsed.contains("drives"))
    {
        list = &parsed["drives"];
    }

    if (list == nullptr || !list->is_array())
    {
        error = "驱动器表必须是数组，或带 drives 数组的对象";
        return false;
    }

    DriveTable table;
    table.source = "file";
    for (const auto& item : *list)
    {
        if (!item.is_object())
        {
            error = "驱动器表元素必须是对象";
            return false;
        }
        DriveEntry entry;
        entry.root = item.value("root", std::string{});
        entry.volumeLabel = item.value("volumeLabel", std::string{});
        entry.driveType = item.value("driveType", std::string{});
        entry.systemDrive = item.value("systemDrive", false);

        if (entry.root.empty())
        {
            error = "驱动器表元素缺少 root";
            return false;
        }
        if (entry.driveType.empty())
        {
            error = "驱动器表元素缺少 driveType: " + entry.root;
            return false;
        }
        table.drives.push_back(std::move(entry));
    }

    out = std::move(table);
    return true;
}

namespace {

bool matchesPathKind(const DriveEntry& entry, PathKind kind)
{
    switch (kind)
    {
    case PathKind::SystemPath:
        return entry.systemDrive;
    case PathKind::UsbDrive:
        return entry.driveType == "removable";
    case PathKind::CdRom:
        return entry.driveType == "cdrom";
    case PathKind::AllDrives:
        return true;
    }
    return false;
}

} // namespace

std::vector<const DriveEntry*> selectDrives(const DriveTable& table, PathKind kind)
{
    return selectDrives(table, kind, std::string{});
}

bool driveTableFromPlan(const std::string& planJson, DriveTable& out, std::string& error)
{
    nlohmann::json parsed;
    try
    {
        parsed = nlohmann::json::parse(planJson);
    }
    catch (const nlohmann::json::exception& e)
    {
        error = std::string("计划 JSON 解析失败: ") + e.what();
        return false;
    }

    if (!parsed.is_object())
    {
        return false;
    }

    nlohmann::json holder;
    if (parsed.contains("driveTable"))
    {
        holder = parsed["driveTable"];
    }
    else if (parsed.contains("drives"))
    {
        holder = parsed["drives"];
    }
    else
    {
        // 计划文件本来就不带驱动器表，不算错误
        return false;
    }

    return parseDriveTable(holder.dump(), out, error);
}

std::vector<const DriveEntry*>
selectDrives(const DriveTable& table, PathKind kind, const std::string& expectedVolumeLabel)
{
    std::vector<const DriveEntry*> selected;
    for (const DriveEntry& entry : table.drives)
    {
        if (!matchesPathKind(entry, kind))
        {
            continue;
        }
        // 卷标非空时按卷标匹配；卷标为空则退回按驱动器类型（见宪章 §8 M4 的路径解析口径）
        if (!expectedVolumeLabel.empty() && entry.volumeLabel != expectedVolumeLabel)
        {
            continue;
        }
        selected.push_back(&entry);
    }
    return selected;
}

// ---------------------------------------------------------------- 构建计划

PlanResult build(const osp::config::Config& cfg, const DriveTable& drives)
{
    PlanResult plan;
    plan.driveTable = drives;

    // 呈现设置：模块与背景取值来自《功能规格》§四，非法值直接拒绝而不是静默退回默认
    const auto& deploy = cfg.deploy;
    plan.presentation.module = deploy.module.empty() ? "grid" : deploy.module;
    if (plan.presentation.module != "grid" && plan.presentation.module != "trueTrans" &&
        plan.presentation.module != "colorBar" && plan.presentation.module != "image")
    {
        plan.error = "未知部署模块: \"" + deploy.module + "\"（允许: grid/trueTrans/colorBar/image）";
        return plan;
    }
    plan.presentation.background = deploy.background.empty() ? "systemDefault" : deploy.background;
    if (plan.presentation.background != "single" && plan.presentation.background != "multi" &&
        plan.presentation.background != "systemDefault")
    {
        plan.error = "未知部署背景: \"" + deploy.background + "\"（允许: single/multi/systemDefault）";
        return plan;
    }
    plan.presentation.titleText = deploy.titleText;
    // 部署图片：SC 的 SA_Lab_oemDeploy「遇品牌电脑则改变相应品牌部署图片」。
    // 显式给的 deploy.backgroundImage 优先；没给才回落到 OEM 那张，
    // 这样「品牌识别出来的图」不会悄悄覆盖用户自己指定的背景。
    plan.presentation.backgroundImage =
        deploy.backgroundImage.empty() ? cfg.oem.deployImage : deploy.backgroundImage;
    plan.presentation.imageSwitchSeconds = deploy.imageSwitchSeconds > 0 ? deploy.imageSwitchSeconds : 10;
    plan.presentation.resolutionX = deploy.deployResolutionX;
    plan.presentation.resolutionY = deploy.deployResolutionY;
    plan.presentation.fontColor = deploy.fontColor;

    plan.checks.verifyIntegrity = deploy.verifyIntegrity;
    plan.checks.blockVirtualMachine = deploy.blockVirtualMachine;
    plan.checks.hijackMode = deploy.hijackMode.empty() ? "terminate" : deploy.hijackMode;
    plan.checks.hijackTip = deploy.hijackTip;
    plan.checks.tamperFiles = deploy.tamperFiles;
    // SC 的防篡改是「锁定文件列表 + MD5」，所以两者必须成对出现：
    // 开了校验却没有清单，等于什么都没校验。
    if (plan.checks.verifyIntegrity && plan.checks.tamperFiles.empty())
    {
        plan.error = "deploy.verifyIntegrity 已开启，但 deploy.tamperFiles 为空 —— 没有可校验的文件";
        return plan;
    }
    for (const auto& entry : plan.checks.tamperFiles)
    {
        if (entry.md5.size() != 32)
        {
            plan.error = "防篡改条目的 MD5 必须是 32 位十六进制：" + entry.path;
            return plan;
        }
    }
    if (plan.checks.hijackMode != "continue" && plan.checks.hijackMode != "terminate")
    {
        plan.error =
            "deploy.hijackMode 只接受 continue / terminate，当前为 \"" + plan.checks.hijackMode + "\"";
        return plan;
    }
    if (!plan.presentation.fontColor.empty() && !isHexColor(plan.presentation.fontColor))
    {
        plan.error = "部署文字颜色必须是 #RRGGBB，当前为 \"" + deploy.fontColor + "\"";
        return plan;
    }
    if ((plan.presentation.resolutionX > 0) != (plan.presentation.resolutionY > 0))
    {
        plan.error = "部署分辨率必须同时给出宽和高，当前为 " + std::to_string(plan.presentation.resolutionX) +
            "x" + std::to_string(plan.presentation.resolutionY);
        return plan;
    }
    if (plan.presentation.background != "systemDefault" && plan.presentation.backgroundImage.empty())
    {
        plan.warnings.push_back(
            "部署背景选了 " + plan.presentation.background + "，但没有给图片路径；运行时按系统默认处理"
        );
    }

    for (Phase phase : phaseTable())
    {
        PhasePlan phasePlan;
        phasePlan.phase = phase;
        phasePlan.scriptName = std::string(phaseId(phase)) + ".cmd";
        plan.phases.push_back(std::move(phasePlan));
    }

    for (const osp::config::RunPlanTask& task : cfg.runPlan)
    {
        Phase phase{};
        TaskType type{};
        WindowStyle windowStyle{};
        WaitStrategy waitStrategy{};
        PathKind pathKind{};

        if (!parsePhase(task.phase, phase, plan.error) || !parseTaskType(task.type, type, plan.error) ||
            !parseWindowStyle(task.windowStyle, windowStyle, plan.error) ||
            !parseWaitStrategy(task.waitMode, waitStrategy, plan.error) ||
            !parsePathKind(task.searchScope, pathKind, plan.error))
        {
            return plan;
        }

        std::string timeoutAction = task.timeoutAction.empty() ? "continue" : task.timeoutAction;
        if (timeoutAction != "kill" && timeoutAction != "continue")
        {
            plan.error = "未知 timeoutAction: \"" + task.timeoutAction + "\"（允许: kill/continue）";
            return plan;
        }

        int timeoutMs = 0;
        if (waitStrategy == WaitStrategy::WaitTimeout)
        {
            if (task.waitSeconds <= 0)
            {
                plan.error = "等待策略 custom 需要 waitSeconds > 0，当前为 " +
                    std::to_string(task.waitSeconds) + "（target=" + task.target + "）";
                return plan;
            }
            timeoutMs = static_cast<int>(timeoutMsOf(task.waitSeconds));
        }

        if (task.cleanup.empty() || (task.cleanup != "keep" && task.cleanup != "deleteFile"))
        {
            plan.error = "未知善后处理: \"" + task.cleanup + "\"（允许: keep/deleteFile）";
            return plan;
        }
        const bool cleanupAfter = task.cleanup == "deleteFile";

        // ---- 路径解析 ----
        std::vector<std::pair<std::string, std::string>> resolved; // <路径, 命中盘符>
        if (pathKind == PathKind::SystemPath)
        {
            if (!hasDrivePrefix(task.target) && !hasEnvPrefix(task.target) && !isUncPath(task.target))
            {
                plan.error =
                    "路径类型 systemPath 需要绝对路径（或 %VAR% 形式），当前为 \"" + task.target + "\"";
                return plan;
            }
            resolved.emplace_back(task.target, std::string{});
        }
        else
        {
            const std::string relative = stripDrive(task.target);
            if (relative.empty())
            {
                plan.error = "路径类型 " + std::string(pathKindId(pathKind)) +
                    " 需要相对路径（盘符会被替换），当前为 \"" + task.target + "\"";
                return plan;
            }
            const std::vector<const DriveEntry*> candidates =
                selectDrives(drives, pathKind, task.expectedVolumeLabel);
            if (candidates.empty())
            {
                plan.error = std::string("路径类型 ") + pathKindId(pathKind) + " 在驱动器表里找不到候选" +
                    (task.expectedVolumeLabel.empty() ? std::string{}
                                                      : "（卷标要求: " + task.expectedVolumeLabel + "）") +
                    "，target=" + task.target;
                return plan;
            }
            for (const DriveEntry* entry : candidates)
            {
                resolved.emplace_back(joinRoot(entry->root, relative), entry->root);
            }
            if (pathKind == PathKind::AllDrives && resolved.size() > 1)
            {
                plan.warnings.push_back(
                    "路径类型 allDrives 展开为 " + std::to_string(resolved.size()) +
                    " 条命令，target=" + task.target
                );
            }
        }

        PhasePlan& target = plan.phases[phaseIndex(phase)];

        for (const auto& [path, drive] : resolved)
        {
            ResolvedTask item;
            item.index = static_cast<int>(target.tasks.size()) + 1;
            item.type = type;
            item.sourceTarget = task.target;
            item.resolvedPath = path;
            item.resolvedFromDriveTable = pathKind != PathKind::SystemPath;
            item.searchedDrive = drive;
            item.commandLine = buildCommandLine(type, path, task.arguments);
            item.windowStyle = windowStyle;
            item.showWindow = showWindowValue(windowStyle);

            const WaitMapping mapping = waitMapping(waitStrategy, timeoutMs);
            item.waitStrategy = waitStrategy;
            item.creationFlags = mapping.creationFlags;
            item.creationFlagsName = mapping.creationFlagsName;
            item.waitMode = mapping.waitMode;
            item.timeoutMs = timeoutMs;
            item.timeoutAction = timeoutAction;
            target.tasks.push_back(std::move(item));

            if (cleanupAfter)
            {
                ResolvedTask cleanup;
                cleanup.index = static_cast<int>(target.tasks.size()) + 1;
                cleanup.type = TaskType::RemoveFile;
                cleanup.sourceTarget = task.target;
                cleanup.resolvedPath = path;
                cleanup.resolvedFromDriveTable = pathKind != PathKind::SystemPath;
                cleanup.searchedDrive = drive;
                cleanup.commandLine = buildCommandLine(TaskType::RemoveFile, path, std::string{});
                cleanup.isCleanupCommand = true;
                // 善后命令是实现细节，固定按隐藏 + 等待完成执行
                cleanup.windowStyle = WindowStyle::Hidden;
                cleanup.showWindow = showWindowValue(WindowStyle::Hidden);
                cleanup.waitStrategy = WaitStrategy::WaitExit;
                const WaitMapping cleanupMapping = waitMapping(WaitStrategy::WaitExit, 0);
                cleanup.creationFlags = cleanupMapping.creationFlags;
                cleanup.creationFlagsName = cleanupMapping.creationFlagsName;
                cleanup.waitMode = cleanupMapping.waitMode;
                cleanup.timeoutMs = 0;
                cleanup.timeoutAction = "continue";
                target.tasks.push_back(std::move(cleanup));
            }
        }
    }

    plan.ok = true;
    return plan;
}

// ---------------------------------------------------------------- JSON

std::string toJson(const PlanResult& plan)
{
    using nlohmann::json;

    json root;
    root["schemaVersion"] = 1;

    json& table = root["driveTable"];
    table["source"] = plan.driveTable.source;
    json drives = json::array();
    for (const DriveEntry& entry : plan.driveTable.drives)
    {
        json item;
        item["root"] = entry.root;
        item["volumeLabel"] = entry.volumeLabel;
        item["driveType"] = entry.driveType;
        item["systemDrive"] = entry.systemDrive;
        drives.push_back(std::move(item));
    }
    table["drives"] = std::move(drives);

    json& presentation = root["presentation"];
    presentation["module"] = plan.presentation.module;
    presentation["background"] = plan.presentation.background;
    presentation["titleText"] = plan.presentation.titleText;
    presentation["backgroundImage"] = plan.presentation.backgroundImage;
    presentation["imageSwitchSeconds"] = plan.presentation.imageSwitchSeconds;
    presentation["resolutionX"] = plan.presentation.resolutionX;
    presentation["resolutionY"] = plan.presentation.resolutionY;
    presentation["fontColor"] = plan.presentation.fontColor;

    // 部署校验（SC 的 Cb_Tampering / Btn_DisableVmrun / SA_Rb_hijack_*）
    json& checks = root["deployChecks"];
    checks["verifyIntegrity"] = plan.checks.verifyIntegrity;
    checks["blockVirtualMachine"] = plan.checks.blockVirtualMachine;
    checks["hijackMode"] = plan.checks.hijackMode;
    checks["hijackTip"] = plan.checks.hijackTip;
    json tamper = json::array();
    for (const auto& entry : plan.checks.tamperFiles)
    {
        json item;
        item["path"] = entry.path;
        item["md5"] = entry.md5;
        if (!entry.tip.empty())
        {
            item["tip"] = entry.tip;
        }
        tamper.push_back(std::move(item));
    }
    checks["tamperFiles"] = std::move(tamper);
    // 部署主控的摘要：由 --gen-runplan 时把 osptd.exe 的 MD5 记进来，
    // 部署端拿自身摘要比对，判断「部署主程序是否被替换」。
    checks["controllerMd5"] = plan.checks.controllerMd5;

    json phases = json::array();
    int total = 0;
    for (const PhasePlan& phase : plan.phases)
    {
        json item;
        item["phase"] = phaseId(phase.phase);
        item["label"] = phaseLabel(phase.phase);
        item["script"] = phase.scriptName;
        item["taskCount"] = phase.tasks.size();
        total += static_cast<int>(phase.tasks.size());

        json tasks = json::array();
        for (const ResolvedTask& task : phase.tasks)
        {
            json entry;
            entry["index"] = task.index;
            entry["type"] = taskTypeId(task.type);
            entry["sourceTarget"] = task.sourceTarget;
            entry["resolvedPath"] = task.resolvedPath;
            entry["searchedDrive"] = task.searchedDrive;
            entry["resolvedFromDriveTable"] = task.resolvedFromDriveTable;
            entry["isCleanupCommand"] = task.isCleanupCommand;
            entry["commandLine"] = task.commandLine;
            entry["windowStyle"] = windowStyleId(task.windowStyle);
            entry["showWindow"] = task.showWindow;
            entry["showWindowName"] = showWindowName(task.windowStyle);
            entry["waitStrategy"] = waitStrategyId(task.waitStrategy);
            entry["creationFlags"] = task.creationFlags;
            entry["creationFlagsName"] = task.creationFlagsName;
            entry["waitMode"] = task.waitMode;
            entry["timeoutMs"] = task.timeoutMs;
            entry["timeoutAction"] = task.timeoutAction;
            tasks.push_back(std::move(entry));
        }
        item["tasks"] = std::move(tasks);
        phases.push_back(std::move(item));
    }
    root["phases"] = std::move(phases);
    root["taskCount"] = total;
    root["warnings"] = plan.warnings;

    return root.dump(2) + "\n";
}

// ---------------------------------------------------------------- 阶段脚本

std::string phaseScript(const PlanResult& plan, const PhasePlan& phase)
{
    (void)plan;

    std::string text;
    text += "@echo off\r\n";
    text += "rem ============================================================\r\n";
    text += std::string("rem OpenSysprepTool 运行计划 —— ") + phaseId(phase.phase) + "（" +
        phaseLabel(phase.phase) + "）\r\n";
    text += "rem 由 ospt --gen-runplan 生成，请勿手工编辑\r\n";
    text += "rem 说明：不等待用 start /b 起进程；自定义超时的毫秒数写在注释里，\r\n";
    text += "rem       精确的等待与超时处理由 osptd.exe 按 runplan.json 执行。\r\n";
    text += "rem ============================================================\r\n";
    text += "\r\n";

    if (phase.tasks.empty())
    {
        text += "rem 本阶段没有任务\r\n";
        return text;
    }

    for (const ResolvedTask& task : phase.tasks)
    {
        text += std::string("rem [") + std::to_string(task.index) + "] " + taskTypeId(task.type) +
            "  wait=" + waitStrategyId(task.waitStrategy) + "  flags=" + task.creationFlagsName +
            (task.isCleanupCommand ? "  善后：删除源文件" : "") + "\r\n";
        if (task.waitStrategy == WaitStrategy::NoWait)
        {
            text += "start \"\" /b " + task.commandLine + "\r\n";
        }
        else
        {
            text += task.commandLine + "\r\n";
            if (task.waitStrategy == WaitStrategy::WaitTimeout)
            {
                text += std::string("rem 超时 ") + std::to_string(task.timeoutMs) + " ms，超时后 " +
                    task.timeoutAction + "\r\n";
            }
        }
        text += "\r\n";
    }

    return text;
}

namespace {

// 写 UTF-8 文本文件（core 里原本没有这个工具函数，host/main.cpp 有一份局部实现）。
// 落盘失败必须把原因带出去：运行计划写不全，部署端会当作「任务丢了」。
bool writeUtf8File(const std::string& utf8Path, const std::string& text, std::string& error)
{
    std::ofstream out(osp::win7::pathFromUtf8(utf8Path), std::ios::binary | std::ios::trunc);
    if (!out)
    {
        error = "打不开文件写入：" + utf8Path;
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out)
    {
        error = "写入失败：" + utf8Path;
        return false;
    }
    return true;
}

} // namespace

bool writePlanDirectory(const PlanResult& plan, const std::string& utf8Dir, std::string& error)
{
    if (!plan.ok)
    {
        error = plan.error.empty() ? std::string("计划无效，无法落盘") : plan.error;
        return false;
    }
    const std::filesystem::path directory = osp::win7::pathFromUtf8(utf8Dir);
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec)
    {
        error = "建目录失败：" + utf8Dir + "（" + ec.message() + "）";
        return false;
    }

    std::string writeError;
    if (!writeUtf8File(osp::win7::toUtf8Path(directory / "runplan.json"), toJson(plan), writeError))
    {
        error = writeError;
        return false;
    }
    for (const PhasePlan& phase : plan.phases)
    {
        if (!writeUtf8File(
                osp::win7::toUtf8Path(directory / phase.scriptName), phaseScript(plan, phase), writeError
            ))
        {
            error = writeError;
            return false;
        }
    }
    return true;
}

} // namespace osp::runplan

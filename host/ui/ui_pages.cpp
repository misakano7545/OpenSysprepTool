// 配置页的数据层（宪章 §8 M7b）
//
// 只有纯数据：不 include windows.h / ATL / WTL，所以单测能直接覆盖。
// 每个字段都带 binding（落到 core/config 的哪个字段）与 note（取值依据），
// 走查时按 DFM 控件名核对，取值按 note 追到图纸或核心模型。

#include "ui.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/runplan.hpp"
#include "core/sysinfo.hpp"

namespace osp::ui {
namespace {

// ---------------------------------------------------------------- 小工具

Field makeInfo(const std::string& id, const std::string& label, const std::string& value)
{
    Field field;
    field.id = id;
    field.label = label;
    field.kind = FieldKind::Info;
    field.value = value;
    return field;
}

Field makeCheck(
    const std::string& id,
    const std::string& label,
    bool checked,
    const std::string& binding,
    const std::string& note
)
{
    Field field;
    field.id = id;
    field.label = label;
    field.kind = FieldKind::Check;
    field.checked = checked;
    field.binding = binding;
    field.note = note;
    return field;
}

Field makeEdit(
    const std::string& id,
    const std::string& label,
    const std::string& value,
    const std::string& binding,
    const std::string& note
)
{
    Field field;
    field.id = id;
    field.label = label;
    field.kind = FieldKind::Edit;
    field.value = value;
    field.binding = binding;
    field.note = note;
    return field;
}

Field makeNumber(
    const std::string& id,
    const std::string& label,
    int value,
    const std::string& binding,
    const std::string& note
)
{
    Field field = makeEdit(id, label, std::to_string(value), binding, note);
    field.kind = FieldKind::Number;
    return field;
}

Field makeCombo(
    const std::string& id,
    const std::string& label,
    const std::vector<std::string>& options,
    const std::vector<std::string>& optionIds,
    int selected,
    const std::string& binding,
    const std::string& note
)
{
    Field field;
    field.id = id;
    field.label = label;
    field.kind = FieldKind::Combo;
    field.options = options;
    field.optionIds = optionIds;
    field.comboIndex = selected;
    field.binding = binding;
    field.note = note;
    if (selected >= 0 && selected < static_cast<int>(field.options.size()))
    {
        field.value = field.options[static_cast<std::size_t>(selected)];
    }
    return field;
}

Field makeRadio(
    const std::string& id,
    const std::string& label,
    bool checked,
    const std::string& binding,
    const std::string& note
)
{
    Field field = makeCheck(id, label, checked, binding, note);
    field.kind = FieldKind::Radio;
    return field;
}

Field makeButton(
    const std::string& id, const std::string& label, const std::string& action, const std::string& note
)
{
    Field field;
    field.id = id;
    field.label = label;
    field.kind = FieldKind::Button;
    field.action = action;
    field.note = note;
    return field;
}

Field makeProgress(const std::string& id, const std::string& label, int percent)
{
    Field field;
    field.id = id;
    field.label = label;
    field.kind = FieldKind::Progress;
    field.progress = percent;
    return field;
}

Field makeList(
    const std::string& id,
    const std::string& label,
    const std::vector<std::string>& columns,
    std::vector<std::vector<std::string>> rows,
    const std::string& note
)
{
    Field field;
    field.id = id;
    field.label = label;
    field.kind = FieldKind::List;
    field.columns = columns;
    field.rows = std::move(rows);
    field.note = note;
    return field;
}

int toInt(const std::string& text, int fallback)
{
    if (text.empty())
    {
        return fallback;
    }
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str())
    {
        return fallback;
    }
    return static_cast<int>(value);
}

std::string toLowerAscii(const std::string& text)
{
    std::string out = text;
    for (char& ch : out)
    {
        if (ch >= 'A' && ch <= 'Z')
        {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}

bool containsFold(const std::string& haystack, const std::string& needle)
{
    if (needle.empty())
    {
        return false;
    }
    return toLowerAscii(haystack).find(toLowerAscii(needle)) != std::string::npos;
}

// 「系统名称」：Windows 11 的 ProductName 仍报 "Windows 10 ..."，而 EditionID 往往已经
// 含在里面了（ProductName="Windows 10 Enterprise" + EditionID="Enterprise"），
// 直接拼接会显示成「Windows 10 Enterprise Enterprise」，所以先判包含再决定要不要补。
std::string composeOsName(const std::string& productName, const std::string& editionId)
{
    if (productName.empty())
    {
        return "未知";
    }
    if (editionId.empty() || containsFold(productName, editionId))
    {
        return productName;
    }
    return productName + " " + editionId;
}

// "1024x768" 这类组合串拆成宽高
void splitResolution(const std::string& text, int& x, int& y)
{
    const std::size_t pos = text.find('x');
    if (pos == std::string::npos)
    {
        return;
    }
    x = toInt(text.substr(0, pos), x);
    y = toInt(text.substr(pos + 1), y);
}

std::string joinResolution(int x, int y)
{
    char buffer[48] = {};
    std::snprintf(buffer, sizeof(buffer), "%dx%d", x, y);
    return std::string(buffer);
}

// DFM 里 Cb_Resolution 的 Items.Strings（图纸原文，11 条）
const std::vector<std::string>& dfmResolutions()
{
    static const std::vector<std::string> values = {
        "800x600",
        "1024x768",
        "1280x800",
        "1280x1024",
        "1360x768",
        "1366x768",
        "1440x900",
        "1600x900",
        "1680x1050",
        "1920x1080",
        "1920x1200",
    };
    return values;
}

// 取一组枚举的 id 与中文标签
void appendPhaseOptions(std::vector<std::string>& labels, std::vector<std::string>& ids)
{
    for (const runplan::Phase phase : runplan::allPhases())
    {
        ids.emplace_back(runplan::phaseId(phase));
        labels.emplace_back(runplan::phaseLabel(phase));
    }
}

int indexOf(const std::vector<std::string>& ids, const std::string& value, int fallback)
{
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        if (ids[i] == value)
        {
            return static_cast<int>(i);
        }
    }
    return fallback;
}

std::string comboValue(const Field& field, const std::string& fallback)
{
    if (field.comboIndex >= 0 && field.comboIndex < static_cast<int>(field.optionIds.size()))
    {
        return field.optionIds[static_cast<std::size_t>(field.comboIndex)];
    }
    return fallback;
}

std::string toUtf8(const std::wstring& text)
{
    if (text.empty())
    {
        return std::string();
    }
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr
    );
    if (size <= 0)
    {
        return std::string();
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr
    );
    return out;
}

// DFM 的 Lab_OsUserv 要显示当前用户
std::string currentUser()
{
    wchar_t buffer[256] = {};
    DWORD length = static_cast<DWORD>(std::size(buffer));
    if (GetUserNameW(buffer, &length) == 0)
    {
        return "读取失败";
    }
    return toUtf8(std::wstring(buffer, length > 0 ? length - 1 : 0));
}

// DFM 的 Lab_RunEnvironv 要显示运行环境（WinPE / 正常系统）
std::string runEnvironment()
{
    HKEY key = nullptr;
    const LSTATUS status = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\MiniNT", 0, KEY_READ | KEY_WOW64_64KEY, &key
    );
    if (status == ERROR_SUCCESS)
    {
        RegCloseKey(key);
        return "WinPE";
    }

    BOOL wow64 = FALSE;
    const bool isWow64 = (IsWow64Process(GetCurrentProcess(), &wow64) != 0) && (wow64 != FALSE);
    return isWow64 ? "正常系统（32 位进程 / WOW64）" : "正常系统";
}

} // namespace

const char* fieldKindId(FieldKind kind)
{
    switch (kind)
    {
    case FieldKind::Info:
        return "info";
    case FieldKind::Check:
        return "check";
    case FieldKind::Edit:
        return "edit";
    case FieldKind::Number:
        return "number";
    case FieldKind::Combo:
        return "combo";
    case FieldKind::Radio:
        return "radio";
    case FieldKind::Button:
        return "button";
    case FieldKind::Progress:
        return "progress";
    case FieldKind::List:
        return "list";
    }
    return "unknown";
}

// ---------------------------------------------------------------- 母盘系统

Page systemPage()
{
    const sysinfo::CheckupReport report = sysinfo::collect();

    Page page;
    page.id = "system";
    page.title = "母盘系统"; // DFM: MlSkinTabSheet1.Caption
    page.dfmSheet = "MlSkinTabSheet1";

    // DFM: Gb_Osbasicinfo『系统信息』，9 个值标签与 M2 的体检报告字段一一对应
    Group basic;
    basic.id = "Gb_Osbasicinfo";
    basic.caption = "系统信息";

    const sysinfo::PartitionInfo* system = nullptr;
    for (const sysinfo::PartitionInfo& part : report.partitions)
    {
        if (part.systemDrive)
        {
            system = &part;
            break;
        }
    }

    std::string osName = composeOsName(report.os.productName, report.os.editionId);

    std::string arch = report.os.architecture.empty() ? std::string("unknown") : report.os.architecture;
    arch += report.os.is64BitOs ? "（64 位系统）" : "（32 位系统）";

    std::string sysprepText;
    if (!report.sysprep.available)
    {
        sysprepText = "不支持（未找到 sysprep.exe）";
    }
    else if (report.sysprep.sysprepPending)
    {
        sysprepText = "不支持（系统已封装，处于待 OOBE/待部署状态）";
    }
    else
    {
        sysprepText = "支持";
    }

    char sizeBuffer[96] = {};
    std::string usedText = "未识别";
    std::string driveText = "未识别";
    if (system != nullptr)
    {
        std::snprintf(
            sizeBuffer,
            sizeof(sizeBuffer),
            "%.1f GB / %.1f GB",
            static_cast<double>(system->usedBytes) / (1024.0 * 1024.0 * 1024.0),
            static_cast<double>(system->totalBytes) / (1024.0 * 1024.0 * 1024.0)
        );
        usedText = sizeBuffer;
        driveText = system->drive + "  " + system->fileSystem + "  " +
            (system->volumeLabel.empty() ? std::string("(无卷标)") : system->volumeLabel);
    }

    char percentBuffer[32] = {};
    std::snprintf(
        percentBuffer, sizeof(percentBuffer), "%.1f%%", system == nullptr ? 0.0 : system->usedPercent
    );

    // 运行环境：WinPE 会建 HKLM\SYSTEM\CurrentControlSet\Control\MiniNT
    basic.fields.push_back(makeInfo("Lab_Osnamev", "系统名称", osName));
    basic.fields.push_back(makeInfo("Lab_Osbitv", "系统位宽", arch));
    basic.fields.push_back(makeInfo(
        "Lab_Osntverv", "内核版本", report.os.version.empty() ? std::string("未知") : report.os.version
    ));
    basic.fields.push_back(makeInfo("Lab_SysprepSupportv", "封装支持判定", sysprepText));
    basic.fields.push_back(makeInfo("Lab_Osdrivesv", "系统分区", driveText));
    basic.fields.push_back(makeInfo("Lab_Osusesizev", "使用容量", usedText));
    basic.fields.push_back(makeInfo("Lab_OsUserv", "当前用户", currentUser()));
    basic.fields.push_back(makeInfo("Lab_OsRelativesizev", "相对容量", percentBuffer));
    basic.fields.push_back(makeInfo("Lab_RunEnvironv", "运行环境", runEnvironment()));
    page.groups.push_back(std::move(basic));

    // DFM: Gb_Osallinfo『封装体检』——只保留体检按钮与结果行；
    // Cb_OsRepair/Btn_Repairend（一键修复）的修复动作在图纸与宪章里都查不到，不做
    Group checkup;
    checkup.id = "Gb_Osallinfo";
    checkup.caption = "封装体检";
    checkup.fields.push_back(makeButton("Btn_Scan", "封装体检", "scan", "重跑 M2 的采集，等价于 --checkup"));
    checkup.fields.push_back(
        makeInfo("Lab_ScanResults", "检测结果", "点『封装体检』后重新采集（--checkup 的完整结果）")
    );
    page.groups.push_back(std::move(checkup));

    // DFM: Gb_SRS『导入磁盘控制器驱动』——对应 M6 的 --inject-drivers 两种模式
    Group srs;
    srs.id = "Gb_SRS";
    srs.caption = "导入磁盘控制器驱动";
    srs.fields.push_back(makeRadio(
        "RB_LoadSCsrs",
        "加载随盘 SRS 驱动",
        true,
        "",
        "对应 --inject-drivers --srs；本页不持久化，按执行的模式选择"
    ));
    srs.fields.push_back(
        makeRadio("RB_Loaddiy", "手动指定驱动目录", false, "", "对应 --inject-drivers --driver")
    );
    srs.fields.push_back(makeCombo(
        "Cb_osDrives",
        "目标系统分区",
        std::vector<std::string>{"(本机分区)", "A:", "B:", "C:", "D:", "E:", "F:", "G:", "H:",
                                 "I:",         "J:", "K:", "L:", "M:", "N:", "O:", "P:", "Q:",
                                 "R:",         "S:", "T:", "U:", "V:", "W:", "X:", "Y:", "Z:"},
        std::vector<std::string>{"",   "A:", "B:", "C:", "D:", "E:", "F:", "G:", "H:",
                                 "I:", "J:", "K:", "L:", "M:", "N:", "O:", "P:", "Q:",
                                 "R:", "S:", "T:", "U:", "V:", "W:", "X:", "Y:", "Z:"},
        0,
        "",
        "DFM 里该下拉是 C: 到 Z:；此处补一条『(本机分区)』表示跟随本机枚举"
    ));
    srs.fields.push_back(makeEdit("Ed_Loaddiy", "自定义驱动目录", "", "", "对应 --inject-drivers 的 --driver")
    );
    srs.fields.push_back(makeButton(
        "Btn_PEloadsrs", "执行", "srsRun", "只暂存意图；真正执行要由 --inject-drivers --execute 走 M6 的路径"
    ));
    page.groups.push_back(std::move(srs));

    return page;
}

// ---------------------------------------------------------------- 目标系统

namespace {

Page targetPage(const config::Config& cfg)
{
    Page page;
    page.id = "target";
    page.title = "目标系统";
    page.dfmSheet = "MlSkinTabSheet2";

    // DFM: Gb_Targetsys_info『系统信息』
    Group info;
    info.id = "Gb_Targetsys_info";
    info.caption = "系统信息";
    info.fields.push_back(makeEdit(
        "Ed_Owner",
        "注册名称",
        cfg.systemInfo.registeredName,
        "systemInfo.registeredName",
        "图纸 Caption『注册名称:』"
    ));
    info.fields.push_back(makeEdit(
        "Ed_Organization",
        "注册单位",
        cfg.systemInfo.registeredOrg,
        "systemInfo.registeredOrg",
        "图纸默认值 Microsoft"
    ));
    info.fields.push_back(makeEdit(
        "Ed_ProductKey",
        "序列号",
        cfg.systemInfo.serialNumber,
        "systemInfo.serialNumber",
        "图纸 Caption『序列号:』"
    ));
    {
        const std::vector<std::string> labels = {"固定名称", "随机名称", "前缀+随机"};
        const std::vector<std::string> ids = {"fixed", "random", "prefixRandom"};
        info.fields.push_back(makeCombo(
            "Cb_Pcname",
            "计算机名",
            labels,
            ids,
            indexOf(
                ids,
                cfg.systemInfo.computerNameMode.empty() ? std::string("fixed")
                                                        : cfg.systemInfo.computerNameMode,
                0
            ),
            "systemInfo.computerNameMode",
            "图纸下拉为空（Items.Strings 缺）；取值按 config 的 computerNameMode 定义"
        ));
    }
    info.fields.push_back(makeEdit(
        "Ed_Pcname", "前缀名称", cfg.systemInfo.computerName, "systemInfo.computerName", "图纸默认值 SC-"
    ));
    info.fields.push_back(makeNumber(
        "Se_Boottime",
        "菜单时间(秒)",
        cfg.systemInfo.bootMenuTimeoutSec,
        "systemInfo.bootMenuTimeoutSec",
        "图纸默认值 3"
    ));
    page.groups.push_back(std::move(info));

    // DFM: Gb_Targetsys_Desktop『首次进桌面』——16 项里按 2026-10-25 裁决只留这 7 项
    Group desktop;
    desktop.id = "Gb_Targetsys_Desktop";
    desktop.caption = "首次进桌面";
    const std::string desktopNote = "行为落到 M4 运行计划的『进桌面』阶段（DFM: Btn_RunPlan5）";
    desktop.fields.push_back(makeCheck(
        "Cb_ShowDevmgmt",
        "运行设备管理器",
        cfg.firstLogon.runDeviceManager,
        "firstLogon.runDeviceManager",
        desktopNote
    ));
    desktop.fields.push_back(makeCheck(
        "Cb_Reboot",
        "运行完成后重启计算机",
        cfg.firstLogon.rebootAfterRun,
        "firstLogon.rebootAfterRun",
        desktopNote
    ));
    desktop.fields.push_back(makeCheck(
        "Cb_AutoSetip", "自动设置 ip 地址", cfg.optimization.autoSetIp, "optimization.autoSetIp", desktopNote
    ));
    desktop.fields.push_back(makeCheck(
        "Cb_CloseKeypad",
        "关闭笔记本小键盘",
        cfg.optimization.closeNumLock,
        "optimization.closeNumLock",
        desktopNote
    ));
    desktop.fields.push_back(makeCheck(
        "Cb_ClearSrs",
        "清理多余的 SRS 驱动",
        cfg.optimization.clearSrsDrivers,
        "optimization.clearSrsDrivers",
        desktopNote
    ));
    desktop.fields.push_back(makeCheck(
        "Cb_Dormancy",
        "开启笔记本休眠",
        cfg.optimization.enableHibernate,
        "optimization.enableHibernate",
        desktopNote
    ));
    {
        const std::vector<std::string> labels = {"不设置", "家庭网络", "工作网络", "公用网络"};
        const std::vector<std::string> ids = {"", "home", "work", "public"};
        desktop.fields.push_back(makeCombo(
            "Cb_SetNetWork",
            "自动设置网络位置",
            labels,
            ids,
            indexOf(ids, cfg.optimization.networkLocation, 0),
            "optimization.networkLocation",
            "图纸是复选框；config 用 networkLocation 字符串表达三种网络位置，故此处用下拉"
        ));
    }
    page.groups.push_back(std::move(desktop));

    // DFM: Gb_Targetsys_ResolutionSet『分辨率设置』
    Group resolution;
    resolution.id = "Gb_Targetsys_ResolutionSet";
    resolution.caption = "分辨率设置";
    resolution.fields.push_back(makeCheck(
        "Cb_SetResolution",
        "智能设置分辨率",
        cfg.optimization.setResolution,
        "optimization.setResolution",
        "授权新增字段"
    ));
    {
        std::vector<std::string> labels;
        std::vector<std::string> ids;
        for (const std::string& value : dfmResolutions())
        {
            labels.push_back(value);
            ids.push_back(value);
        }
        const std::string current = joinResolution(cfg.firstLogon.resolutionX, cfg.firstLogon.resolutionY);
        resolution.fields.push_back(makeCombo(
            "Cb_Resolution",
            "分辨率",
            labels,
            ids,
            indexOf(ids, current, -1),
            "firstLogon.resolutionX / resolutionY",
            "条目来自 DFM 里 Cb_Resolution 的 Items.Strings（11 条）"
        ));
    }
    resolution.fields.push_back(
        makeNumber("Ed_ResolutionX", "宽", cfg.firstLogon.resolutionX, "firstLogon.resolutionX", "图纸可手填")
    );
    resolution.fields.push_back(
        makeNumber("Ed_ResolutionY", "高", cfg.firstLogon.resolutionY, "firstLogon.resolutionY", "图纸可手填")
    );
    resolution.fields.push_back(makeNumber(
        "Ed_Refresh", "刷新率", cfg.firstLogon.refreshRate, "firstLogon.refreshRate", "图纸默认值 60"
    ));
    resolution.fields.push_back(makeNumber(
        "Ed_ColorQuality", "颜色质量", cfg.firstLogon.colorDepth, "firstLogon.colorDepth", "图纸默认值 32"
    ));
    page.groups.push_back(std::move(resolution));

    return page;
}

// ---------------------------------------------------------------- 计划任务

std::vector<std::vector<std::string>> taskRows(const config::Config& cfg)
{
    std::vector<std::vector<std::string>> rows;
    for (const config::RunPlanTask& task : cfg.runPlan)
    {
        runplan::Phase phase = runplan::Phase::PreInstall;
        std::string error;
        const std::string phaseText =
            runplan::parsePhase(task.phase, phase, error) ? runplan::phaseLabel(phase) : task.phase;
        rows.push_back({phaseText, task.type, task.target, task.arguments});
    }
    return rows;
}

Page tasksPage(const config::Config& cfg)
{
    Page page;
    page.id = "tasks";
    page.title = "计划任务";
    page.dfmSheet = "MlSkinTabSheet3";

    Group tasks;
    tasks.id = "Gb_Tasklist";
    tasks.caption = "计划任务";
    tasks.fields.push_back(makeList(
        "Lv_Tasklist",
        "任务列表",
        std::vector<std::string>{"阶段", "类型", "执行路径", "执行参数"},
        taskRows(cfg),
        "行来自 config.runPlan；DFM 里该控件是 TMlSkinListView"
    ));

    {
        const std::vector<std::string> labels = {"运行程序", "导注册表", "删除文件", "删除目录"};
        std::vector<std::string> ids;
        for (const runplan::TaskType type : runplan::allTaskTypes())
        {
            ids.emplace_back(runplan::taskTypeId(type));
        }
        tasks.fields.push_back(makeCombo(
            "Cb_Tasktype",
            "任务类型",
            labels,
            ids,
            0,
            "runPlan[].type",
            "取值来自 core/runplan 的 4 种任务类型；图纸下拉为空"
        ));
    }

    // DFM 里是 5 个按钮（Btn_RunPlan1..5），标签逐字为 部署前/部署中/部署后/登录时/桌面后；
    // 此处用互斥单选表达同一语义（不复刻 SC 观感）。
    {
        std::vector<std::string> labels;
        std::vector<std::string> ids;
        appendPhaseOptions(labels, ids);
        const std::string note = "DFM 同名按钮的 Caption 逐字为 部署前/部署中/部署后/登录时/桌面后";
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            tasks.fields.push_back(
                makeRadio("Btn_RunPlan" + std::to_string(i + 1), labels[i], i == 0, "runPlan[].phase", note)
            );
        }
    }

    {
        const std::vector<std::string> labels = {"正常窗口", "隐藏窗口", "最小化", "最大化"};
        std::vector<std::string> ids;
        for (const runplan::WindowStyle style : runplan::allWindowStyles())
        {
            ids.emplace_back(runplan::windowStyleId(style));
        }
        tasks.fields.push_back(makeCombo(
            "Cb_RunMode",
            "运行方式",
            labels,
            ids,
            0,
            "runPlan[].windowStyle",
            "取值来自 core/runplan 的 4 种运行方式（5×4×4×3×4 = 960 组合之一维）"
        ));
    }

    {
        const std::vector<std::string> labels = {"等待完成", "不等待", "自定义秒数"};
        std::vector<std::string> ids;
        for (const runplan::WaitStrategy strategy : runplan::allWaitStrategies())
        {
            ids.emplace_back(runplan::waitStrategyId(strategy));
        }
        tasks.fields.push_back(makeCombo(
            "Cb_RunWait",
            "运行等待",
            labels,
            ids,
            0,
            "runPlan[].waitMode",
            "取值来自 core/runplan 的 3 种等待策略"
        ));
    }

    tasks.fields.push_back(
        makeNumber("Sed_WaitTime", "等待时间(秒)", 0, "runPlan[].waitSeconds", "配合『自定义秒数』")
    );

    {
        // 2026-10-25 裁决：按 M4 已有模型定，善后处理 = 不处理/删除文件/删除目录/重启
        const std::vector<std::string> labels = {"不处理", "删除文件", "删除目录", "重启"};
        const std::vector<std::string> ids = {"keep", "deleteFile", "deleteDir", "reboot"};
        tasks.fields.push_back(makeCombo(
            "Cb_RunAfter",
            "善后处理",
            labels,
            ids,
            0,
            "runPlan[].cleanup",
            "2026-10-25 裁决；core/runplan 目前实现 keep/deleteFile 两种"
        ));
    }

    {
        const std::vector<std::string> labels = {"系统路径", "外置U盘", "外置光盘", "所有盘符"};
        std::vector<std::string> ids;
        for (const runplan::PathKind kind : runplan::allPathKinds())
        {
            ids.emplace_back(runplan::pathKindId(kind));
        }
        tasks.fields.push_back(makeCombo(
            "Cb_RunPath",
            "执行路径",
            labels,
            ids,
            0,
            "runPlan[].searchScope",
            "取值来自 core/runplan 的 4 种路径类型"
        ));
    }

    tasks.fields.push_back(makeEdit("Ed_RunPath", "路径", "", "runPlan[].target", "DFM: Ed_RunPath"));
    tasks.fields.push_back(makeEdit("Ed_CmdLine", "执行参数", "", "runPlan[].arguments", "DFM: Ed_CmdLine"));
    tasks.fields.push_back(makeButton("Btn_Add", "添加", "addTask", "把上面这一行追加进 config.runPlan"));
    page.groups.push_back(std::move(tasks));

    return page;
}

// ---------------------------------------------------------------- 部署过程

Page deployPage(const config::Config& cfg)
{
    Page page;
    page.id = "deploy";
    page.title = "部署过程";
    page.dfmSheet = "MlSkinTabSheet4";

    Group set;
    set.id = "Gb_DeploySet";
    set.caption = "部署设置";
    set.fields.push_back(makeEdit(
        "Ed_PicPath",
        "部署背景图片路径",
        cfg.deploy.backgroundImage,
        "deploy.backgroundImage",
        "只存配置，图形化部署画面按宪章后置"
    ));
    set.fields.push_back(makeNumber(
        "Ed_PicSpeed",
        "多图切换速度(秒)",
        cfg.deploy.imageSwitchSeconds,
        "deploy.imageSwitchSeconds",
        "图纸默认值 10"
    ));
    {
        std::vector<std::string> labels = {"不设置"};
        std::vector<std::string> ids = {"0x0"};
        for (const std::string& value : dfmResolutions())
        {
            labels.push_back(value);
            ids.push_back(value);
        }
        const std::string current =
            joinResolution(cfg.deploy.deployResolutionX, cfg.deploy.deployResolutionY);
        set.fields.push_back(makeCombo(
            "Cb_DeployResolution",
            "部署分辨率",
            labels,
            ids,
            indexOf(ids, current, 0),
            "deploy.deployResolutionX / deployResolutionY",
            "图纸该下拉为空；沿用同页分辨率集合，另加『不设置』"
        ));
    }
    set.fields.push_back(makeNumber(
        "Ed_DeployResolution_Width",
        "部署分辨率宽",
        cfg.deploy.deployResolutionX,
        "deploy.deployResolutionX",
        "0 = 不设置"
    ));
    set.fields.push_back(makeNumber(
        "Ed_DeployResolution_Height",
        "部署分辨率高",
        cfg.deploy.deployResolutionY,
        "deploy.deployResolutionY",
        "0 = 不设置"
    ));
    set.fields.push_back(
        makeEdit("Ed_DeployFontcolour", "部署文字颜色", cfg.deploy.fontColor, "deploy.fontColor", "只存配置")
    );
    set.fields.push_back(makeEdit(
        "Ed_DeployTitle",
        "部署顶部标题",
        cfg.deploy.titleText,
        "deploy.titleText",
        "图纸另有 Cb_DeployTitle 开关，开关语义由本字段非空表达"
    ));
    set.fields.push_back(
        makeCheck("Cb_Skipoobe", "跳过 OOBE", cfg.deploy.skipOobe, "deploy.skipOobe", "图纸 Caption 逐字")
    );
    set.fields.push_back(makeCheck(
        "Cb_AddUser",
        "自动新增系统用户",
        cfg.deploy.autoCreateUser,
        "deploy.autoCreateUser",
        "图纸 Caption 逐字"
    ));
    set.fields.push_back(makeCheck(
        "Btn_DisableVmrun",
        "禁止在虚拟机里部署",
        cfg.deploy.blockVirtualMachine,
        "deploy.blockVirtualMachine",
        "图纸里它是复选框"
    ));
    set.fields.push_back(makeCheck(
        "Cb_Tampering",
        "校验文件防篡改（用户态）",
        cfg.deploy.verifyIntegrity,
        "deploy.verifyIntegrity",
        "宪章 §7：防篡改只做用户态校验"
    ));
    set.fields.push_back(makeCheck(
        "Cb_Admin",
        "开启 administrator",
        cfg.deploy.enableAdministrator,
        "deploy.enableAdministrator",
        "图纸 Caption 逐字"
    ));
    page.groups.push_back(std::move(set));

    return page;
}

// ---------------------------------------------------------------- 封装过程

Page sysprepPage(const config::Config& cfg)
{
    Page page;
    page.id = "sysprep";
    page.title = "封装过程";
    page.dfmSheet = "MlSkinTabSheet5";

    Group run;
    run.id = "Gb_Sysprep_Run";
    run.caption = "封装";
    run.fields.push_back(makeButton(
        "Btn_Syspreprun", "开始封装", "sysprepRun", "只暂存意图；真正封装要由 --sysprep --execute 启动"
    ));
    page.groups.push_back(std::move(run));

    Group device;
    device.id = "Gb_Sysprep_Runset";
    device.caption = "设备类";
    device.fields.push_back(makeCheck(
        "Cb_Uninstalldriver",
        "卸载本机驱动",
        cfg.sysprep.uninstallDrivers,
        "sysprep.uninstallDrivers",
        "图纸默认勾选"
    ));
    device.fields.push_back(makeCheck(
        "Cb_LoadSrsDrv",
        "加载 SRS 磁盘控制器驱动",
        cfg.sysprep.loadSrsDrivers,
        "sysprep.loadSrsDrivers",
        "图纸默认勾选；对应 M6 的 SRS 注入"
    ));
    device.fields.push_back(makeCheck(
        "Cb_Createnewsid",
        "在目标计算机系统中生成全新的安全标识符",
        cfg.sysprep.generateNewSid,
        "sysprep.generateNewSid",
        "即 /generalize"
    ));
    page.groups.push_back(std::move(device));

    Group type;
    type.id = "Gb_Sysprep_Type";
    type.caption = "封装方式";
    // 图纸另外两项是『联盟封装方式』『会员封装方式』——宪章 §7 红线，一律不做
    type.fields.push_back(makeRadio(
        "Rb_Sysprep_Type1",
        "传统封装方式",
        true,
        "sysprep.mode",
        "图纸默认选中；联盟/会员封装方式按 §7 红线不做"
    ));
    page.groups.push_back(std::move(type));

    Group sysset;
    sysset.id = "Gb_Sysprep_Sysset";
    sysset.caption = "系统类";
    sysset.fields.push_back(makeCheck(
        "Cb_Cutdesktopfile",
        "转移桌面文件到所有用户",
        cfg.sysprep.transferDesktopFiles,
        "sysprep.transferDesktopFiles",
        "图纸 Caption 逐字"
    ));
    sysset.fields.push_back(makeCheck(
        "Cb_Cleanjunkfiles",
        "封装后清理系统无用文件",
        cfg.sysprep.cleanTemp,
        "sysprep.cleanTemp",
        "图纸默认勾选"
    ));
    sysset.fields.push_back(makeCheck(
        "Cb_Dllcache",
        "清理 DLLcache 目录并在部署时还原(减少体积)",
        cfg.sysprep.cleanDllCache,
        "sysprep.cleanDllCache",
        "图纸默认勾选"
    ));
    // 这两项没有 DFM 控件（SC 把痕迹清理散在运行计划事件里），是本项目新增的开关，
    // 由 --prepare 落地。放在「系统类」是因为它们和清理动作同属封装前的系统处理。
    sysset.fields.push_back(makeCheck(
        "Cb_ClearPrivacy",
        "清理使用痕迹（地址栏/运行/最近文档/对话框历史/托盘缓存）",
        cfg.optimization.clearPrivacyTraces,
        "optimization.clearPrivacyTraces",
        "本项目新增，图纸无对应控件；由 --prepare 执行"
    ));
    sysset.fields.push_back(makeCheck(
        "Cb_ClearUpdateCache",
        "清理 Windows 更新下载缓存",
        cfg.optimization.clearUpdateCache,
        "optimization.clearUpdateCache",
        "本项目新增，图纸无对应控件；由 --prepare 执行"
    ));
    sysset.fields.push_back(makeCheck(
        "Cb_AutoLogon",
        "自动登录（次数内免输入密码）",
        cfg.sysprep.autoLogon,
        "sysprep.autoLogon",
        "《功能规格》§五 系统类：自动登录（Administrator 密码 / 次数）"
    ));
    sysset.fields.push_back(makeEdit(
        "Ed_LogonPassword",
        "自动登录密码",
        cfg.sysprep.autoLogonPassword,
        "sysprep.autoLogonPassword",
        "留空时走 Winlogon 注册表（空密码的 <AutoLogon> 会被系统忽略）"
    ));
    sysset.fields.push_back(makeNumber(
        "Se_LogonCount",
        "自动登录次数",
        cfg.sysprep.autoLogonCount,
        "sysprep.autoLogonCount",
        "《功能规格》§五：次数"
    ));
    // 封装前清理：对齐 SysprepPreparator 的 PreparationTasks，全部默认关闭
    sysset.fields.push_back(makeCheck(
        "Cb_StopSysprep",
        "结束已在运行的 sysprep.exe",
        cfg.sysprep.stopSysprepProcess,
        "sysprep.stopSysprepProcess",
        "不先停会报「sysprep 已在运行」"
    ));
    sysset.fields.push_back(makeCheck(
        "Cb_CleanShadow",
        "删除卷影副本与还原点",
        cfg.sysprep.cleanShadowCopies,
        "sysprep.cleanShadowCopies",
        "不可恢复"
    ));
    sysset.fields.push_back(makeCheck(
        "Cb_CleanEventLog",
        "清空系统事件日志（System/Application/Setup/PowerShell）",
        cfg.sysprep.cleanEventLogs,
        "sysprep.cleanEventLogs",
        "只清这四个固定日志，不做 wevtutil el 全量枚举"
    ));
    sysset.fields.push_back(makeCheck(
        "Cb_CleanRecycleBin",
        "清空回收站（当前用户）",
        cfg.sysprep.cleanRecycleBin,
        "sysprep.cleanRecycleBin",
        "按当前用户 SID 定位，只清固定磁盘"
    ));
    sysset.fields.push_back(makeCheck(
        "Cb_CleanComponentStore",
        "DISM 组件清理（不含 /resetbase）",
        cfg.sysprep.cleanComponentStore,
        "sysprep.cleanComponentStore",
        "/resetbase 会让已装更新无法卸载，故意不加；耗时较长"
    ));
    sysset.fields.push_back(makeCheck(
        "Cb_RemoveUnattend",
        "删除残留的 unattend.xml",
        cfg.sysprep.removeUnattendResidue,
        "sysprep.removeUnattendResidue",
        "Panther / Sysprep / 系统盘根三处；下次 Setup 可能重跑旧应答文件"
    ));
    page.groups.push_back(std::move(sysset));

    Group net;
    net.id = "Gb_Sysprep_Netset";
    net.caption = "网络类";
    const bool joinDomain = !cfg.sysprep.domain.empty();
    net.fields.push_back(makeRadio(
        "Rb_WorkGroup", "工作组", !joinDomain, "sysprep.workgroup", "与 Rb_Domain 互斥：选中即清空 domain"
    ));
    net.fields.push_back(makeRadio(
        "Rb_Domain", "加入域", joinDomain, "sysprep.domain", "与 Rb_WorkGroup 互斥：选中即写入 domain"
    ));
    net.fields.push_back(makeEdit(
        "Ed_WorkGroup", "工作组名", cfg.sysprep.workgroup, "sysprep.workgroup", "图纸默认值 WorkGroup"
    ));
    net.fields.push_back(
        makeEdit("Ed_Domain_name", "域名", cfg.sysprep.domain, "sysprep.domain", "DFM: Ed_Domain_name")
    );
    net.fields.push_back(makeEdit(
        "Ed_Domain_user", "域用户", cfg.sysprep.domainUser, "sysprep.domainUser", "DFM: Ed_Domain_user"
    ));
    net.fields.push_back(makeEdit(
        "Ed_Domain_pw",
        "域密码",
        cfg.sysprep.domainPassword,
        "sysprep.domainPassword",
        "授权新增字段；界面上以掩码显示"
    ));
    page.groups.push_back(std::move(net));

    Group server;
    server.id = "Gb_Sysprep_Ser";
    server.caption = "服务器类";
    server.fields.push_back(makeRadio(
        "Rb_EveryOne",
        "每设备或每用户",
        !cfg.sysprep.perServerLicensing,
        "sysprep.perServerLicensing",
        "图纸默认选中"
    ));
    server.fields.push_back(makeRadio(
        "Rb_EverySer",
        "每服务器,同时连接数",
        cfg.sysprep.perServerLicensing,
        "sysprep.perServerLicensing",
        "授权新增字段"
    ));
    server.fields.push_back(makeNumber(
        "Se_Serconut", "同时连接数", cfg.sysprep.maxConnections, "sysprep.maxConnections", "图纸默认值 5"
    ));
    page.groups.push_back(std::move(server));

    Group after;
    after.id = "Gb_AfterSysprep";
    after.caption = "封装后处理";
    {
        // 图纸该下拉为空；取值按 M6 的 sysprep 模型（/quit /reboot /shutdown）
        const std::vector<std::string> labels = {"停留在当前界面", "重启", "关机"};
        const std::vector<std::string> ids = {"stay", "reboot", "shutdown"};
        after.fields.push_back(makeCombo(
            "Cb_AfterSysprep",
            "封装后处理",
            labels,
            ids,
            indexOf(ids, cfg.sysprep.afterAction.empty() ? std::string("stay") : cfg.sysprep.afterAction, 0),
            "sysprep.afterAction",
            "图纸下拉为空；取值按 M6 的 /quit /reboot /shutdown 三态"
        ));
    }
    page.groups.push_back(std::move(after));

    return page;
}

// ---------------------------------------------------------------- 运行期页面

Page runPage()
{
    Page page;
    page.id = "run";
    page.title = "运行";
    page.dfmSheet = "MlSkinTabSheet6";
    page.configPage = false;

    Group work;
    work.id = "Pan_Work";
    work.caption = "运行";
    work.fields.push_back(makeInfo("Lab_Run", "当前动作", "正在执行，请稍候..."));
    work.fields.push_back(makeInfo("Lab_Total", "总进度", "总进度:"));
    work.fields.push_back(makeProgress("Pro_Total", "总进度", 0));
    work.fields.push_back(makeProgress("Pro_Task", "当前任务", 0));
    page.groups.push_back(std::move(work));

    return page;
}

Page donePage()
{
    Page page;
    page.id = "done";
    page.title = "完成";
    page.dfmSheet = "MlSkinTabSheet7";
    page.configPage = false;

    Group end;
    end.id = "Pan_End";
    end.caption = "完成";
    end.fields.push_back(makeInfo("Lab_Complete", "成功", "恭喜您，执行成功！"));
    end.fields.push_back(makeInfo("Lab_CompleteErr", "失败", "错误，执行失败！"));
    end.fields.push_back(makeProgress("Pro_End", "完成度", 100));
    end.fields.push_back(
        makeInfo("Lab_Countdown", "倒计时", "程序将在 10 秒后重启计算机，如需取消请点『返回』。")
    );
    // 图纸还有 Btn_Bugs（错误报告）——要连 SC 的服务器，2026-10-25 裁决不做
    end.fields.push_back(makeButton(
        "Btn_Shutdown", "关机", "afterShutdown", "只置 sysprep.afterAction=shutdown，界面不真的关机"
    ));
    end.fields.push_back(
        makeButton("Btn_Restart", "重启", "afterRestart", "只置 sysprep.afterAction=reboot，界面不真的重启")
    );
    page.groups.push_back(std::move(end));

    return page;
}

} // namespace

// ---------------------------------------------------------------- 页表

std::vector<Page> pagesFromConfig(const config::Config& cfg)
{
    std::vector<Page> pages;
    pages.push_back(systemPage());
    pages.push_back(targetPage(cfg));
    pages.push_back(tasksPage(cfg));
    pages.push_back(deployPage(cfg));
    pages.push_back(sysprepPage(cfg));
    pages.push_back(runPage());
    pages.push_back(donePage());
    return pages;
}

const Page* findPage(const std::vector<Page>& pages, const std::string& id)
{
    for (const Page& page : pages)
    {
        if (page.id == id)
        {
            return &page;
        }
    }
    return nullptr;
}

Page* findPage(std::vector<Page>& pages, const std::string& id)
{
    for (Page& page : pages)
    {
        if (page.id == id)
        {
            return &page;
        }
    }
    return nullptr;
}

const Field* findField(const Page& page, const std::string& id)
{
    for (const Group& group : page.groups)
    {
        for (const Field& field : group.fields)
        {
            if (field.id == id)
            {
                return &field;
            }
        }
    }
    return nullptr;
}

Field* findField(Page& page, const std::string& id)
{
    for (Group& group : page.groups)
    {
        for (Field& field : group.fields)
        {
            if (field.id == id)
            {
                return &field;
            }
        }
    }
    return nullptr;
}

std::size_t fieldCount(const Page& page)
{
    std::size_t count = 0;
    for (const Group& group : page.groups)
    {
        count += group.fields.size();
    }
    return count;
}

std::size_t editableFieldCount(const Page& page)
{
    std::size_t count = 0;
    for (const Group& group : page.groups)
    {
        for (const Field& field : group.fields)
        {
            if (field.kind != FieldKind::Info && field.kind != FieldKind::Progress &&
                field.kind != FieldKind::List && !field.binding.empty())
            {
                ++count;
            }
        }
    }
    return count;
}

// ---------------------------------------------------------------- 写回模型

void applyTo(const Page& page, config::Config& cfg)
{
    for (const Group& group : page.groups)
    {
        for (const Field& field : group.fields)
        {
            if (field.binding.empty())
            {
                continue;
            }
            const std::string& id = field.id;

            // 目标系统
            if (id == "Ed_Owner")
            {
                cfg.systemInfo.registeredName = field.value;
            }
            else if (id == "Ed_Organization")
            {
                cfg.systemInfo.registeredOrg = field.value;
            }
            else if (id == "Ed_ProductKey")
            {
                cfg.systemInfo.serialNumber = field.value;
            }
            else if (id == "Cb_Pcname")
            {
                cfg.systemInfo.computerNameMode = comboValue(field, cfg.systemInfo.computerNameMode);
            }
            else if (id == "Ed_Pcname")
            {
                cfg.systemInfo.computerName = field.value;
            }
            else if (id == "Se_Boottime")
            {
                cfg.systemInfo.bootMenuTimeoutSec = toInt(field.value, cfg.systemInfo.bootMenuTimeoutSec);
            }
            else if (id == "Cb_ShowDevmgmt")
            {
                cfg.firstLogon.runDeviceManager = field.checked;
            }
            else if (id == "Cb_Reboot")
            {
                cfg.firstLogon.rebootAfterRun = field.checked;
            }
            else if (id == "Cb_AutoSetip")
            {
                cfg.optimization.autoSetIp = field.checked;
            }
            else if (id == "Cb_CloseKeypad")
            {
                cfg.optimization.closeNumLock = field.checked;
            }
            else if (id == "Cb_ClearSrs")
            {
                cfg.optimization.clearSrsDrivers = field.checked;
            }
            else if (id == "Cb_Dormancy")
            {
                cfg.optimization.enableHibernate = field.checked;
            }
            else if (id == "Cb_SetNetWork")
            {
                cfg.optimization.networkLocation = comboValue(field, cfg.optimization.networkLocation);
            }
            else if (id == "Cb_SetResolution")
            {
                cfg.optimization.setResolution = field.checked;
            }
            else if (id == "Cb_Resolution")
            {
                const std::string value = comboValue(field, std::string());
                if (!value.empty())
                {
                    splitResolution(value, cfg.firstLogon.resolutionX, cfg.firstLogon.resolutionY);
                }
            }
            else if (id == "Ed_ResolutionX")
            {
                cfg.firstLogon.resolutionX = toInt(field.value, cfg.firstLogon.resolutionX);
            }
            else if (id == "Ed_ResolutionY")
            {
                cfg.firstLogon.resolutionY = toInt(field.value, cfg.firstLogon.resolutionY);
            }
            else if (id == "Ed_Refresh")
            {
                cfg.firstLogon.refreshRate = toInt(field.value, cfg.firstLogon.refreshRate);
            }
            else if (id == "Ed_ColorQuality")
            {
                cfg.firstLogon.colorDepth = toInt(field.value, cfg.firstLogon.colorDepth);
            }
            // 部署过程
            else if (id == "Ed_PicPath")
            {
                cfg.deploy.backgroundImage = field.value;
            }
            else if (id == "Ed_PicSpeed")
            {
                cfg.deploy.imageSwitchSeconds = toInt(field.value, cfg.deploy.imageSwitchSeconds);
            }
            else if (id == "Cb_DeployResolution")
            {
                const std::string value = comboValue(field, std::string());
                if (!value.empty())
                {
                    splitResolution(value, cfg.deploy.deployResolutionX, cfg.deploy.deployResolutionY);
                }
            }
            else if (id == "Ed_DeployResolution_Width")
            {
                cfg.deploy.deployResolutionX = toInt(field.value, cfg.deploy.deployResolutionX);
            }
            else if (id == "Ed_DeployResolution_Height")
            {
                cfg.deploy.deployResolutionY = toInt(field.value, cfg.deploy.deployResolutionY);
            }
            else if (id == "Ed_DeployFontcolour")
            {
                cfg.deploy.fontColor = field.value;
            }
            else if (id == "Ed_DeployTitle")
            {
                cfg.deploy.titleText = field.value;
            }
            else if (id == "Cb_Skipoobe")
            {
                cfg.deploy.skipOobe = field.checked;
            }
            else if (id == "Cb_AddUser")
            {
                cfg.deploy.autoCreateUser = field.checked;
            }
            else if (id == "Btn_DisableVmrun")
            {
                cfg.deploy.blockVirtualMachine = field.checked;
            }
            else if (id == "Cb_Tampering")
            {
                cfg.deploy.verifyIntegrity = field.checked;
            }
            else if (id == "Cb_Admin")
            {
                cfg.deploy.enableAdministrator = field.checked;
            }
            // 封装过程
            else if (id == "Cb_Uninstalldriver")
            {
                cfg.sysprep.uninstallDrivers = field.checked;
            }
            else if (id == "Cb_LoadSrsDrv")
            {
                cfg.sysprep.loadSrsDrivers = field.checked;
            }
            else if (id == "Cb_Createnewsid")
            {
                cfg.sysprep.generateNewSid = field.checked;
            }
            else if (id == "Rb_Sysprep_Type1")
            {
                cfg.sysprep.mode = "classic";
            }
            else if (id == "Cb_Cutdesktopfile")
            {
                cfg.sysprep.transferDesktopFiles = field.checked;
            }
            else if (id == "Cb_Cleanjunkfiles")
            {
                cfg.sysprep.cleanTemp = field.checked;
            }
            else if (id == "Cb_Dllcache")
            {
                cfg.sysprep.cleanDllCache = field.checked;
            }
            else if (id == "Cb_ClearPrivacy")
            {
                cfg.optimization.clearPrivacyTraces = field.checked;
            }
            else if (id == "Cb_ClearUpdateCache")
            {
                cfg.optimization.clearUpdateCache = field.checked;
            }
            else if (id == "Cb_AutoLogon")
            {
                cfg.sysprep.autoLogon = field.checked;
            }
            else if (id == "Ed_LogonPassword")
            {
                cfg.sysprep.autoLogonPassword = field.value;
            }
            else if (id == "Se_LogonCount")
            {
                cfg.sysprep.autoLogonCount = toInt(field.value, cfg.sysprep.autoLogonCount);
            }
            else if (id == "Cb_StopSysprep")
            {
                cfg.sysprep.stopSysprepProcess = field.checked;
            }
            else if (id == "Cb_CleanShadow")
            {
                cfg.sysprep.cleanShadowCopies = field.checked;
            }
            else if (id == "Cb_CleanEventLog")
            {
                cfg.sysprep.cleanEventLogs = field.checked;
            }
            else if (id == "Cb_CleanRecycleBin")
            {
                cfg.sysprep.cleanRecycleBin = field.checked;
            }
            else if (id == "Cb_CleanComponentStore")
            {
                cfg.sysprep.cleanComponentStore = field.checked;
            }
            else if (id == "Cb_RemoveUnattend")
            {
                cfg.sysprep.removeUnattendResidue = field.checked;
            }
            else if (id == "Ed_WorkGroup")
            {
                cfg.sysprep.workgroup = field.value;
            }
            else if (id == "Ed_Domain_name")
            {
                cfg.sysprep.domain = field.value;
            }
            else if (id == "Ed_Domain_user")
            {
                cfg.sysprep.domainUser = field.value;
            }
            else if (id == "Ed_Domain_pw")
            {
                cfg.sysprep.domainPassword = field.value;
            }
            else if (id == "Se_Serconut")
            {
                cfg.sysprep.maxConnections = toInt(field.value, cfg.sysprep.maxConnections);
            }
            else if (id == "Cb_AfterSysprep")
            {
                cfg.sysprep.afterAction = comboValue(field, cfg.sysprep.afterAction);
            }
        }
    }

    // 单选组：两个成员谁是选中态，决定了另一个字段是否清空
    if (const Field* workgroup = findField(page, "Rb_WorkGroup"))
    {
        if (const Field* domain = findField(page, "Rb_Domain"))
        {
            if (domain->checked && !workgroup->checked)
            {
                // 加入域：工作组字段保留用户输入，但语义上以 domain 为准
            }
            else if (workgroup->checked && !domain->checked)
            {
                cfg.sysprep.domain.clear();
                cfg.sysprep.domainUser.clear();
                cfg.sysprep.domainPassword.clear();
            }
        }
    }
    if (const Field* everyone = findField(page, "Rb_EveryOne"))
    {
        if (const Field* perserver = findField(page, "Rb_EverySer"))
        {
            cfg.sysprep.perServerLicensing = perserver->checked && !everyone->checked;
        }
    }
}

// ---------------------------------------------------------------- 页面 <-> 配置

config::Config configFromPages(const std::vector<Page>& pages)
{
    config::Config cfg;
    for (const Page& page : pages)
    {
        if (!page.configPage)
        {
            continue; // 运行 / 完成是运行期页面，不参与配置
        }
        applyTo(page, cfg);
    }
    return cfg;
}

bool loadConfigFile(const std::string& path, config::Config& out, std::string& error)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        error = "打不开文件：" + path;
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (text.empty())
    {
        error = "文件是空的：" + path;
        return false;
    }
    // 配置模型的解析失败会返回错误而不是抛异常，这里照原样转成可读信息
    const config::ParseResult parsed = config::fromJson(text);
    if (!parsed.ok)
    {
        error = "不是合法的配置 JSON：" + parsed.error;
        return false;
    }
    if (parsed.config.schemaVersion != config::kSchemaVersion)
    {
        error = "schemaVersion 不符：文件是 " + std::to_string(parsed.config.schemaVersion) +
            "，本程序支持 " + std::to_string(config::kSchemaVersion);
        return false;
    }
    out = parsed.config;
    return true;
}

bool saveConfigFile(const std::string& path, const config::Config& cfg, std::string& error)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
    {
        error = "写不了文件：" + path;
        return false;
    }
    const std::string text = config::toJson(cfg);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream)
    {
        error = "写文件失败：" + path;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- 任务草稿

config::RunPlanTask taskFromPage(const Page& tasksPage)
{
    config::RunPlanTask task;
    task.phase = "preinstall";
    task.type = "run";
    task.windowStyle = "normal";
    task.waitMode = "wait";
    task.cleanup = "keep";
    task.searchScope = "systemPath";

    if (const Field* field = findField(tasksPage, "Cb_Tasktype"))
    {
        task.type = comboValue(*field, task.type);
    }
    for (int i = 1; i <= 5; ++i)
    {
        if (const Field* field = findField(tasksPage, "Btn_RunPlan" + std::to_string(i)))
        {
            if (field->checked)
            {
                // Btn_RunPlan1..5 的顺序就是 core/runplan 里 Phase 的枚举顺序
                task.phase = runplan::phaseId(static_cast<runplan::Phase>(i - 1));
            }
        }
    }
    if (const Field* field = findField(tasksPage, "Cb_RunMode"))
    {
        task.windowStyle = comboValue(*field, task.windowStyle);
    }
    if (const Field* field = findField(tasksPage, "Cb_RunWait"))
    {
        task.waitMode = comboValue(*field, task.waitMode);
    }
    if (const Field* field = findField(tasksPage, "Sed_WaitTime"))
    {
        task.waitSeconds = toInt(field->value, 0);
    }
    if (const Field* field = findField(tasksPage, "Cb_RunAfter"))
    {
        task.cleanup = comboValue(*field, task.cleanup);
    }
    if (const Field* field = findField(tasksPage, "Cb_RunPath"))
    {
        task.searchScope = comboValue(*field, task.searchScope);
    }
    if (const Field* field = findField(tasksPage, "Ed_RunPath"))
    {
        task.target = field->value;
    }
    if (const Field* field = findField(tasksPage, "Ed_CmdLine"))
    {
        task.arguments = field->value;
    }
    return task;
}

void fillTaskPage(Page& tasksPage, const config::RunPlanTask& task)
{
    runplan::Phase phase = runplan::Phase::PreInstall;
    std::string error;
    const bool known = runplan::parsePhase(task.phase, phase, error);

    const auto setCombo = [&tasksPage](const std::string& id, const std::string& value) {
        if (Field* field = findField(tasksPage, id))
        {
            field->comboIndex = indexOf(field->optionIds, value, field->comboIndex);
            if (field->comboIndex >= 0 && field->comboIndex < static_cast<int>(field->options.size()))
            {
                field->value = field->options[static_cast<std::size_t>(field->comboIndex)];
            }
        }
    };

    setCombo("Cb_Tasktype", task.type);
    setCombo("Cb_RunMode", task.windowStyle);
    setCombo("Cb_RunWait", task.waitMode);
    setCombo("Cb_RunAfter", task.cleanup);
    setCombo("Cb_RunPath", task.searchScope);

    for (int i = 1; i <= 5; ++i)
    {
        if (Field* field = findField(tasksPage, "Btn_RunPlan" + std::to_string(i)))
        {
            // Btn_RunPlan1..5 的顺序就是 core/runplan 里 Phase 的枚举顺序
            field->checked = (known && phase == static_cast<runplan::Phase>(i - 1));
        }
    }
    if (Field* field = findField(tasksPage, "Sed_WaitTime"))
    {
        field->value = std::to_string(task.waitSeconds);
    }
    if (Field* field = findField(tasksPage, "Ed_RunPath"))
    {
        field->value = task.target;
    }
    if (Field* field = findField(tasksPage, "Ed_CmdLine"))
    {
        field->value = task.arguments;
    }
}

// ---------------------------------------------------------------- 走查名单

std::vector<std::string> expectedControlIds(const std::string& pageId)
{
    if (pageId == "system")
    {
        return {
            "Lab_Osnamev",
            "Lab_Osbitv",
            "Lab_Osntverv",
            "Lab_SysprepSupportv",
            "Lab_Osdrivesv",
            "Lab_Osusesizev",
            "Lab_OsUserv",
            "Lab_OsRelativesizev",
            "Lab_RunEnvironv",
            "Btn_Scan",
            "Lab_ScanResults",
            "RB_LoadSCsrs",
            "RB_Loaddiy",
            "Cb_osDrives",
            "Ed_Loaddiy",
            "Btn_PEloadsrs",
        };
    }
    if (pageId == "target")
    {
        return {
            "Ed_Owner",       "Ed_Organization", "Ed_ProductKey", "Cb_Pcname",        "Ed_Pcname",
            "Se_Boottime",    "Cb_ShowDevmgmt",  "Cb_Reboot",     "Cb_AutoSetip",     "Cb_CloseKeypad",
            "Cb_ClearSrs",    "Cb_Dormancy",     "Cb_SetNetWork", "Cb_SetResolution", "Cb_Resolution",
            "Ed_ResolutionX", "Ed_ResolutionY",  "Ed_Refresh",    "Ed_ColorQuality",
        };
    }
    if (pageId == "tasks")
    {
        return {
            "Lv_Tasklist",
            "Cb_Tasktype",
            "Btn_RunPlan1",
            "Btn_RunPlan2",
            "Btn_RunPlan3",
            "Btn_RunPlan4",
            "Btn_RunPlan5",
            "Cb_RunMode",
            "Cb_RunWait",
            "Sed_WaitTime",
            "Cb_RunAfter",
            "Cb_RunPath",
            "Ed_RunPath",
            "Ed_CmdLine",
            "Btn_Add",
        };
    }
    if (pageId == "deploy")
    {
        return {
            "Ed_PicPath",
            "Ed_PicSpeed",
            "Cb_DeployResolution",
            "Ed_DeployResolution_Width",
            "Ed_DeployResolution_Height",
            "Ed_DeployFontcolour",
            "Ed_DeployTitle",
            "Cb_Skipoobe",
            "Cb_AddUser",
            "Btn_DisableVmrun",
            "Cb_Tampering",
            "Cb_Admin",
        };
    }
    if (pageId == "sysprep")
    {
        return {
            "Btn_Syspreprun",      "Cb_Uninstalldriver", "Cb_LoadSrsDrv",
            "Cb_Createnewsid",     "Rb_Sysprep_Type1",   "Cb_Cutdesktopfile",
            "Cb_Cleanjunkfiles",   "Cb_Dllcache",        "Cb_ClearPrivacy",
            "Cb_ClearUpdateCache", "Cb_AutoLogon",       "Ed_LogonPassword",
            "Se_LogonCount",       "Cb_StopSysprep",     "Cb_CleanShadow",
            "Cb_CleanEventLog",    "Cb_CleanRecycleBin", "Cb_CleanComponentStore",
            "Cb_RemoveUnattend",   "Rb_WorkGroup",       "Rb_Domain",
            "Ed_WorkGroup",        "Ed_Domain_name",     "Ed_Domain_user",
            "Ed_Domain_pw",        "Rb_EveryOne",        "Rb_EverySer",
            "Se_Serconut",         "Cb_AfterSysprep",
        };
    }
    if (pageId == "run")
    {
        return {"Lab_Run", "Lab_Total", "Pro_Total", "Pro_Task"};
    }
    if (pageId == "done")
    {
        return {"Lab_Complete", "Lab_CompleteErr", "Pro_End", "Lab_Countdown", "Btn_Shutdown", "Btn_Restart"};
    }
    return {};
}

std::vector<std::string> allExpectedControlIds()
{
    std::vector<std::string> all;
    for (const std::string& id : {"system", "target", "tasks", "deploy", "sysprep", "run", "done"})
    {
        for (const std::string& control : expectedControlIds(id))
        {
            all.push_back(control);
        }
    }
    return all;
}

std::vector<std::string> excludedControlIds()
{
    return {
        // 宪章 §7 红线：装机联盟分成 / 渠道号
        "Gb_ScTools",
        "Cb_Driveinstall",
        "Cb_Mboxinstall",
        "Lab_Ceounion",
        "Ed_Ceounionuid",
        "Mkb_Ceounion",
        "Btn_Logins",
        // 宪章 §7 红线：改写浏览器主页 / 默认搜索、预置收藏夹
        "Cb_Saveiehp",
        "Cb_SaveieFavorites",
        // 宪章 §7 红线：预置任务栏（推广快捷方式）
        "Cb_Locktaskbar",
        "Cb_Unlocktaskbar",
        // 宪章 §7：防流氓封装方式（联盟 / 会员封装方式）
        "Rb_Sysprep_Type2",
        "Rb_Sysprep_Type3",
        // 依赖 SC 的服务器：更新页与错误报告（2026-10-25 裁决不做）
        "Btn_Update",
        "Btn_Update_Exit",
        "MM_Updatelog",
        "Pb_Updating",
        "Lab_Updatingpr",
        "Lab_DiyUpdating",
        "Btn_Bugs",
        // 图纸与宪章都查不到交互语义：一律不做
        "Cb_CreateConnection",
        "Cb_MoveFavorites",
        "Cb_MoveDocuments",
        "Cb_MovePageFile",
        "Cb_CloseRestartbox",
        "Cb_ResolutionSos",
        "Gb_Tools",
        "Btn_OsRepair",
        "Btn_Repairend",
        "Lab_Rescan",
        "Lab_Rescanf",
        "Btn_Sysprepaset",
        "Lv_DeployModular",
        "Cb_DeployTitle",
        "Btn_OemSet",
        "Btn_help_V",
        // 内部图片暂存页，不是配置页
        "Pan_temp",
        "Pic_Listline",
        "Pic_Listwarning",
        "RowLine",
        "Pic_Listerror",
        "Pic_Listnormal",
        "Pic_Listser",
        "Pic_Stasks",
        "Pic_Srun",
        "Pic_OBJ",
    };
}

std::vector<std::string> forbiddenBindings()
{
    return {
        "firstLogon.syncIeHomepage",
        "firstLogon.syncIeFavorites",
        "optimization.pinTaskbarItems",
        "optimization.myDocumentsTarget",
        "optimization.pageFileTarget",
        "optimization.closeRebootDialog",
    };
}

// ---------------------------------------------------------------- JSON

std::string toJson(const std::vector<Page>& pages)
{
    nlohmann::json root;
    root["schemaVersion"] = 2;
    root["configPageCount"] = kConfigPageCount;

    nlohmann::json array = nlohmann::json::array();
    for (const Page& page : pages)
    {
        nlohmann::json item;
        item["id"] = page.id;
        item["title"] = page.title;
        item["dfmSheet"] = page.dfmSheet;
        item["configPage"] = page.configPage;
        item["fieldCount"] = fieldCount(page);

        nlohmann::json groups = nlohmann::json::array();
        for (const Group& group : page.groups)
        {
            nlohmann::json groupJson;
            groupJson["id"] = group.id;
            groupJson["caption"] = group.caption;
            nlohmann::json fields = nlohmann::json::array();
            for (const Field& field : group.fields)
            {
                nlohmann::json entry;
                entry["control"] = field.id;
                entry["label"] = field.label;
                entry["kind"] = fieldKindId(field.kind);
                entry["value"] = field.value;
                if (field.kind == FieldKind::Check || field.kind == FieldKind::Radio)
                {
                    entry["checked"] = field.checked;
                }
                if (field.kind == FieldKind::Combo)
                {
                    entry["comboIndex"] = field.comboIndex;
                    entry["options"] = field.options;
                }
                if (field.kind == FieldKind::Progress)
                {
                    entry["progress"] = field.progress;
                }
                if (field.kind == FieldKind::List)
                {
                    entry["columns"] = field.columns;
                    entry["rowCount"] = field.rows.size();
                }
                if (!field.binding.empty())
                {
                    entry["binding"] = field.binding;
                }
                if (!field.note.empty())
                {
                    entry["note"] = field.note;
                }
                fields.push_back(std::move(entry));
            }
            groupJson["fields"] = std::move(fields);
            groups.push_back(std::move(groupJson));
        }
        item["groups"] = std::move(groups);
        array.push_back(std::move(item));
    }
    root["pages"] = std::move(array);

    root["excludedControls"] = excludedControlIds();
    root["forbiddenBindings"] = forbiddenBindings();
    return root.dump(2) + "\n";
}

} // namespace osp::ui

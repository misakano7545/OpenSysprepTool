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

// DFM 里 TImage 的占位：矩形按图纸，图案由窗口层自绘（SC 的位图不搬）
Field makeImage(const std::string& id, const std::string& label, int x, int y, int width, int height)
{
    Field field;
    field.id = id;
    field.label = label;
    field.kind = FieldKind::Image;
    field.x = x;
    field.y = y;
    field.width = width;
    field.height = height;
    field.dfmClass = "TImage";
    field.note = "图纸矩形逐字；图案自绘，不使用 SC 的位图";
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
    // 与 DFM 的 Items.Strings 保持一致用 '*'：界面下拉的选中项要靠它反查，
    // 用 'x' 拼出来的串永远匹配不上图里的条目。
    char buffer[48] = {};
    std::snprintf(buffer, sizeof(buffer), "%d*%d", x, y);
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
    case FieldKind::Image:
        return "image";
    }
    return "unknown";
}

// ---------------------------------------------------------------- DFM 坐标套用

// 坐标表由 tools/gen_ui_coords.py 从 docs/sc_layout/TSCMF.json 生成，随仓库提交。
// 页面里的业务逻辑（下拉条目、列表行、动态取值）保持原样，只有位置来自 DFM。
namespace {

#include "ui_dfm_coords.inc"

// 把坐标表套到页面上。返回 false 表示「这一页还有字段没有图纸坐标」——
// 那时**不启用**绝对布局，宁可退回自动排列，也不要把控件堆到 (0,0)。
bool applyDfmCoords(Page& page)
{
    for (const DfmCoord& coord : kDfmCoords)
    {
        if (page.id != coord.page)
        {
            continue;
        }
        if (coord.field[0] == '\0')
        {
            for (Group& group : page.groups)
            {
                if (group.id == coord.group)
                {
                    group.x = coord.x;
                    group.y = coord.y;
                    group.width = coord.width;
                    group.height = coord.height;
                    group.visible = coord.visible;
                }
            }
            continue;
        }
        for (Group& group : page.groups)
        {
            if (group.id != coord.group && !(coord.group[0] == '\0' && group.id.empty()))
            {
                continue;
            }
            for (Field& field : group.fields)
            {
                if (field.id == coord.field)
                {
                    field.x = coord.x;
                    field.y = coord.y;
                    field.width = coord.width;
                    field.height = coord.height;
                }
            }
        }
    }

    // 我方比 SC 多出来的字段（自动登录三件套、单个 RunPlan 按钮…）图纸里没有位置。
    // 不把它们堆到 (0,0)，而是在图纸区域下方排一条「扩展区」，页面高度跟着加长。
    // 这样既保住图纸版面，也不丢功能 —— 少的那部分在 docs/SC窗体对照.md 里记着。
    const int designWidth = 800;
    const int designHeight = 398;
    int extensionY = designHeight + 8;
    int extensionX = 36;
    int extensionCount = 0;
    for (Group& group : page.groups)
    {
        for (Field& field : group.fields)
        {
            if (field.placed())
            {
                continue;
            }
            const int width = (std::min)(360, designWidth - 72);
            field.x = extensionX;
            field.y = extensionY;
            field.width = width;
            field.height = 22;
            if (extensionX + width * 2 + 20 < designWidth)
            {
                extensionX += width + 20;
            }
            else
            {
                extensionX = 36;
                extensionY += 28;
                ++extensionCount;
            }
        }
    }

    page.dfmLayout = true;
    page.clientWidth = designWidth;
    page.clientHeight = extensionCount > 0 ? extensionY + 28 : designHeight;
    return true;
}

} // namespace

// ---------------------------------------------------------------- 母盘系统

Page systemPage()
{
    return systemPage(sysinfo::collect());
}

Page systemPage(const sysinfo::CheckupReport& report)
{
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

    // 位置全部来自 DFM（tools/gen_ui_coords.py 生成）；字段缺坐标时该函数
    // 会返回 false，页面退回自动排列，绝不把控件堆到 (0,0)
    applyDfmCoords(page);
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
    // 整页按 DFM 绝对坐标摆放：分组框是绝对矩形，组内控件坐标相对分组框。
    page.dfmLayout = true;
    page.clientWidth = 800;  // Page_mf.Width
    page.clientHeight = 398; // Page_mf.Height

    // 下面这段由 tools/gen_ui_page.py 从 docs/sc_layout/TSCMF.json 生成：
    // 坐标与控件名逐字来自 DFM，人工只维护 tools/ui_bindings/target.json 里的
    // 「控件名 -> 配置项」映射。改布局请改映射表再重新生成，不要手改生成物。
#include "ui_pages_dfm.inc"

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

    // 位置全部来自 DFM（tools/gen_ui_coords.py 生成）；字段缺坐标时该函数
    // 会返回 false，页面退回自动排列，绝不把控件堆到 (0,0)
    applyDfmCoords(page);
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

    // 位置全部来自 DFM（tools/gen_ui_coords.py 生成）；字段缺坐标时该函数
    // 会返回 false，页面退回自动排列，绝不把控件堆到 (0,0)
    applyDfmCoords(page);
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

    // 位置全部来自 DFM（tools/gen_ui_coords.py 生成）；字段缺坐标时该函数
    // 会返回 false，页面退回自动排列，绝不把控件堆到 (0,0)
    applyDfmCoords(page);
    return page;
}

} // namespace

// ---------------------------------------------------------------- 运行期页面

// 运行页 / 完成页：这两个页在 SC 里是固定版面（居中大图 + 居中说明 + 底部进度条），
// 所以整页按 DFM 的绝对坐标摆放，不走引擎的纵向排列。
// 坐标与控件名逐条来自 docs/sc_layout/TSCMF.json（由 tools/dfm_layout.py 从 DFM 生成），
// 单测 test_ui 会拿同一份 JSON 逐项比对，抄错或漂移都会红。

Page runPage()
{
    Page page;
    page.id = "run";
    page.title = "运行";
    page.dfmSheet = "MlSkinTabSheet6";
    page.configPage = false;
    page.dfmLayout = true;
    page.clientWidth = 800;  // Page_mf.Width（DFM）
    page.clientHeight = 398; // Page_mf.Height（DFM）

    // Pan_Work 是 SC 的无边框面板（BorderSides = []），这里只用来承载控件，不画边框
    Group work;
    work.id = "Pan_Work";
    work.caption = "";

    // DFM 顺序：Pic_RunGif, Pro_Total, Pro_Task, Lab_Run, Lab_Total
    // Pic_RunGif 在 SC 里是一张动图（Picture.Data 48771 字节）。SC 的图片不能搬，
    // 这里按同一矩形（347,104 100x100）自绘一个转动的圆弧。
    work.fields.push_back(makeImage("Pic_RunGif", "运行动画", 347, 104, 100, 100));

    Field total;
    total.id = "Pro_Total";
    total.label = "总进度";
    total.kind = FieldKind::Progress;
    total.progress = 0;
    total.x = 97;
    total.y = 333;
    total.width = 600;
    total.height = 10;
    total.dfmClass = "TAeroColorProgressBar";
    total.note = "图纸坐标逐字；总进度条在任务进度条下方";
    work.fields.push_back(std::move(total));

    Field task;
    task.id = "Pro_Task";
    task.label = "当前任务";
    task.kind = FieldKind::Progress;
    task.progress = 0;
    task.x = 97;
    task.y = 322;
    task.width = 600;
    task.height = 5;
    task.dfmClass = "TAeroColorProgressBar";
    task.note = "图纸坐标逐字；高度只有 5px，是细条";
    work.fields.push_back(std::move(task));

    Field run;
    run.id = "Lab_Run";
    run.label = "";
    run.kind = FieldKind::Info;
    run.value = "正在执行，请稍候...";
    run.x = 0;
    run.y = 231;
    run.width = 794;
    run.height = 34;
    run.dfmClass = "TMlSkinLabel";
    run.align = 2;
    run.note = "图纸 Caption 逐字；AlignmentHorizontal = taCenter，居中显示";
    work.fields.push_back(std::move(run));

    Field totalLabel;
    totalLabel.id = "Lab_Total";
    totalLabel.label = "";
    totalLabel.kind = FieldKind::Info;
    totalLabel.value = "总进度:";
    totalLabel.x = 13;
    totalLabel.y = 331;
    totalLabel.width = 78;
    totalLabel.height = 22;
    totalLabel.dfmClass = "TMlSkinLabel";
    totalLabel.align = 1;
    totalLabel.note = "图纸 Caption 逐字；AlignmentHorizontal = taRightJustify，右对齐";
    work.fields.push_back(std::move(totalLabel));

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
    page.dfmLayout = true;
    page.clientWidth = 800;  // Page_mf.Width（DFM）
    page.clientHeight = 398; // Page_mf.Height（DFM）

    Group end;
    end.id = "Pan_End";
    end.caption = "";

    // DFM 顺序：PIC_Success, Lab_Complete, PIC_Error, Pro_End, Lab_Countdown,
    //           Lab_CompleteErr, Btn_Bugs, Btn_Shutdown, Btn_Restart
    // 成功图与失败图在图纸上占同一矩形（331,96 118x96），互斥显示。
    end.fields.push_back(makeImage("PIC_Success", "成功图示", 331, 96, 118, 96));
    end.fields.back().visible = false; // 结果出来后才显示
    end.fields.back().note = "图纸矩形逐字；SC 的位图不搬，这里自绘对勾";

    Field complete;
    complete.id = "Lab_Complete";
    complete.label = "";
    complete.kind = FieldKind::Info;
    complete.value = "恭喜您，执行成功！";
    complete.x = 13;
    complete.y = 209;
    complete.width = 762;
    complete.height = 19;
    complete.dfmClass = "TLabel";
    complete.note = "图纸 Caption 逐字";
    complete.visible = false; // 与 Lab_CompleteErr 二选一
    end.fields.push_back(std::move(complete));

    end.fields.push_back(makeImage("PIC_Error", "失败图示", 331, 96, 118, 96));
    end.fields.back().visible = false;
    end.fields.back().note = "图纸矩形逐字；与 PIC_Success 同位置，二选一显示";

    Field endProgress;
    endProgress.id = "Pro_End";
    endProgress.label = "完成度";
    endProgress.kind = FieldKind::Progress;
    endProgress.progress = 100;
    endProgress.x = 152;
    endProgress.y = 254;
    endProgress.width = 525;
    endProgress.height = 8;
    endProgress.dfmClass = "TAeroColorProgressBar";
    endProgress.note = "图纸坐标逐字；高度 8px";
    end.fields.push_back(std::move(endProgress));

    Field countdown;
    countdown.id = "Lab_Countdown";
    countdown.label = "";
    countdown.kind = FieldKind::Info;
    // 图纸原文用的是中文引号「返回」，逐字照抄
    countdown.value = "程序将在10秒后重启计算机，如需取消请点“返回”。";
    countdown.x = 152;
    countdown.y = 268;
    countdown.width = 288;
    countdown.height = 12;
    countdown.dfmClass = "TLabel";
    countdown.note = "图纸 Caption 逐字（含中文引号）";
    end.fields.push_back(std::move(countdown));

    Field completeErr;
    completeErr.id = "Lab_CompleteErr";
    completeErr.label = "";
    completeErr.kind = FieldKind::Info;
    completeErr.value = "错误，执行失败！";
    completeErr.x = 13;
    completeErr.y = 209;
    completeErr.width = 762;
    completeErr.height = 19;
    completeErr.dfmClass = "TLabel";
    completeErr.note = "图纸 Caption 逐字；与 Lab_Complete 同位置，二选一显示";
    completeErr.visible = false;
    end.fields.push_back(std::move(completeErr));

    // Btn_Bugs 在图纸上存在（331,268 118x27，Caption 'Bug'），但它是「错误报告」，
    // 要把崩溃信息发到 SC 的服务器 —— 宪章 §7 与 2026-10-25 裁决都不做。
    // 这里是本页唯一的有意缺项，见 docs/SC窗体对照.md 的备注列。

    Field shutdown;
    shutdown.id = "Btn_Shutdown";
    shutdown.label = "关机(&T)";
    shutdown.kind = FieldKind::Button;
    shutdown.action = "afterShutdown";
    shutdown.x = 275;
    shutdown.y = 242;
    shutdown.width = 94;
    shutdown.height = 27;
    shutdown.dfmClass = "TMlSkinColorButton";
    shutdown.note = "图纸 Caption 逐字（含快捷键标记）；只置 sysprep.afterAction=shutdown";
    end.fields.push_back(std::move(shutdown));

    Field restart;
    restart.id = "Btn_Restart";
    restart.label = "重启(&R)";
    restart.kind = FieldKind::Button;
    restart.action = "afterRestart";
    restart.x = 421;
    restart.y = 242;
    restart.width = 94;
    restart.height = 27;
    restart.dfmClass = "TMlSkinColorButton";
    restart.note = "图纸 Caption 逐字（含快捷键标记）；只置 sysprep.afterAction=reboot";
    end.fields.push_back(std::move(restart));

    page.groups.push_back(std::move(end));
    return page;
}

// ---------------------------------------------------------------- 页表

std::vector<Page> pagesFromConfig(const config::Config& cfg)
{
    return pagesFromConfig(cfg, sysinfo::collect());
}

std::vector<Page> pagesFromConfig(const config::Config& cfg, const sysinfo::CheckupReport& report)
{
    std::vector<Page> pages;
    pages.push_back(systemPage(report));
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
                // DFM 里这是勾选框（不是下拉）：勾了 = 自动设为工作网络
                cfg.optimization.autoNetworkLocation = field.checked;
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
            else if (id == "Cb_MovePageFile")
            {
                cfg.optimization.autoPagefileDrive = field.checked;
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
            else if (id == "Cb_CleanShadow")
            {
            }
            else if (id == "Cb_CleanEventLog")
            {
            }
            else if (id == "Cb_CleanRecycleBin")
            {
            }
            else if (id == "Cb_CleanComponentStore")
            {
            }
            else if (id == "Cb_RemoveUnattend")
            {
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
            "Cb_ShowDevmgmt",     "Cb_AutoSetip",     "Cb_CloseKeypad",   "Cb_MoveFavorites",
            "Cb_MoveDocuments",   "Cb_MovePageFile",  "Cb_Reboot",        "Cb_ClearSrs",
            "Cb_CloseRestartbox", "Cb_Dormancy",      "Cb_SetNetWork",    "Lab_Owner",
            "Ed_Owner",           "Ed_Organization",  "Lab_Organization", "Lab_ProductKey",
            "Ed_ProductKey",      "Lab_Pcname",       "Cb_Pcname",        "Lab_Pcnameprefix",
            "Ed_Pcname",          "Lab_Boottime",     "Se_Boottime",      "Lab_TimeCompany",
            "Btn_OemSet",         "Cb_SetResolution", "Cb_Resolution",    "Ed_ResolutionX",
            "Ed_ResolutionY",     "Lab_Refresh",      "Ed_Refresh",       "Lab_ColorQuality",
            "Ed_ColorQuality",    "Lab_tempX",
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
            "Btn_Syspreprun",    "Cb_Uninstalldriver", "Cb_LoadSrsDrv", "Cb_Createnewsid", "Rb_Sysprep_Type1",
            "Cb_Cutdesktopfile", "Cb_Cleanjunkfiles",  "Cb_Dllcache",   "Cb_AutoLogon",    "Ed_LogonPassword",
            "Se_LogonCount",     "Rb_WorkGroup",       "Rb_Domain",     "Ed_WorkGroup",    "Ed_Domain_name",
            "Ed_Domain_user",    "Ed_Domain_pw",       "Rb_EveryOne",   "Rb_EverySer",     "Se_Serconut",
            "Cb_AfterSysprep",
        };
    }
    if (pageId == "run")
    {
        // 顺序与 DFM 的 MlSkinTabSheet6 一致（Pic_RunGif 在最前）
        return {"Pic_RunGif", "Pro_Total", "Pro_Task", "Lab_Run", "Lab_Total"};
    }
    if (pageId == "done")
    {
        // 顺序与 DFM 的 MlSkinTabSheet7 一致；Btn_Bugs 是唯一有意缺项（错误报告要连 SC 的服务器）
        return {
            "PIC_Success",
            "Lab_Complete",
            "PIC_Error",
            "Pro_End",
            "Lab_Countdown",
            "Lab_CompleteErr",
            "Btn_Shutdown",
            "Btn_Restart",
        };
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
    // 这里的每一条都必须写明「为什么不做」。反过来，**已经做出来的控件不能留在这里** ——
    // M7 时期的名单把后来实现的一键修复、Ctrl+F11 急救、OEM 设置、部署顶部标题都列成了
    // 「不做」，形成自相矛盾。判断依据统一到 docs/SC功能对照.md。
    return {
        // 宪章 §7 红线：装机联盟分成 / 渠道号 / 推广软件安装
        "Gb_ScTools",
        "Cb_Driveinstall",
        "Cb_Mboxinstall",
        "Lab_Ceounion",
        "Ed_Ceounionuid",
        "Mkb_Ceounion",
        "Btn_Logins",
        // 宪章 §7 红线：改写浏览器主页 / 预置收藏夹
        "Cb_Saveiehp",
        "Cb_SaveieFavorites",
        // 任务栏固定/取消固定在 B9 已实现，但**只接受用户自己列出的文件**：
        // 这里仍然排除的是 SC 那套「预置推广快捷方式」的用法，不是机制本身。
        // 机制见 optimization.lockTaskbarFiles / unlockTaskbarFiles（空 = 什么都不做）。
        // 宪章 §7：防流氓 / 大客户高级方式都依赖联盟授权与内核驱动
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
        // 需要内核驱动常驻才能做到，宪章 §7 明令禁止（拒绝内核驱动防删除）
        "Page_Nodelfile",
        "Page_Nodelreg",
        // 尚未实现，按 docs/SC功能对照.md 排期（不是永久不做）
        "Cb_CreateConnection",
        "Btn_Execute",
        // 内部图片暂存页与工具栏容器，不是配置控件
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
        "Gb_Tools",
        "Btn_help_V",
        "Lab_Rescan",
        "Lab_Rescanf",
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
        // closeRebootDialog 已实现（--prepare 写 NoRebootDialog），不再属于「禁止出现」
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

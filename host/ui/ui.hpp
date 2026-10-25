#pragma once

// 主程序 UI（宪章 §8 M7）
//
// 框架：原生 Win32 标准控件 + WTL 薄封装，视觉完全自研，不复刻 SC 的换肤观感。
// 子系统保持 CONSOLE（顶层 CMakeLists 的 /SUBSYSTEM:CONSOLE,6.01），窗口在运行期创建 ——
// 这样既避开宪章 §10 记的「GUI 子系统与 6.01 冲突」，也保住 M5 那条
// 「产物子系统版本必须是 6.1」的闸门，--ui-smoke-test 的输出还能打到 stdout。
//
// 分层：本头文件与 ui_pages.cpp 只有纯数据（不 include 任何 ATL/WTL/windows.h），
// 所以能被单测直接覆盖；ui_frame.cpp 才是窗口层，负责把 Field 摆成原生控件、
// 把控件消息写回 Field，再由 applyTo() 落到 core/config 的模型上。
//
// 页面名、分组名、字段名一律沿用 DFM 里的控件名（如 Gb_Targetsys_Desktop、Cb_ShowDevmgmt），
// 这样「手工走查对照图纸」时能一对一核对；页签标题也与 DFM 的 Caption 逐字一致。
//
// 字段的取值来源分三类，都在 Field::note 里写明：
//   1. 图纸直接给出（Caption / Items.Strings 里有的）；
//   2. 我们自己的核心模型给出（如运行方式/运行等待/执行路径来自 core/runplan 的四个维度）；
//   3. 图纸与宪章都定不下来的字段一律不做，并登记到 excludedControlIds()，绝不猜。

#include <string>
#include <vector>

#include "core/config.hpp"

namespace osp::ui {

// ---------------------------------------------------------------- 页面模型

enum class FieldKind
{
    Info,     // 只读一行（static）
    Check,    // 复选框
    Edit,     // 单行编辑框
    Number,   // 数字编辑框（底层仍是 EDIT）
    Combo,    // 下拉框
    Radio,    // 单选（同组互斥）
    Button,   // 命令按钮
    Progress, // 进度条
    List,     // 列表视图
};

const char* fieldKindId(FieldKind kind);

struct Field
{
    // DFM 里的控件名，走查与验收都按这个名字核对
    std::string id;
    // 界面上的中文标签（DFM 的 Caption 去掉快捷键标记）
    std::string label;
    FieldKind kind = FieldKind::Info;

    // 文本值：Info 的显示值、Edit/Number 的内容
    std::string value;
    // 勾选状态（Check / Radio）
    bool checked = false;
    // 数字值：Progress 的百分比
    int progress = 0;

    // Combo 的条目：options 是界面文本，optionIds 是对应的存值（写回 config 用）
    std::vector<std::string> options;
    std::vector<std::string> optionIds;
    int comboIndex = -1;

    // List 的列名与行
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;

    // 按钮在窗口层触发的动作名（空 = 纯展示按钮）
    std::string action;

    // 落到 config 模型上的字段路径，如 "optimization.autoSetIp"；空表示只读或运行期字段
    std::string binding;
    // 取值依据：图纸 / 核心模型 / 宪章裁决
    std::string note;
};

struct Group
{
    std::string id;      // DFM 分组框名，如 "Gb_Targetsys_Desktop"
    std::string caption; // 分组标题，与图纸 Caption 一致
    std::vector<Field> fields;
};

struct Page
{
    std::string id;         // "target"
    std::string title;      // "目标系统"，与 DFM 的 TabSheet.Caption 逐字一致
    std::string dfmSheet;   // DFM 里的页对象名，如 "MlSkinTabSheet2"
    bool configPage = true; // 配置页 / 运行期页面（运行、完成为运行期页面）
    std::vector<Group> groups;
};

// 「母盘系统」页：字段来自 M2 的系统体检结果（M7a 已交付，此处并入统一模型）
Page systemPage();

// 按 DFM 的 11 个页签，交付其中 7 个；cfg 提供编辑初值
std::vector<Page> pagesFromConfig(const config::Config& cfg);

// 把页面上的编辑结果写回配置模型；Info / Progress / List 字段忽略
void applyTo(const Page& page, config::Config& cfg);

// 把全部配置页上的编辑合并成一份配置（逐页 applyTo）
config::Config configFromPages(const std::vector<Page>& pages);

// 配置文件的读写。失败时不抛异常，error 填原因；schemaVersion 不符或 JSON 不合法都算失败。
bool loadConfigFile(const std::string& path, config::Config& out, std::string& error);
bool saveConfigFile(const std::string& path, const config::Config& cfg, std::string& error);

// 计划任务页：把当前草稿行读成一条 runPlan 任务
config::RunPlanTask taskFromPage(const Page& tasksPage);
// 计划任务页：用一条任务刷新草稿行（列表选中项回填编辑器）
void fillTaskPage(Page& tasksPage, const config::RunPlanTask& task);

// 便捷查找
const Page* findPage(const std::vector<Page>& pages, const std::string& id);
Page* findPage(std::vector<Page>& pages, const std::string& id);
const Field* findField(const Page& page, const std::string& id);
Field* findField(Page& page, const std::string& id);
std::size_t fieldCount(const Page& page);
std::size_t editableFieldCount(const Page& page);

std::string toJson(const std::vector<Page>& pages);

// ---------------------------------------------------------------- 走查名单

// 某页按图纸应当出现的控件名（缺项 = 走查失败）
std::vector<std::string> expectedControlIds(const std::string& pageId);
// 全部页面的应出现控件名
std::vector<std::string> allExpectedControlIds();
// 明确不做的控件名：宪章 §7 红线 + 图纸/宪章都查不到语义的字段。
// 验收脚本按这份名单断言「界面上没有它们」。
std::vector<std::string> excludedControlIds();
// 界面上不得出现的 config 绑定（红线字段在我们模型里存在，但不许露到 UI）
std::vector<std::string> forbiddenBindings();

// 打开窗口并常驻，直到用户关闭；返回进程退出码。
// initialConfigPath 非空时启动就载入该 cfg.json；载入失败只报错、不退出。
int runApplication(const std::string& initialConfigPath);

// ---------------------------------------------------------------- 冒烟测试

struct PageSummary
{
    std::string id;
    std::string title;
    std::string dfmSheet;
    int fields = 0;   // 页面模型里的字段数
    int controls = 0; // 窗口层真的建出来的控件数
    bool opened = false;
};

struct SmokeResult
{
    bool ok = false;
    bool skipped = false; // 无头环境：显式跳过，绝不静默略过
    std::string skipReason;
    int checks = 0;
    int failures = 0;
    int pagesOpened = 0;            // 真正打开并驱动过的页面数
    int controlsDriven = 0;         // 用真实消息驱动过的控件数
    std::vector<std::string> lines; // 每条检查的可读记录
    std::vector<std::string> failuresDetail;
    // 走查用的清单：页面一览 + 明确不做的控件名（验收脚本按这两份核对）
    std::vector<PageSummary> pages;
    std::vector<std::string> excludedControls;
    std::vector<std::string> forbiddenBindings;
};

// 创建窗口、逐页打开、用真实消息驱动每个控件、销毁窗口。
// visible=true 时窗口可见（本地人工走查用）；默认不可见。
SmokeResult runSmokeTest(bool visible);

std::string smokeReport(const SmokeResult& result);
std::string smokeJson(const SmokeResult& result);

// 供验收脚本判断「无头跳过」是否被显式声明
inline constexpr const char* kSkipMarker = "SMOKE-SKIP";

// 供验收脚本核对页面数量（DFM 有 11 个页签，其中 4 个不是配置页）
inline constexpr int kConfigPageCount = 7;

} // namespace osp::ui

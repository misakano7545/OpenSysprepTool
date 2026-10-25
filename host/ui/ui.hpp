#pragma once

// 主程序 UI（宪章 §8 M7）
//
// 框架：原生 Win32 标准控件 + WTL 薄封装，视觉完全自研，不复刻 SC 的换肤观感。
// 子系统保持 CONSOLE（顶层 CMakeLists 的 /SUBSYSTEM:CONSOLE,6.01），窗口在运行期创建 ——
// 这样既避开宪章 §10 记的「GUI 子系统与 6.01 冲突」，也保住 M5 那条
// 「产物子系统版本必须是 6.1」的闸门，--ui-smoke-test 的输出还能打到 stdout。

#include <string>
#include <vector>

namespace osp::ui {

// ---------------------------------------------------------------- 页面模型
//
// 页面与字段是纯数据，不依赖窗口 —— 这样它们能被单元测试直接覆盖，
// 窗口那一层只负责把数据摆到原生控件上。

struct Field
{
    std::string id;    // 对应 DFM 里的控件名，便于与图纸核对
    std::string label; // 界面上的中文名
    std::string value;
};

struct Page
{
    std::string id;    // "system"
    std::string title; // "母盘系统"
    std::vector<Field> fields;
};

// 「母盘系统」页（DFM 里 MlSkinTabSheet1，标题 Caption = '母盘系统'）。
// 字段来自 M2 的系统体检结果，控件名与图纸逐一对齐。
Page systemPage();

// M7a 阶段实现的全部页面。框架支持多页，本里程碑只交付第一页。
std::vector<Page> allPages();

// 页面的字段数与按 DFM 控件名的映射是否完整 —— 供单测与验收脚本核对
std::vector<std::string> expectedControlIds();
std::string toJson(const std::vector<Page>& pages);

// ---------------------------------------------------------------- 冒烟测试

struct SmokeResult
{
    bool ok = false;
    bool skipped = false; // 无头环境：显式跳过，绝不静默略过
    std::string skipReason;
    int checks = 0;
    int failures = 0;
    std::vector<std::string> lines; // 每条检查的可读记录
    std::vector<std::string> failuresDetail;
};

// 创建窗口、逐页打开、用真实消息驱动控件、销毁窗口。
// visible=true 时窗口可见（本地人工走查用）；默认不可见。
SmokeResult runSmokeTest(bool visible);

std::string smokeReport(const SmokeResult& result);
std::string smokeJson(const SmokeResult& result);

// 供验收脚本判断「无头跳过」是否被显式声明
inline constexpr const char* kSkipMarker = "SMOKE-SKIP";

} // namespace osp::ui

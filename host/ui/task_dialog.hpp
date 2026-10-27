#pragma once

// SC 的「任务」对话框 TTLMF（740×400）
//
// DFM: TLmf
//   Lab_Tasktype(83,72) + Cb_Tasktype(150,67 106x26)     任务类型:
//   Lab_RunPlan(83,127) + Pl_RunPlan(150,111 515x37)     运行计划:
//       Btn_RunPlan1..5 (4/103/202/301/401, y=9, 93x27)  部署前/部署中/部署后/登录时/桌面后
//   Lab_RunMode(87,176) + Cb_RunMode(154,171 106x26)     运行方式:
//   Lab_RunWait(279,176) + Cb_RunWait(346,171 106x26)    运行等待:
//   Lab_WaitTime(470,176) + Sed_WaitTime(537,171 90x26) + Lab_WaitTime_S(626,176)  S
//   Lab_RunPath(87,224) + Cb_RunPath(154,219 106x26) + Ed_RunPath(266,219 297x26)
//       + Btn_Open(569,219 75x27)                        执行路径: / 浏览(&O)
//   Lab_Cmdline(87,272) + Ed_CmdLine(154,267 298x26)     执行参数:
//   Lab_RunAfter(470,272) + Cb_RunAfter(537,267 106x26)  善后处理:
//   Btn_Save(537,330 106x27)「添 加(&T)」
//   Lab_run(87,336 444x15)                               状态行（校验失败时显示原因）
//
// 图纸里还有 Ed_Temp1..4（18/130/245/357, y=366）与一个跑到 (5440,371) 的 Ed_temp，
// 那是 SC 自己存中间值用的隐藏编辑框，界面上看不见也没语义，不做。

#include <string>

#include "core/config.hpp"

namespace osp::ui {

// 对话框编辑出来的任务草稿（对应 config::RunPlanTask 的七个字段）
struct TaskDraft
{
    std::string phase;
    std::string type;
    std::string windowStyle;
    std::string waitMode;
    int waitSeconds = 0;
    std::string cleanup;
    std::string target;
    std::string arguments;
};

// 弹出任务对话框。draft 既是初值也是输出（用户点「添 加」后填充）。
// 返回 true = 用户确认添加；false = 取消或关窗口。
bool showTaskDialog(void* ownerHwnd, TaskDraft& draft);

// 冒烟测试用：不上屏，只验控件树、坐标与「空路径不允许添加」的校验。
bool smokeTaskDialog(void* ownerHwnd, std::string& detail);

// DFM 设计尺寸
constexpr int kTaskDialogWidth = 740;
constexpr int kTaskDialogHeight = 400;

} // namespace osp::ui

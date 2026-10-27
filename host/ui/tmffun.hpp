#pragma once

// TMFFUN —— SC 的「高级设置」总窗体（DFM: MFfun，740x400，18 个页签）。
//
// 为什么单独一个窗体：主窗体（800x540）放不下的细节都在这里。SC 的
// 「高级设置(&A)」按钮（Btn_Sysprepaset）和「OEM设置」按钮（Btn_OemSet）都开这个窗体。
//
// 本轮已排的页签（坐标逐字来自 docs/sc_layout/TMFFUN.json，控件表由
// tools/gen_tmffun.py 生成到 tmffun_table.inc）：
//   转移文档 / 转移收藏夹 / 转移虚拟内存
// 其余页签仍然列在页签条上、标成未实现并写明原因 —— SC 的用户会去找这些页，
// 从界面上悄悄抹掉比留着更糟。对照表见 docs/SC窗体对照.md §4.4。
//
// 每个页签底部有「保存(&S)」与「退出(&E)」两个按钮（SC 就是这么放的）：
// 保存只提交当前页签，退出关窗，未保存的页签丢弃。

#include <string>

#include "core/config.hpp"

namespace osp::ui {

// 设计尺寸，验收脚本据此比对
constexpr int kTmffunClientWidth = 740;
constexpr int kTmffunClientHeight = 400;

// 打开窗体。initialTab 是要直接选中的页签 id（空 = 第一页）。
// 返回 true 表示用户至少保存过一次；config 就地更新。
bool showTmffunDialog(void* ownerHwnd, config::Config& cfg, int initialTab);

// 冒烟测试用：不上屏，验尺寸、页签数、已排页签的控件摆放、保存回写与未实现页的说明。
bool smokeTmffunDialog(void* ownerHwnd, config::Config& cfg, std::string& detail);

// 页签在 kTabs 里的序号（按 DFM 顺序）；找不到返回 -1。
int tmffunTabIndex(const std::string& tabId);

} // namespace osp::ui

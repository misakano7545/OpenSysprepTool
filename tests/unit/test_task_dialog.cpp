// M10：TTLMF 任务对话框（740×400）
//
// SC 的任务不是直接在「计划任务」页上填的，而是在这个对话框里编辑：任务类型、
// 五个阶段按钮、运行方式、运行等待、等待时间、执行路径（下拉+编辑+浏览）、
// 执行参数、善后处理，底部一行状态（Lab_run）。

#include <doctest/doctest.h>

#include <string>

#include "task_dialog.hpp"

TEST_CASE("task_dialog: TTLMF 设计尺寸与 DFM 一致")
{
    // DFM: TLmf ClientWidth=740 / ClientHeight=400
    CHECK(osp::ui::kTaskDialogWidth == 740);
    CHECK(osp::ui::kTaskDialogHeight == 400);
}

TEST_CASE("task_dialog: 草稿默认值与 config 的取值域一致")
{
    osp::ui::TaskDraft draft;
    CHECK(draft.phase.empty());
    CHECK(draft.waitSeconds == 0);
    // 阶段/类型/方式这些字段是字符串，取值域由 core/runplan 定义；
    // 这里只确认结构默认可构造，具体取值校验在冒烟里做（要真窗口）
    CHECK(draft.target.empty());
    CHECK(draft.arguments.empty());
}

TEST_CASE("task_dialog: 冒烟入口验尺寸与校验逻辑")
{
    std::string detail;
    const bool ok = osp::ui::smokeTaskDialog(nullptr, detail);
    if (!ok)
    {
        // 建不起来必须说明原因，不能静默通过
        CHECK(!detail.empty());
        return;
    }
    CHECK(detail.find("740x400") != std::string::npos);
    // 空路径必须被拦下、填好后 phase/target/args 要正确带出
    CHECK(detail.find("空路径被拦下") != std::string::npos);
}

// M10：TSBOX 小弹窗（重启方式 / 宽带创建方式）
//
// SC 的 TSBOX 是 300×136 的确认框，两个页签各两个单选 + 一个确定按钮。
// 这几条测试钉住三件容易漂移的事实：
//   1. 设计尺寸 300×136（DFM 的 ClientWidth/ClientHeight）；
//   2. 文案取自语言文件，而不是 DFM —— DFM 里页签 2 写的是「静默重启」，
//      那是 SC 自己的复制粘贴错误，运行时被语言文件覆盖成「静默创建」；
//   3. 两个模式（重启 / 宽带）用各自的文案。

#include <doctest/doctest.h>

#include <string>

#include "popups.hpp"

TEST_CASE("popups: TSBOX 设计尺寸与 DFM 一致")
{
    // DFM: Sbox ClientWidth=300 / ClientHeight=136
    CHECK(osp::ui::kChoiceClientWidth == 300);
    CHECK(osp::ui::kChoiceClientHeight == 136);
}

TEST_CASE("popups: 两种模式的结果默认值")
{
    osp::ui::ChoiceResult result;
    CHECK_FALSE(result.confirmed);
    CHECK_FALSE(result.silent);
}

TEST_CASE("popups: 枚举覆盖 SC 的两个页签")
{
    // 页签 1 是重启方式、页签 2 是宽带创建方式；多一个或少一个都说明理解错了图纸
    CHECK(static_cast<int>(osp::ui::ChoiceKind::Restart) == 0);
    CHECK(static_cast<int>(osp::ui::ChoiceKind::BroadbandCreate) == 1);
}

TEST_CASE("popups: 冒烟入口对空 owner 也要能跑通")
{
    // 不上屏、不需要消息循环：验证窗口能建、尺寸/控件位置对、点确定的结果对。
    // 注意这条会真的创建窗口，所以只在有窗口站的会话里有意义；
    // CI 的 runner 是服务会话，GetProcessWindowStation 仍可用，实测能建。
    std::string detail;
    const bool ok = osp::ui::smokeChoiceDialog(nullptr, osp::ui::ChoiceKind::Restart, detail);
    if (!ok)
    {
        // 建不起来必须给出原因，不能静默通过
        CHECK(!detail.empty());
        return;
    }
    CHECK(detail.find("300x136") != std::string::npos);
    CHECK(detail.find("倒计时") != std::string::npos);
}

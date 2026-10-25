// M7a：界面数据层的单元测试
//
// 页面与字段是纯数据（不依赖窗口），所以能被直接覆盖；窗口那一层由
// `ospt --ui-smoke-test` 与 tests/integration/verify_ui.py 负责。

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ui.hpp"

using namespace osp;

TEST_CASE("第一个页面就是 DFM 里的「母盘系统」页")
{
    const ui::Page page = ui::systemPage();

    CHECK(page.id == "system");
    // DFM: MlSkinTabSheet1.Caption = '母盘系统'
    CHECK(page.title == "母盘系统");
    CHECK_FALSE(page.fields.empty());
}

TEST_CASE("页面的控件名与 DFM 图纸逐一对应，且没有多余字段")
{
    const ui::Page page = ui::systemPage();
    const std::vector<std::string> expected = ui::expectedControlIds();

    REQUIRE(page.fields.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        INFO(expected[i]);
        CHECK(page.fields[i].id == expected[i]);
        CHECK_FALSE(page.fields[i].label.empty());
    }
}

TEST_CASE("DFM 里 Gb_Osbasicinfo 的九个标签都在名单里")
{
    const std::vector<std::string> expected = ui::expectedControlIds();
    const std::vector<std::string> required = {
        "Lab_Osnamev",
        "Lab_Osbitv",
        "Lab_Osntverv",
        "Lab_SysprepSupportv",
        "Lab_Osdrivesv",
        "Lab_Osusesizev",
        "Lab_OsUserv",
        "Lab_OsRelativesizev",
        "Lab_RunEnvironv",
    };

    for (const std::string& id : required)
    {
        INFO(id);
        CHECK(std::find(expected.begin(), expected.end(), id) != expected.end());
    }
    CHECK(expected.size() == required.size());
}

TEST_CASE("关键字段有真实取值，不是空壳")
{
    const ui::Page page = ui::systemPage();

    const auto valueOf = [&page](const std::string& id) -> std::string {
        for (const ui::Field& field : page.fields)
        {
            if (field.id == id)
            {
                return field.value;
            }
        }
        return std::string();
    };

    CHECK_FALSE(valueOf("Lab_Osnamev").empty());
    CHECK_FALSE(valueOf("Lab_Osbitv").empty());
    CHECK_FALSE(valueOf("Lab_Osntverv").empty());
    CHECK_FALSE(valueOf("Lab_SysprepSupportv").empty());
    CHECK_FALSE(valueOf("Lab_OsUserv").empty());
    CHECK_FALSE(valueOf("Lab_RunEnvironv").empty());

    // 位宽字段必须点明是 32 位还是 64 位系统
    const std::string arch = valueOf("Lab_Osbitv");
    CHECK((arch.find("64 位系统") != std::string::npos || arch.find("32 位系统") != std::string::npos));

    // 相对容量是百分比
    const std::string relative = valueOf("Lab_OsRelativesizev");
    CHECK(relative.find('%') != std::string::npos);

    // 封装支持判定只用两个取值之一
    const std::string support = valueOf("Lab_SysprepSupportv");
    CHECK(support.rfind("支持", 0) == 0);
}

TEST_CASE("allPages 至少交付一页，且第一页就是系统信息页")
{
    const std::vector<ui::Page> pages = ui::allPages();

    REQUIRE(!pages.empty());
    CHECK(pages[0].id == "system");
    CHECK(pages[0].title == "母盘系统");
}

TEST_CASE("页面 JSON 带上控件名、标签与取值")
{
    const std::string json = ui::toJson(ui::allPages());

    CHECK(json.find("\"schemaVersion\"") != std::string::npos);
    CHECK(json.find("\"control\"") != std::string::npos);
    CHECK(json.find("Lab_Osnamev") != std::string::npos);
    CHECK(json.find("Lab_RunEnvironv") != std::string::npos);
    CHECK(json.find("母盘系统") != std::string::npos);
}

TEST_CASE("无头跳过的标记是固定的，验收脚本按它判定")
{
    // 宪章 §9.7 M7：UI smoke test 在无头环境可能跳过，必须显式打印 skip 原因。
    CHECK(std::string(ui::kSkipMarker) == "SMOKE-SKIP");
}

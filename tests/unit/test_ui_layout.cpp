// M10：运行页 / 完成页必须与 SC 的 DFM 逐控件对齐
//
// 这两页在 SC 里是固定版面（居中大图 + 居中说明 + 底部双进度条），之前用「分组框 +
// 一行一个字段」的通用排列去渲染，形状完全不对 —— 用户一眼就看出来了。
//
// 现在拿 docs/sc_layout/TSCMF.json（由 tools/dfm_layout.py 从 SC 的 DFM 直接生成）
// 做基准，逐项比对：控件名、顺序、坐标、尺寸、Caption。
// 这份 JSON 是「界面事实」的转写，不含任何 Delphi 代码，也不含图片二进制。

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ui.hpp"

namespace {

using json = nlohmann::json;

std::filesystem::path layoutFixture()
{
    // 测试的工作目录是 build/tests/unit，仓库根在其上三级
    const std::filesystem::path candidates[] = {
        std::filesystem::current_path() / "docs" / "sc_layout" / "TSCMF.json",
        std::filesystem::current_path() / ".." / ".." / ".." / "docs" / "sc_layout" / "TSCMF.json",
        std::filesystem::current_path() / ".." / ".." / "docs" / "sc_layout" / "TSCMF.json",
    };
    for (const auto& candidate : candidates)
    {
        if (std::filesystem::exists(candidate))
        {
            return candidate;
        }
    }
    return std::filesystem::path();
}

// 取某页签的子控件（只到二级：Pan_* 的直接子控件就是页面上的控件）
std::vector<json> sheetChildren(const json& controls, const std::string& sheet)
{
    std::vector<json> direct;
    std::vector<json> nested;
    const std::string prefix = "Scmf/Page_mf/" + sheet + "/";
    for (const auto& row : controls)
    {
        const std::string path = row.value("path", std::string());
        if (path.rfind(prefix, 0) != 0)
        {
            continue;
        }
        const std::string rest = path.substr(prefix.size());
        if (rest.find('/') == std::string::npos)
        {
            direct.push_back(row); // Pan_Work / Pan_End
        }
        else if (rest.find('/', rest.find('/') + 1) == std::string::npos)
        {
            nested.push_back(row); // Pan_* 的直接子控件
        }
    }
    (void)direct;
    return nested;
}

} // namespace

TEST_CASE("ui: 运行页与 DFM 的控件集合、顺序、坐标一致")
{
    const std::filesystem::path fixture = layoutFixture();
    REQUIRE_MESSAGE(!fixture.empty(), "找不到 docs/sc_layout/TSCMF.json（先用 tools/dfm_layout.py 生成）");

    std::ifstream in(fixture, std::ios::binary);
    REQUIRE(in);
    const json root = json::parse(in);
    const std::vector<json> dfm = sheetChildren(root["controls"], "MlSkinTabSheet6");

    // DFM 有 5 个控件（Pic_RunGif / Pro_Total / Pro_Task / Lab_Run / Lab_Total）
    REQUIRE(dfm.size() == 5);

    const osp::ui::Page page = osp::ui::runPage();
    CHECK(page.dfmLayout);
    CHECK(page.title == "运行");
    CHECK(page.dfmSheet == "MlSkinTabSheet6");

    // 数量与顺序必须与 DFM 相同
    CHECK(fieldCount(page) == dfm.size());
    std::size_t index = 0;
    for (const auto& group : page.groups)
    {
        for (const auto& field : group.fields)
        {
            if (index >= dfm.size())
            {
                break;
            }
            const json& expected = dfm[index];
            CHECK(field.id == expected.value("name", std::string()));
            CHECK(field.x == expected["left"].get<int>());
            CHECK(field.y == expected["top"].get<int>());
            CHECK(field.width == expected["width"].get<int>());
            CHECK(field.height == expected["height"].get<int>());
            ++index;
        }
    }
}

TEST_CASE("ui: 完成页与 DFM 一致，只有 Btn_Bugs 是有意缺项")
{
    const std::filesystem::path fixture = layoutFixture();
    REQUIRE(!fixture.empty());

    std::ifstream in(fixture, std::ios::binary);
    REQUIRE(in);
    const json root = json::parse(in);
    std::vector<json> dfm = sheetChildren(root["controls"], "MlSkinTabSheet7");
    REQUIRE(dfm.size() == 9);

    const osp::ui::Page page = osp::ui::donePage();
    CHECK(page.dfmLayout);
    CHECK(page.title == "完成");

    // Btn_Bugs 要连 SC 的服务器发错误报告，宪章 §7 不做；其余 8 个必须一个不少
    CHECK(fieldCount(page) == dfm.size() - 1);
    CHECK(findField(page, "Btn_Bugs") == nullptr);

    // 先收集缺项，一次性报出来（逐个 REQUIRE 只能看到第一个）
    std::vector<std::string> missing;
    for (const auto& expected : dfm)
    {
        const std::string name = expected.value("name", std::string());
        if (name == "Btn_Bugs")
        {
            continue;
        }
        if (findField(page, name) == nullptr)
        {
            missing.push_back(name);
        }
    }
    INFO("缺项: ", [&missing]() {
        std::string joined;
        for (const auto& name : missing)
        {
            joined += name + " ";
        }
        return joined;
    }());
    REQUIRE(missing.empty());

    for (const auto& expected : dfm)
    {
        const std::string name = expected.value("name", std::string());
        if (name == "Btn_Bugs")
        {
            continue;
        }
        const osp::ui::Field* field = findField(page, name);
        REQUIRE(field != nullptr);
        CHECK(field->x == expected["left"].get<int>());
        CHECK(field->y == expected["top"].get<int>());
        CHECK(field->width == expected["width"].get<int>());
        CHECK(field->height == expected["height"].get<int>());

        // Caption 逐字比对（含「关机(&T)」的快捷键标记与中文引号）
        const std::string caption = expected.value("caption", std::string());
        const std::string text = field->kind == osp::ui::FieldKind::Info ? field->value : field->label;
        if (!caption.empty())
        {
            CHECK(text == caption);
        }
    }
}

TEST_CASE("ui: 运行期页面标记为非配置页，且带 DFM 设计尺寸")
{
    for (const auto& page : {osp::ui::runPage(), osp::ui::donePage()})
    {
        CHECK_FALSE(page.configPage);
        CHECK(page.dfmLayout);
        // 绝对布局按设计宽度等比缩放，宽度为 0 会退化成自动排列
        CHECK(page.clientWidth == 800);
        CHECK(page.clientHeight == 398);
        for (const auto& group : page.groups)
        {
            // 图纸这里是 Pan_* 无边框面板，不能画成 GroupBox
            CHECK(group.caption.empty());
            for (const auto& field : group.fields)
            {
                CHECK(field.placed());
            }
        }
    }
}

TEST_CASE("ui: 图片控件只占位不自带素材")
{
    const osp::ui::Page run = osp::ui::runPage();
    const osp::ui::Field* spin = findField(run, "Pic_RunGif");
    REQUIRE(spin != nullptr);
    CHECK(spin->kind == osp::ui::FieldKind::Image);
    CHECK(spin->dfmClass == "TImage");

    const osp::ui::Page done = osp::ui::donePage();
    // 成功图与失败图在图纸上同矩形，互斥显示
    const osp::ui::Field* success = findField(done, "PIC_Success");
    const osp::ui::Field* error = findField(done, "PIC_Error");
    REQUIRE(success != nullptr);
    REQUIRE(error != nullptr);
    CHECK(success->x == error->x);
    CHECK(success->y == error->y);
    CHECK(success->width == error->width);
    CHECK(success->height == error->height);
}

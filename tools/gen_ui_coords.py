#!/usr/bin/env python3
"""gen_ui_coords.py —— 从 DFM 布局表生成「控件坐标表」C++ 片段

用法：
    python tools/gen_ui_coords.py --layout docs/sc_layout/TSCMF.json \
        --out host/ui/ui_dfm_coords.inc

为什么是坐标表而不是整页代码：页面里的下拉条目、列表行、动态取值（磁盘容量等）
都带有业务逻辑，整页重新生成会把它们冲掉。这里只把**位置**这一件事交给 DFM，
逻辑保持原样 —— 生成的表由 applyDfmCoords() 在页面构造末尾套上去。

表里只收「我方模型里真实存在的控件」：DFM 有而模型没有的（纯标签、图片占位、
内部滚动条）不进表，避免生成永远用不到的坐标；缺了哪些在生成的尾部注释里列出。
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

# DFM 页签 -> 我方页面 id
# 只覆盖「模型里已有字段、但版面还是自动排列」的四页。
# 目标系统页走 tools/gen_ui_page.py 的整页生成物；运行/完成页的坐标直接写在
# ui_pages.cpp 里（它们没有分组框，逐字给更直观）。
SHEETS = {
    "MlSkinTabSheet1": "system",
    "MlSkinTabSheet3": "tasks",
    "MlSkinTabSheet4": "deploy",
    "MlSkinTabSheet5": "sysprep",
}

# 分组框名 -> 我方 Group.id（名字本来就一致，留这个表是为了显式化）
def emit(layout: dict, out_path: str, model_ids: dict[str, set[str]]) -> int:
    rows = layout["controls"]
    form = layout["form"]
    lines = [
        "// 本文件由 tools/gen_ui_coords.py 生成，勿手改。",
        f"// 依据：docs/sc_layout/{os.path.basename(out_path).replace('ui_dfm_coords.inc','TSCMF.json')}"
        "（DFM 原样坐标，800x398 设计空间）",
        "//",
        "// 每条对应「页面 / 分组框 / 控件」的矩形。分组框用绝对坐标，",
        "// 组内控件用相对分组框的坐标 —— 与 DFM 的语义一致。",
        "",
        "struct DfmCoord",
        "{",
        "    const char* page;",
        "    const char* group;   // 空 = 页面直属控件",
        "    const char* field;   // 空 = 这一条描述分组框本身",
        "    int x;",
        "    int y;",
        "    int width;",
        "    int height;",
        "    bool visible;        // DFM 的 Visible；false = 点按钮才显示的面板",
        "};",
        "",
        "inline constexpr DfmCoord kDfmCoords[] = {",
    ]

    mapped = 0
    skipped: dict[str, list[str]] = {}

    for sheet, page in SHEETS.items():
        prefix = f"{form}/Page_mf/{sheet}/"
        page_rows = {r["name"]: r for r in rows if r.get("path", "").startswith(prefix)}
        # 哪个控件属于哪个分组框：沿父链找最近的 GroupBox 祖先（面板不参与，
        # 但坐标是相对**直接父容器**的，所以要把中间的 Panel 偏移累加进来）。
        parents: dict[str, list[str]] = {}
        for row in rows:
            path = row.get("path", "")
            if not path.startswith(prefix):
                continue
            chain = path[len(prefix):].split("/")[:-1]
            parents[row["name"]] = chain

        groups = {name: row for name, row in page_rows.items() if not parents[name]}
        for name, chain in parents.items():
            if not chain:
                continue
            if name not in model_ids.get(page, set()):
                skipped.setdefault(page, []).append(name)
                continue
            # 累加中间容器的偏移（DFM 里子控件坐标相对父容器）
            x = 0
            y = 0
            offset_ok = True
            for parent in chain:
                entry = page_rows.get(parent)
                if entry is None or entry.get("left") is None or entry.get("top") is None:
                    offset_ok = False
                    break
                x += entry["left"]
                y += entry["top"]
            row = page_rows[name]
            if not offset_ok or row.get("left") is None or row.get("top") is None:
                continue
            # 归属到最近的 GroupBox 祖先；没有就挂在页面级（group 写空）
            owner = ""
            for parent in reversed(chain):
                if page_rows.get(parent, {}).get("class") in ("TMlSkinGroupBox",):
                    owner = parent
                    break
            group_offset_x = 0
            group_offset_y = 0
            if owner:
                entry = page_rows[owner]
                group_offset_x = entry.get("left") or 0
                group_offset_y = entry.get("top") or 0
            # x/y 只累加了父容器，控件自身的 Left/Top 必须加上 ——
            # 早先漏了这一步，组框内的控件全被摆到 (0,0)，整页堆成一坨。
            own_x = row["left"]
            own_y = row["top"]
            lines.append(
                f'    {{"{page}", "{owner}", "{name}", '
                f'{x + own_x - group_offset_x}, {y + own_y - group_offset_y}, '
                f'{row.get("width")}, {row.get("height")}, true}},'
            )
            mapped += 1

        # 分组框本身最后写，方便阅读时按页面聚合
        for name, group in groups.items():
            left, top = group.get("left"), group.get("top")
            if left is None or top is None:
                continue
            visible = "false" if group.get("visible") is False else "true"
            lines.append(
                f'    {{"{page}", "{name}", "", {left}, {top}, '
                f'{group.get("width")}, {group.get("height")}, {visible}}},'
            )

    lines.append("};")
    lines.append("")
    for page, names in skipped.items():
        lines.append(f"// {page} 页 DFM 里另有以下控件未进坐标表（模型里没有对应字段）：")
        lines.append("//   " + "、".join(names))
    lines.append("")

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))
    print(f"RESULT: OK（{mapped} 条控件坐标 + 分组框矩形 -> {out_path}）")
    return 0


def collect_model_ids(source_path: str) -> dict[str, set[str]]:
    """从 ui_pages.cpp 里扫出每页实际拥有的控件 id

    按 `page.id = "xxx";` 切段：页面里的控件都在这一句之后、下一句之前。
    早期版本按 `Page fnName(` 切段，遇到 namespace、前置声明、生成物混排就会串页，
    实测把 14 个字段的母盘系统页扫成了 0 个。
    """
    with open(source_path, encoding="utf-8") as handle:
        text = handle.read()

    marks = [(m.start(), m.group(1)) for m in re.finditer(r'page\.id = "([a-z]+)";', text)]
    result: dict[str, set[str]] = {}
    for index, (start, page) in enumerate(marks):
        end = marks[index + 1][0] if index + 1 < len(marks) else len(text)
        chunk = text[start:end]
        ids = set(re.findall(r'make[A-Za-z]+\(\s*"([A-Za-z_0-9]+)"', chunk))
        ids |= set(re.findall(r'\w+\.id = "([A-Za-z_0-9]+)";', chunk))
        ids.discard(page)
        result.setdefault(page, set()).update(ids)
    return result


def main(argv: list[str]) -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[union-attr]

    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--source", default="host/ui/ui_pages.cpp")
    args = parser.parse_args(argv[1:])

    with open(args.layout, encoding="utf-8") as handle:
        layout = json.load(handle)
    model = collect_model_ids(args.source)
    return emit(layout, args.out, model)


if __name__ == "__main__":
    sys.exit(main(sys.argv))

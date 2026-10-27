#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 TMFFUN 的页签与控件转成 C++ 表（host/ui/tmffun_table.inc）。

为什么用生成器而不是手抄：主窗体那边手抄过 4 处错（分辨率分隔符、坐标写反等），
这里的控件量更大（18 页签约 370 个），手抄必然出错。坐标只从
docs/sc_layout/TMFFUN.json 来，绑定关系只从 tools/ui_bindings/tmffun.json 来。

用法：python tools/gen_tmffun.py
"""

import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
LAYOUT = ROOT / "docs" / "sc_layout" / "TMFFUN.json"
BINDINGS = ROOT / "tools" / "ui_bindings" / "tmffun.json"
OUTPUT = ROOT / "host" / "ui" / "tmffun_table.inc"

CPP_ESCAPES = {
    "\\": "\\\\",
    '"': '\\"',
    "\n": "\\n",
    "\r": "\\r",
    "\t": "\\t",
}


def cstr(text):
    """C++ 窄字符串字面量。源码是 UTF-8，编译器按 /utf-8 解释。"""
    out = []
    for ch in text:
        out.append(CPP_ESCAPES.get(ch, ch))
    return '"' + "".join(out) + '"'


def tab_controls(controls, sheet):
    """取某个页签下的控件。路径形如 MFfun/Mfpage/<Sheet>/<控件>：
    len 4 = 页签的直接子控件；len 5 = 组框里的控件。

    返回 (顶部控件, 组框子控件字典)。组框的坐标是绝对的，里面控件的坐标相对组框 ——
    与主窗体那边两级坐标语义一致。
    """
    top = []
    children = {}
    for row in controls:
        parts = row["path"].split("/")
        if len(parts) < 3 or parts[1] != "Mfpage" or parts[2] != sheet:
            continue
        if len(parts) == 4:
            top.append(row)
        elif len(parts) == 5:
            children.setdefault(parts[3], []).append(row)
    return top, children


def main():
    layout = json.loads(LAYOUT.read_text(encoding="utf-8"))
    bindings = json.loads(BINDINGS.read_text(encoding="utf-8"))
    controls = layout["controls"]
    visible_tabs = [c for c in controls if c["class"] == "TMlSkinTabSheet"]

    lines = []
    lines.append("// 本文件由 tools/gen_tmffun.py 生成，不要手改。")
    lines.append("// 坐标来自 docs/sc_layout/TMFFUN.json（DFM 逐字），")
    lines.append("// 绑定与文案来自 tools/ui_bindings/tmffun.json。")
    lines.append("")
    lines.append("namespace {")
    lines.append("")

    tab_entries = []
    excluded = bindings.get("excludeTabs", {})
    for tab in visible_tabs:
        sheet = tab["name"]
        if sheet in excluded:
            # 宪章 §7 红线页：连页签都不出现，不给这个功能留任何入口
            continue
        spec = bindings["tabs"].get(sheet)
        caption = tab.get("caption") or ""
        if spec is None:
            # 图纸里有、我们没安排实现的页签：仍然列出来，标成未实现，
            # 而不是从界面上悄悄消失（SC 的用户会去找它）。
            tab_entries.append((sheet, caption, spec, [], {}))
            continue
        top, children = tab_controls(controls, sheet)
        tab_entries.append((sheet, caption, spec, top, children))

    # 控件表
    for sheet, caption, spec, top, children in tab_entries:
        if spec is None:
            continue
        ident = "kControls" + sheet.replace("MlSkinTabSheet", "Sheet")
        rows = []
        for row in top:
            name = row["name"]
            if row["class"] in ("TMlSkinTabSheet",):
                continue
            bound = spec["controls"].get(name)
            if bound is None:
                continue
            rows.append((row, bound, None))
            for child in children.get(name, []):
                child_bound = spec["controls"].get(child["name"])
                if child_bound is None:
                    continue
                rows.append((child, child_bound, name))
        # 先把条目数组输出（ControlSpec 要引用它们）
        for row, bound, parent in rows:
            items = bound.get("items")
            if not items:
                continue
            lines.append(f"const char* const kItems_{row['name']}[] = {{")
            for item in items:
                lines.append(f"    {cstr(item)},")
            lines.append("};")
        lines.append(f"const ControlSpec {ident}[] = {{")
        for row, bound, parent in rows:
            x = row.get("left")
            y = row.get("top")
            w = row.get("width")
            h = row.get("height")
            if x is None:
                # 图纸里没给坐标的容器（页签本身等），跳过
                continue
            kind = bound["kind"]
            text = bound.get("text", row.get("caption") or "")
            binding = bound.get("binding", "")
            items = bound.get("items")
            items_lit = "nullptr"
            item_count = 0
            if items:
                items_lit = "kItems_" + row["name"]
                item_count = len(items)
            parent_lit = cstr(parent) if parent else "nullptr"
            group = bound.get("group", "")
            value = bound.get("value", "")
            action = bound.get("action", "")
            role = bound.get("role", "")
            lines.append(
                f"    {{{cstr(row['name'])}, {cstr(kind)}, {cstr(text)}, "
                f"{x}, {y}, {w}, {h}, {cstr(binding)}, {cstr(group)}, {cstr(value)}, "
                f"{cstr(action)}, {cstr(role)}, {items_lit}, {item_count}, {parent_lit}}},"
            )
        lines.append("};")
        lines.append(f"const int kControlCount{ident[len('kControls'):]} = "
                     f"static_cast<int>(sizeof({ident}) / sizeof({ident}[0]));")
        lines.append("")

    # 页签表（放在控件表之后，C++ 里先声明后使用）
    for sheet, caption, spec, top, children in tab_entries:
        ident = "kTab" + sheet.replace("MlSkinTabSheet", "Sheet")
        lines.append(f"const TabSpec {ident} = {{")
        lines.append(f"    {cstr(sheet)},")
        lines.append(f"    {cstr(caption)},")
        if spec is None:
            lines.append(f"    {cstr(bindings.get('unimplemented', {}).get(sheet, '未实现'))},")
            lines.append("    nullptr,")
            lines.append("    0,")
            lines.append("};")
            lines.append("")
            continue
        note = spec.get("note", "")
        lines.append(f"    {cstr(note)},")
        lines.append(f"    kControls{ident[4:]},")
        lines.append(f"    kControlCount{ident[4:]},")
        lines.append("};")
        lines.append("")

    lines.append("const TabSpec* const kTabs[] = {")
    for sheet, caption, spec, top, children in tab_entries:
        ident = "kTab" + sheet.replace("MlSkinTabSheet", "Sheet")
        lines.append(f"    &{ident},")
    lines.append("};")
    lines.append("const int kTabCount = static_cast<int>(sizeof(kTabs) / sizeof(kTabs[0]));")
    lines.append("")
    lines.append("} // namespace")
    lines.append("")

    OUTPUT.write_text("\n".join(lines), encoding="utf-8")

    implemented = sum(1 for _, _, s, _, _ in tab_entries if s is not None)
    total_controls = sum(len(top) + sum(len(v) for v in children.values())
                         for _, _, s, top, children in tab_entries if s is not None)
    print(f"生成 {OUTPUT.relative_to(ROOT)}：{len(tab_entries)} 个页签"
          f"（{implemented} 个已排、{len(tab_entries) - implemented} 个标未实现），"
          f"{total_controls} 个图纸控件")
    return 0


if __name__ == "__main__":
    sys.exit(main())

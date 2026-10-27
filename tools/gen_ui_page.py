#!/usr/bin/env python3
"""gen_ui_page.py —— 从 DFM 布局表生成「按图纸摆放」的页面构造函数

用法：
    python tools/gen_ui_page.py --layout docs/sc_layout/TSCMF.json \
        --binding tools/ui_bindings/target.json --out host/ui/ui_pages_dfm.inc

为什么要生成而不是手抄：302 个控件的坐标手抄必然出错，而且 DFM 一更新就对不上。
约定是「坐标全部来自 DFM，人工只维护控件名 -> 配置项/取值」这一层映射
（tools/ui_bindings/<page>.json）。生成物是 C++ 源码、随仓库提交，
构建时不依赖 SC 的反编译产物。

映射表里每条控件声明：
    {"kind": "edit|check|number|combo|info|button|progress|image",
     "label": "左边说明文字（kind=check/edit 时给）",
     "binding": "config 字段路径",
     "value": "info 的固定文本",
     "options": ["..."], "optionIds": ["..."], "select": "cfg.xxx 表达式",
     "action": "按钮动作名", "note": "取值依据"}

被排除的控件（宪章 §7 红线）在 "exclude" 里列名字，生成时整组跳过，
并在注释里写明为什么不做。
"""

from __future__ import annotations

import argparse
import json
import os
import sys


def cxx_string(text: str) -> str:
    escaped = text.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def collect_children(layout: dict, sheet: str) -> tuple[list[dict], list[list[dict]]]:
    """返回 (分组框列表, 每个分组框的子控件)。

    分组框=页签的直接子控件（TMlSkinGroupBox / TMlSkinPanel）；
    子控件=分组框的直接子控件。DFM 只到这一层，更深的嵌套本工程没有。
    """
    rows = layout["controls"]
    prefix = f"{layout['form']}/Page_mf/{sheet}/"
    groups: list[dict] = []
    children: dict[str, list[dict]] = {}

    for row in rows:
        path = row.get("path", "")
        if not path.startswith(prefix):
            continue
        rest = path[len(prefix):]
        depth = rest.count("/")
        if depth == 0:
            groups.append(row)
            children[row["name"]] = []
        elif depth == 1:
            children.setdefault(rest.split("/")[0], []).append(row)
    return groups, [children[group["name"]] for group in groups]


def emit_field(field: dict, spec: dict, indent: str) -> list[str]:
    """一个控件 -> 若干行 C++"""
    name = field["name"]
    cls = field.get("class", "")
    kind = spec.get("kind", "info")
    x, y = field.get("left"), field.get("top")
    w, h = field.get("width"), field.get("height")
    note = spec.get("note", "")

    lines: list[str] = []
    lines.append(f"{indent}{{")
    lines.append(f"{indent}    Field field;")
    lines.append(f"{indent}    field.id = {cxx_string(name)};")
    lines.append(f"{indent}    field.dfmClass = {cxx_string(cls)};")

    if kind == "info":
        lines.append(f"{indent}    field.kind = FieldKind::Info;")
        if "valueSource" in spec:
            # 动态值（系统信息、磁盘容量…）：表达式原样放进生成代码
            lines.append(f"{indent}    field.value = {spec['valueSource']};")
        else:
            value = spec.get("value", field.get("caption", ""))
            lines.append(f"{indent}    field.value = {cxx_string(value)};")
    elif kind == "button":
        label = spec.get("label", field.get("caption", ""))
        lines.append(f"{indent}    field.kind = FieldKind::Button;")
        lines.append(f"{indent}    field.label = {cxx_string(label)};")
        action = spec.get("action")
        if action:
            lines.append(f"{indent}    field.action = {cxx_string(action)};")
    elif kind == "edit":
        lines.append(f"{indent}    field.kind = FieldKind::Edit;")
        lines.append(f"{indent}    field.label = {cxx_string(spec.get('label', ''))};")
        binding = spec.get("binding", "")
        lines.append(f"{indent}    field.binding = {cxx_string(binding)};")
        lines.append(f"{indent}    field.value = {spec.get('source', '')};")
    elif kind == "number":
        lines.append(f"{indent}    field.kind = FieldKind::Number;")
        lines.append(f"{indent}    field.label = {cxx_string(spec.get('label', ''))};")
        binding = spec.get("binding", "")
        lines.append(f"{indent}    field.binding = {cxx_string(binding)};")
        lines.append(f"{indent}    field.value = std::to_string({spec.get('source', '0')});")
    elif kind == "check":
        lines.append(f"{indent}    field.kind = FieldKind::Check;")
        lines.append(f"{indent}    field.label = {cxx_string(spec.get('label', ''))};")
        binding = spec.get("binding", "")
        lines.append(f"{indent}    field.binding = {cxx_string(binding)};")
        lines.append(f"{indent}    field.checked = {spec.get('source', 'false')};")
    elif kind == "combo":
        lines.append(f"{indent}    field.kind = FieldKind::Combo;")
        lines.append(f"{indent}    field.label = {cxx_string(spec.get('label', ''))};")
        binding = spec.get("binding", "")
        lines.append(f"{indent}    field.binding = {cxx_string(binding)};")
        # 「条目就在 DFM 里的下拉」直接从 TSCMF.json 取，避免手抄漂移
        if spec.get("fromDfm"):
            items = field.get("items") or []
            if not items:
                raise SystemExit(f"控件 {name}: 声明了 fromDfm，但 DFM 里没有 Items.Strings")
            options = ", ".join(cxx_string(value) for value in items)
            option_ids = options
        else:
            options = ", ".join(cxx_string(value) for value in spec.get("options", []))
            option_ids = ", ".join(cxx_string(value) for value in spec.get("optionIds", []))
        lines.append(f"{indent}    field.options = {{{options}}};")
        lines.append(f"{indent}    field.optionIds = {{{option_ids}}};")
        lines.append(f"{indent}    field.comboIndex = {spec.get('select', '0')};")
    elif kind == "progress":
        lines.append(f"{indent}    field.kind = FieldKind::Progress;")
        lines.append(f"{indent}    field.label = {cxx_string(spec.get('label', ''))};")
        lines.append(f"{indent}    field.progress = {spec.get('progress', '0')};")
    elif kind == "image":
        lines.append(f"{indent}    field.kind = FieldKind::Image;")
        lines.append(f"{indent}    field.label = {cxx_string(spec.get('label', ''))};")
    else:
        raise SystemExit(f"控件 {name}: 未知 kind {kind}")

    lines.append(f"{indent}    field.x = {x};")
    lines.append(f"{indent}    field.y = {y};")
    lines.append(f"{indent}    field.width = {w};")
    lines.append(f"{indent}    field.height = {h};")
    if spec.get("visible") is False:
        lines.append(f"{indent}    field.visible = false;")
    if spec.get("align"):
        lines.append(f"{indent}    field.align = {spec['align']};")
    if note:
        lines.append(f"{indent}    field.note = {cxx_string(note)};")
    lines.append(f"{indent}    group.fields.push_back(std::move(field));")
    lines.append(f"{indent}}}")
    return lines


def main(argv: list[str]) -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[union-attr]

    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", required=True)
    parser.add_argument("--binding", required=True, help="控件映射表 JSON")
    parser.add_argument("--out", required=True)
    args = parser.parse_args(argv[1:])

    with open(args.layout, encoding="utf-8") as handle:
        layout = json.load(handle)
    with open(args.binding, encoding="utf-8") as handle:
        binding = json.load(handle)

    sheet = binding["sheet"]
    groups, children = collect_children(layout, sheet)
    specs = binding["controls"]
    excluded = set(binding.get("exclude", {}).keys())
    excluded_groups = {name for name, why in binding.get("exclude", {}).items() if why.get("kind") == "group"}

    out: list[str] = []
    out.append("// 本文件由 tools/gen_ui_page.py 生成，勿手改。")
    out.append(f"// 依据：docs/sc_layout/{os.path.basename(args.layout)} 的 {sheet}（DFM 原样坐标）")
    out.append(f"// 映射表：{args.binding}")
    out.append("")

    for group, items in zip(groups, children):
        gname = group["name"]
        if gname in excluded_groups:
            out.append(f"// 跳过整组 {gname}：{binding['exclude'][gname].get('why', '')}")
            continue
        caption = group.get("caption", "")
        out.append("    {")
        out.append(f"        Group group;")
        out.append(f"        group.id = {cxx_string(gname)};")
        out.append(f"        group.caption = {cxx_string(caption)};")
        out.append(f"        group.x = {group.get('left')};")
        out.append(f"        group.y = {group.get('top')};")
        out.append(f"        group.width = {group.get('width')};")
        out.append(f"        group.height = {group.get('height')};")
        if group.get("visible") is False:
            out.append("        group.visible = false; // DFM: Visible = False（点按钮才显示）")
        out.append("")
        for item in items:
            if item["name"] in excluded:
                why = binding["exclude"][item["name"]].get("why", "")
                out.append(f"        // 跳过 {item['name']}：{why}")
                continue
            if item["name"] not in specs:
                # 图纸里的纯文本标签（TMlSkinLabel / TLabel，带 Caption）自动补成只读文本，
                # 不需要人工声明：它们的文字就在 DFM 里，漏掉反而会让界面少一行说明。
                if item.get("class") in ("TMlSkinLabel", "TLabel") and item.get("caption"):
                    auto = {"kind": "info", "value": item["caption"], "note": "图纸 Caption 逐字"}
                    out.extend(emit_field(item, auto, "        "))
                    continue
                raise SystemExit(
                    f"控件 {item['name']}（{sheet}，{item.get('class')}）既没在 controls 里声明，"
                    f"也没在 exclude 里说明"
                )
            out.extend(emit_field(item, specs[item["name"]], "        "))
        out.append("")
        out.append("        page.groups.push_back(std::move(group));")
        out.append("    }")
        out.append("")

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(out))
    checked = len(specs) + len(excluded)
    print(f"RESULT: OK（{sheet}：{len(groups)} 个分组，生成 {len(out)} 行）")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

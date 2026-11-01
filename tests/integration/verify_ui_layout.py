#!/usr/bin/env python3
"""verify_ui_layout.py —— 界面坐标表的独立复核（不依赖任何二进制）

为什么要有这个脚本
------------------
2026-10-28 那一版「界面整体错乱」是这么发生的：`tools/gen_ui_coords.py` 累加父容器
偏移时漏了控件自身的 Left/Top，组框内的控件全被写成 (0,0)。而当时的冒烟测试是拿
**页面模型里的坐标**去核**窗口里的坐标** —— 两边都来自那张错的表，于是自证清白，
266 项断言全绿，界面照样是乱的。

所以这里的判据必须来自表的**上游**：`docs/sc_layout/*.json`（DFM 逐控件解析出来的
原样坐标）。脚本自己重算一遍期望值，不复用 `gen_ui_coords.py` 的任何函数 ——
同一个函数既生成又校验，等于没校验。

校验四件事
----------
1. **坐标可复算**：表里每条控件的相对坐标 == 由 DFM 的绝对坐标减去分组框原点得到的值。
   漏加控件自身 Left/Top 的 bug 会在这里当场现形（差值恰好是控件自身坐标）。
2. **可见性可复算**：DFM 里 `Visible = False` 的控件，表里必须是 false。
   「压根没解析 Visible」的 bug 会在这里现形。
3. **不重不漏**：表里每条都能在 DFM 里找到；DFM 里有、表里没有的，必须逐条列在
   生成文件尾部的注释里，不许静默消失。
4. **版面自洽**：同一分组框内的控件不得互相压住、不得溢出分组框；页面级控件
   （分组框之间）不得互相压住。这一条不需要任何期望值，纯粹用几何关系兜底 ——
   哪怕上下游两份数据一起错，控件叠在一起仍然会被抓住。

用法: python tests/integration/verify_ui_layout.py [--layout <json>] [--coords <inc>]
退出码: 0 全部通过 / 1 有失败项 / 2 用法或读取错误
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))

DEFAULT_LAYOUT = os.path.join(REPO, "docs", "sc_layout", "TSCMF.json")
DEFAULT_COORDS = os.path.join(REPO, "host", "ui", "ui_dfm_coords.inc")

# 与 gen_ui_coords.py 的 SHEETS 保持一致：哪四页走坐标表。
# 这里独立抄一份而不是 import —— 被校验的对象不该提供校验器的配置。
SHEETS = {
    "MlSkinTabSheet1": "system",
    "MlSkinTabSheet3": "tasks",
    "MlSkinTabSheet4": "deploy",
    "MlSkinTabSheet5": "sysprep",
}

GROUP_BOX_CLASS = "TMlSkinGroupBox"

# {"page", "group", "field", x, y, w, h, true|false},
ROW_RE = re.compile(
    r'\{\s*"([a-z]+)",\s*"([A-Za-z_0-9]*)",\s*"([A-Za-z_0-9]*)",\s*'
    r"(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(true|false)\s*\},"
)

# 生成文件尾部列出的「未进表」控件，形状:
# // system 页 DFM 里另有以下控件未进坐标表（模型里没有对应字段）：
# //   Btn_Xxx、Lab_Yyy
SKIP_HEAD_RE = re.compile(r"^//\s*([a-z]+)\s*页 DFM 里另有以下控件未进坐标表")
SKIP_BODY_RE = re.compile(r"^//\s{3}(\S.*)$")

_checks = 0
_failures: list[str] = []


def _say(text: str = "") -> None:
    sys.stdout.write(text + "\n")


def section(title: str) -> None:
    _say("")
    _say(f"== {title} ==")


def safe_relpath(path: str) -> str:
    """相对路径只用于显示，但 relpath 在 Windows 跨盘符时会抛 ValueError。

    CI #74/76 就是这么挂的：临时目录在 C:，仓库在 D:，`os.path.relpath` 直接
    `ValueError: path is on mount 'C:', start on mount 'D:'`，闸门在打印第一行
    时就崩了 —— 负例脚本读到的是崩溃而不是断言失败。本机两者都在 C:，所以
    永远看不到这个坑。
    """
    try:
        return os.path.relpath(path, REPO)
    except ValueError:
        return path


def check(name: str, ok: bool, detail: str = "") -> bool:
    global _checks
    _checks += 1
    if ok:
        _say(f"  PASS  {name}")
    else:
        _say(f"  FAIL  {name}" + (f"  [{detail}]" if detail else ""))
        _failures.append(name)
    return ok


def load_layout(path: str) -> dict:
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def parse_coords(path: str) -> tuple[list[dict], dict[str, set[str]]]:
    """解析生成的坐标表，返回 (行, 尾部声明的未进表控件)"""
    with open(path, encoding="utf-8") as handle:
        text = handle.read()

    rows = []
    for match in ROW_RE.finditer(text):
        rows.append(
            {
                "page": match.group(1),
                "group": match.group(2),
                "field": match.group(3),
                "x": int(match.group(4)),
                "y": int(match.group(5)),
                "w": int(match.group(6)),
                "h": int(match.group(7)),
                "visible": match.group(8) == "true",
            }
        )

    skipped: dict[str, set[str]] = {}
    current: str | None = None
    for line in text.splitlines():
        head = SKIP_HEAD_RE.match(line)
        if head:
            current = head.group(1)
            skipped.setdefault(current, set())
            continue
        if current is not None:
            body = SKIP_BODY_RE.match(line)
            if body:
                for name in re.split(r"[、,，\s]+", body.group(1).strip()):
                    if name:
                        skipped[current].add(name)
            elif line.strip() and not line.startswith("//"):
                current = None
    return rows, skipped


def page_rows(layout: dict, sheet: str) -> dict[str, dict]:
    """某一页的 DFM 行，按控件名索引（坐标相对直接父容器，DFM 语义）"""
    prefix = f"{layout['form']}/Page_mf/{sheet}/"
    return {
        row["name"]: row
        for row in layout["controls"]
        if row.get("path", "").startswith(prefix)
    }


def ancestors(row: dict, sheet_path: str) -> list[str]:
    """从直接父容器到页面根，逐级列出祖先名"""
    path = row.get("path", "")
    rest = path[len(sheet_path) :] if path.startswith(sheet_path) else path
    chain = [part for part in rest.split("/")[:-1] if part]
    return chain


def absolute_rect(rows: dict[str, dict], sheet_path: str, name: str) -> tuple[int, int] | None:
    """把 DFM 的相对坐标累加成绝对坐标；任一环缺 left/top 就返回 None"""
    row = rows.get(name)
    if row is None:
        return None
    x = row.get("left")
    y = row.get("top")
    if x is None or y is None:
        return None
    for parent in ancestors(row, sheet_path):
        entry = rows.get(parent)
        if entry is None or entry.get("left") is None or entry.get("top") is None:
            return None
        x += entry["left"]
        y += entry["top"]
    return x, y


def nearest_group_box(rows: dict[str, dict], sheet_path: str, name: str) -> str | None:
    row = rows.get(name)
    if row is None:
        return None
    for parent in reversed(ancestors(row, sheet_path)):
        if rows.get(parent, {}).get("class") == GROUP_BOX_CLASS:
            return parent
    return None


def overlaps(a: dict, b: dict, tolerance: int = 0) -> bool:
    """两个矩形是否真的互相压住（相邻不算；容差为 0 时贴边通过）"""
    ax2 = a["x"] + a["w"]
    ay2 = a["y"] + a["h"]
    bx2 = b["x"] + b["w"]
    by2 = b["y"] + b["h"]
    return (
        a["x"] < bx2 - tolerance
        and b["x"] < ax2 - tolerance
        and a["y"] < by2 - tolerance
        and b["y"] < ay2 - tolerance
    )


def verify_sheet(layout: dict, sheet: str, page: str, rows: list[dict], skipped: dict[str, set[str]]) -> None:
    sheet_path = f"{layout['form']}/Page_mf/{sheet}/"
    dfm = page_rows(layout, sheet)
    page_rows_out = [row for row in rows if row["page"] == page]

    # ---------------------------------------------------------------- 3. 不重不漏
    seen: set[str] = set()
    duplicated: list[str] = []
    for row in page_rows_out:
        key = row["field"] or row["group"]
        if key in seen:
            duplicated.append(key)
        seen.add(key)
    check(f"{page}: 坐标表里没有重复条目", not duplicated, "、".join(duplicated[:5]))

    unknown = sorted(name for name in seen if name not in dfm)
    check(f"{page}: 表里每个控件都能在 DFM 里找到", not unknown, "、".join(unknown[:5]))

    declared_skip = skipped.get(page, set())
    absent = {name for name in dfm if name not in seen} - declared_skip
    check(
        f"{page}: DFM 里有、表里没有的控件都在尾部注释里声明了",
        not absent,
        "未声明：" + "、".join(sorted(absent)[:8]),
    )

    # ---------------------------------------------------------------- 1. 坐标可复算
    coord_bad: list[str] = []
    for row in page_rows_out:
        if row["field"]:
            continue
        entry = dfm.get(row["group"])
        if entry is None:
            continue
        expected = (
            entry.get("left"),
            entry.get("top"),
            entry.get("width"),
            entry.get("height"),
        )
        actual = (row["x"], row["y"], row["w"], row["h"])
        if expected != actual:
            coord_bad.append(f"{row['group']} 期望={expected} 实际={actual}")
    check(f"{page}: 页面级控件/分组框的矩形与 DFM 逐字段一致", not coord_bad, "; ".join(coord_bad[:5]))

    rel_bad: list[str] = []
    for row in page_rows_out:
        if not row["field"]:
            continue
        abs_rect = absolute_rect(dfm, sheet_path, row["field"])
        if abs_rect is None:
            rel_bad.append(f"{row['field']} 在 DFM 里缺坐标")
            continue
        group = dfm.get(row["group"])
        if group is None:
            rel_bad.append(f"{row['field']} 的分组框 {row['group']} 不在 DFM 里")
            continue
        expected_x = abs_rect[0] - group.get("left", 0)
        expected_y = abs_rect[1] - group.get("top", 0)
        if (row["x"], row["y"]) != (expected_x, expected_y):
            rel_bad.append(
                f"{row['field']} 期望={expected_x},{expected_y} 实际={row['x']},{row['y']}"
                f"（差={row['x'] - expected_x},{row['y'] - expected_y}）"
            )
    check(f"{page}: 组内控件的相对坐标 == 绝对坐标 − 分组框原点", not rel_bad, "; ".join(rel_bad[:5]))

    # ---------------------------------------------------------------- 2. 可见性可复算
    vis_bad: list[str] = []
    for row in page_rows_out:
        entry = dfm.get(row["field"] or row["group"])
        if entry is None:
            continue
        dfm_visible = entry.get("visible") is not False
        if dfm_visible != row["visible"]:
            vis_bad.append(
                f"{row['field'] or row['group']} DFM={dfm_visible} 表={row['visible']}"
            )
    check(f"{page}: Visible=False 的控件在表里也是 false", not vis_bad, "; ".join(vis_bad[:5]))

    # ---------------------------------------------------------------- 4. 版面自洽
    # 4a. 组内控件不得溢出自己的分组框，也不得互相压住。
    #     需要表里的相对坐标已经是「相对分组框」，所以先换算成绝对再比。
    groups: dict[str, list[dict]] = {}
    for row in page_rows_out:
        if row["field"]:
            group = dfm.get(row["group"]) or {}
            groups.setdefault(row["group"], []).append(
                {
                    "id": row["field"],
                    "x": row["x"] + (group.get("left") or 0),
                    "y": row["y"] + (group.get("top") or 0),
                    "w": row["w"],
                    "h": row["h"],
                    "visible": row["visible"],
                }
            )

    outside: list[str] = []
    stacked: list[str] = []
    for group_name, items in groups.items():
        box = dfm.get(group_name) or {}
        gx, gy = box.get("left") or 0, box.get("top") or 0
        gw, gh = box.get("width") or 0, box.get("height") or 0
        visibles = [item for item in items if item["visible"]]
        for item in visibles:
            if gw and gh:
                if (
                    item["x"] < gx
                    or item["y"] < gy
                    or item["x"] + item["w"] > gx + gw + 2
                    or item["y"] + item["h"] > gy + gh + 2
                ):
                    outside.append(
                        f"{group_name}/{item['id']} 越界 "
                        f"({item['x']},{item['y']} {item['w']}x{item['h']} 出 "
                        f"{gx},{gy} {gw}x{gh})"
                    )
        for i in range(len(visibles)):
            for j in range(i + 1, len(visibles)):
                if overlaps(visibles[i], visibles[j]):
                    stacked.append(f"{group_name}: {visibles[i]['id']} × {visibles[j]['id']}")

    check(f"{page}: 组内控件不溢出分组框", not outside, "; ".join(outside[:5]))
    check(f"{page}: 组内可见控件互不重叠", not stacked, "; ".join(stacked[:8]))

    # 4b. 页面级控件（分组框之间）互不重叠 —— 那次「五块 572x352 面板全显示」
    #     就是这个形状：同一矩形上叠 5 个面板。
    tops = [
        {
            "id": row["group"],
            "x": row["x"],
            "y": row["y"],
            "w": row["w"],
            "h": row["h"],
        }
        for row in page_rows_out
        if not row["field"] and row["visible"]
    ]
    top_stacked: list[str] = []
    for i in range(len(tops)):
        for j in range(i + 1, len(tops)):
            if overlaps(tops[i], tops[j]):
                top_stacked.append(f"{tops[i]['id']} × {tops[j]['id']}")
    check(f"{page}: 页面级可见控件互不重叠", not top_stacked, "; ".join(top_stacked[:8]))

    # 4c. 组内控件全落在 (0,0) 是那次事故的具体形状，单独钉一条
    zeroed = [
        f"{group}/{item['id']}"
        for group, items in groups.items()
        for item in items
        if item["w"] > 8 and item["h"] > 8 and (item["x"], item["y"]) == (0, 0)
    ]
    check(f"{page}: 没有控件被整片摆到分组框原点 (0,0)", not zeroed, "、".join(zeroed[:8]))


def main(argv: list[str]) -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[union-attr]

    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("--layout", default=DEFAULT_LAYOUT)
    parser.add_argument("--coords", default=DEFAULT_COORDS)
    parser.add_argument(
        "--json",
        default="",
        help="把结果写成结构化 JSON（ok / checks / failed）。负例脚本读这个判红，"
        "不去匹配中文输出 —— 那样一旦 runner 的 locale 变了就会假失败（2026-11-01 CI #74 实测）",
    )
    try:
        args = parser.parse_args(argv[1:])
    except SystemExit:
        return 2

    for path in (args.layout, args.coords):
        if not os.path.isfile(path):
            _say(f"找不到文件: {path}")
            return 2

    try:
        layout = load_layout(args.layout)
        rows, skipped = parse_coords(args.coords)
    except (OSError, json.JSONDecodeError, UnicodeDecodeError) as exc:
        _say(f"读取失败: {exc}")
        return 2

    _say(f"verify_ui_layout: {safe_relpath(args.layout)} + {safe_relpath(args.coords)}")
    _say(f"DFM 控件行 {len(layout['controls'])} 条 / 坐标表 {len(rows)} 条")

    section("解析完整性")
    check("坐标表能解析出条目", len(rows) > 0, f"{len(rows)} 条")
    pages = sorted({row["page"] for row in rows})
    check("覆盖全部四个坐标表页面", pages == sorted(SHEETS.values()), "、".join(pages))

    for sheet, page in SHEETS.items():
        section(f"{page}（{sheet}）")
        verify_sheet(layout, sheet, page, rows, skipped)

    _say("")
    ok = not _failures
    if not ok:
        _say(f"RESULT: FAILED  ({len(_failures)}/{_checks} checks failed)")
        for name in _failures:
            _say(f"  - {name}")
    else:
        _say(f"RESULT: ALL PASSED  ({_checks} checks)")

    if args.json:
        payload = {
            "schemaVersion": 1,
            "ok": ok,
            "checks": _checks,
            "failed": list(_failures),
            "layout": safe_relpath(args.layout),
            "coords": args.coords,
        }
        folder = os.path.dirname(os.path.abspath(args.json))
        if folder:
            os.makedirs(folder, exist_ok=True)
        with open(args.json, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(payload, handle, ensure_ascii=False, indent=2)
            handle.write("\n")

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))

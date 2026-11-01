#!/usr/bin/env python3
"""verify_ui_layout_negative.py —— 证明界面坐标闸门真的会咬人

`verify_ui_layout.py` 在健康的仓库上是全绿的；全绿本身不能说明它有用 ——
一个永远 return 0 的脚本也是全绿。所以这里反过来做：把坐标表按**已知事故的形状**
改坏，逐个确认闸门变红，并且红在正确的那一条断言上。

四个负例（前两个就是 2026-10-28 那次「界面整体错乱」的两个成因）：

1. 组内控件坐标退回 (0,0)        —— 生成器漏加控件自身 Left/Top
2. Visible=False 改成 true        —— 生成器没解析 DFM 的 Visible
3. 两个页面级分组框叠到同一矩形   —— 五块 572x352 面板同时显示的形状
4. 表里出现 DFM 里没有的控件名    —— 生成器把 name 拼错/串页

用法: python tests/integration/verify_ui_layout_negative.py [--coords <inc>]
退出码: 0 全部被拦下 / 1 有漏网的
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_COORDS = os.path.join(REPO, "host", "ui", "ui_dfm_coords.inc")
CHECKER = os.path.join(HERE, "verify_ui_layout.py")

ROW_RE = re.compile(
    r'\{\s*"([a-z]+)",\s*"([A-Za-z_0-9]*)",\s*"([A-Za-z_0-9]*)",\s*'
    r"(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(true|false)\s*\},"
)

_checks = 0
_failures: list[str] = []


def _say(text: str = "") -> None:
    sys.stdout.write(text + "\n")


def section(title: str) -> None:
    _say("")
    _say(f"== {title} ==")


def check(name: str, ok: bool, detail: str = "") -> bool:
    global _checks
    _checks += 1
    if ok:
        _say(f"  PASS  {name}")
    else:
        _say(f"  FAIL  {name}" + (f"  [{detail}]" if detail else ""))
        _failures.append(name)
    return ok


def run_checker(coords_path: str) -> tuple[int, str]:
    result = subprocess.run(
        [sys.executable, CHECKER, "--coords", coords_path],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    return result.returncode, result.stdout + result.stderr


def expect_blocked(work: str, label: str, mutated: str, must_mention: str) -> None:
    path = os.path.join(work, f"{label}.inc")
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(mutated)
    code, output = run_checker(path)
    check(f"{label}: 闸门退出码非 0", code != 0, f"exit={code}")
    check(f"{label}: 红在「{must_mention}」这条断言上", must_mention in output)


def main(argv: list[str]) -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[union-attr]

    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("--coords", default=DEFAULT_COORDS)
    try:
        args = parser.parse_args(argv[1:])
    except SystemExit:
        return 2

    if not os.path.isfile(args.coords):
        _say(f"找不到文件: {args.coords}")
        return 2

    with open(args.coords, encoding="utf-8") as handle:
        original = handle.read()

    work = tempfile.mkdtemp(prefix="ospt-ui-layout-neg-")
    _say(f"verify_ui_layout_negative: {os.path.relpath(args.coords, REPO)}")
    _say(f"临时目录: {work}")

    # 对照组：没改过的表必须全绿，否则后面几条「变红」都不算数
    section("对照组")
    code, output = run_checker(args.coords)
    check("未改动的坐标表通过（闸门不是永久红）", code == 0, f"exit={code}")

    # ---------------------------------------------------------------- 负例 1
    section("负例 1：组内控件坐标退回 (0,0)（漏加控件自身 Left/Top）")

    def zero_children(match: re.Match[str]) -> str:
        page, group, field, _x, _y, w, h, visible = match.groups()
        if not field:  # 分组框本身不动
            return match.group(0)
        return f'    {{"{page}", "{group}", "{field}", 0, 0, {w}, {h}, {visible}}},'

    mutated = ROW_RE.sub(zero_children, original)
    expect_blocked(work, "neg1_zero_coords", mutated, "组内控件的相对坐标 == 绝对坐标 − 分组框原点")

    # ---------------------------------------------------------------- 负例 2
    section("负例 2：Visible=False 改成 true（没解析 DFM 的 Visible）")
    hits = 0
    lines: list[str] = []
    for line in original.splitlines():
        if hits == 0 and ", false}," in line:
            line = line.replace(", false},", ", true},")
            hits += 1
        lines.append(line)
    check("原表里确实存在 Visible=False 的条目", hits == 1, f"替换 {hits} 处")
    expect_blocked(work, "neg2_visible", "\n".join(lines), "Visible=False 的控件在表里也是 false")

    # ---------------------------------------------------------------- 负例 3
    section("负例 3：两个页面级分组框叠到同一矩形（五块面板同时显示的形状）")
    top_rows = [
        index
        for index, line in enumerate(original.splitlines())
        if re.match(r'\s*\{"system", "Gb_[A-Za-z_0-9]+", "", ', line) and ", true}," in line
    ]
    check("system 页有足够的页面级分组框可叠", len(top_rows) >= 2, f"{len(top_rows)} 个")
    if len(top_rows) >= 2:
        lines = original.splitlines()
        first = ROW_RE.search(lines[top_rows[0]])
        assert first is not None
        _page, group, _field, x, y, w, h, _vis = first.groups()
        lines[top_rows[1]] = (
            f'    {{"system", "{group}2", "", {x}, {y}, {w}, {h}, true}},'
        )
        expect_blocked(work, "neg3_overlap", "\n".join(lines), "页面级可见控件互不重叠")

    # ---------------------------------------------------------------- 负例 4
    section("负例 4：表里出现 DFM 里没有的控件名")
    mutated = ROW_RE.sub(
        lambda m: m.group(0).replace(f'"{m.group(3)}"', '"Btn_NotInDfm"', 1)
        if m.group(3)
        else m.group(0),
        original,
        count=1,
    )
    expect_blocked(work, "neg4_ghost_control", mutated, "表里每个控件都能在 DFM 里找到")

    _say("")
    if _failures:
        _say(f"RESULT: FAILED  ({len(_failures)}/{_checks} checks failed)")
        for name in _failures:
            _say(f"  - {name}")
        return 1

    _say(f"RESULT: ALL PASSED  ({_checks} checks) —— 四个已知事故形状全部被拦下")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

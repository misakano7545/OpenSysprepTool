#!/usr/bin/env python3
"""dfm_layout.py —— 把 SC 反编译产物里的 DFM 窗体树解析成 JSON 布局表

用法：
    python tools/dfm_layout.py --src D:/dev/scpt/out/02_forms/dfm --out docs/sc_layout

为什么需要它：UI 必须与 SC 的窗体逐控件对齐（控件名、类、Caption、坐标、尺寸），
靠人肉抄写随时会漂。这里把 DFM 解析成机器可读的布局表并提交进仓库，单测拿它
和界面模型逐项比对，漂了就红。

**只提取界面事实**（控件名 / 类名 / Caption / 四边坐标），不搬运任何 Delphi 代码，
也不搬运图片二进制（Picture.Data 只记录字节数）。

DFM 是文本格式，语法很规整：
    object 名字: 类名  ... 属性 ...  end
属性值 = 数字 / '字符串' / [枚举] / {...} 二进制 / 嵌套属性 X.Y = 值。
多行二进制块（Picture.Data = { ... }）要整体跳过，否则会被当成控件。
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

OBJECT_RE = re.compile(r"^(\s*)object\s+([A-Za-z_][\w]*)\s*:\s*([A-Za-z_][\w]*)\s*$")
END_RE = re.compile(r"^\s*end\s*$")
PROP_RE = re.compile(r"^\s*([A-Za-z_][\w.]*)\s*=\s*(.*)$")


class Node:
    def __init__(self, name: str, cls: str, depth: int) -> None:
        self.name = name
        self.cls = cls
        self.depth = depth
        self.props: dict[str, str] = {}
        self.children: list[Node] = []
        self.picture_bytes = 0
        self.items: list[str] = []  # Items.Strings 的多行条目

    def to_dict(self) -> dict:
        def number(key: str) -> int | None:
            raw = self.props.get(key)
            if raw is None:
                return None
            try:
                return int(raw.strip())
            except ValueError:
                return None

        item = {
            "name": self.name,
            "class": self.cls,
            "caption": unquote(self.props.get("Caption", "")),
            "left": number("Left"),
            "top": number("Top"),
            "width": number("Width"),
            "height": number("Height"),
        }
        if self.picture_bytes:
            item["pictureBytes"] = self.picture_bytes
        if self.items:
            item["items"] = self.items
        if self.children:
            item["children"] = [child.to_dict() for child in self.children]
        return item


def unquote(raw: str) -> str:
    text = raw.strip()
    if len(text) >= 2 and text[0] == "'" and text[-1] == "'":
        return text[1:-1].replace("''", "'")
    return text


def parse(text: str) -> list[Node]:
    roots: list[Node] = []
    stack: list[Node] = []
    in_binary = False
    in_items = False

    for line in text.splitlines():
        stripped = line.strip()

        # Items.Strings = ( 'xxx' ... ) 多行块：逐行收条目
        if in_items:
            if stripped == ")":
                in_items = False
                continue
            value = unquote(stripped)
            if value:
                stack[-1].items.append(value) if stack else None
            continue

        # 二进制块（Picture.Data = { ... }）整体跳过
        if in_binary:
            if "}" in stripped:
                in_binary = False
            continue
        if stripped.endswith("{") and "=" in stripped:
            in_binary = True
            continue

        match = OBJECT_RE.match(line)
        if match:
            node = Node(match.group(2), match.group(3), len(match.group(1)))
            if stack:
                stack[-1].children.append(node)
            else:
                roots.append(node)
            stack.append(node)
            continue

        if END_RE.match(line) and stack:
            stack.pop()
            continue

        prop = PROP_RE.match(line)
        if prop and stack:
            key, value = prop.group(1), prop.group(2)
            if key == "Items.Strings":
                if value.strip() == "(":
                    in_items = True
                elif value.strip().startswith("("):
                    # 单行写法 ( 'a' 'b' )
                    for token in re.findall(r"'([^']*)'", value):
                        stack[-1].items.append(token)
                continue
            if key == "Picture.Data":
                stack[-1].picture_bytes = len(value.replace(" ", ""))
            else:
                stack[-1].props[key] = value
            continue

    return roots


def flatten(node: Node, out: list[dict], parent: str = "") -> None:
    """把树压平成「每个控件一行」，带上父链，便于逐项核对与排序比较。"""
    path = f"{parent}/{node.name}" if parent else node.name
    out.append({
        "path": path,
        "name": node.name,
        "class": node.cls,
        "caption": unquote(node.props.get("Caption", "")),
        "left": _int(node.props.get("Left")),
        "top": _int(node.props.get("Top")),
        # Delphi 里 Visible 默认 True，DFM 只在设成 False 时才写出来。
        # 不记这一位，SC 那些「点按钮才显示」的堆叠面板会全部同时显示 —— 界面就糊成一团。
        **({"visible": False} if node.props.get("Visible", "").strip().lower() == "false" else {}),
        "width": _int(node.props.get("Width")),
        "height": _int(node.props.get("Height")),
        "depth": node.depth // 2,
    })
    if node.items:
        out[-1]["items"] = list(node.items)
    for child in node.children:
        flatten(child, out, path)


def _int(raw: str | None) -> int | None:
    if raw is None:
        return None
    try:
        return int(raw.strip())
    except ValueError:
        return None


def main(argv: list[str]) -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[union-attr]

    parser = argparse.ArgumentParser()
    parser.add_argument("--src", required=True, help="包含 *.dfm 的目录")
    parser.add_argument("--out", required=True, help="输出 JSON 的目录")
    args = parser.parse_args(argv[1:])

    if not os.path.isdir(args.src):
        print(f"找不到目录: {args.src}")
        return 2
    os.makedirs(args.out, exist_ok=True)

    count = 0
    for name in sorted(os.listdir(args.src)):
        if not name.lower().endswith(".dfm"):
            continue
        path = os.path.join(args.src, name)
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            roots = parse(handle.read())
        if not roots:
            print(f"跳过（没解析出窗体）: {name}")
            continue

        form = roots[0]
        rows: list[dict] = []
        flatten(form, rows)
        payload = {
            "form": form.name,
            "class": form.cls,
            "clientHeight": _int(form.props.get("ClientHeight")),
            "clientWidth": _int(form.props.get("ClientWidth")),
            "controls": rows,
        }
        target = os.path.join(args.out, os.path.splitext(name)[0] + ".json")
        with open(target, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(payload, handle, ensure_ascii=False, indent=2, sort_keys=False)
            handle.write("\n")
        print(f"{name}: {len(rows)} 个控件 -> {target}")
        count += 1

    print(f"RESULT: OK（{count} 个窗体）")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

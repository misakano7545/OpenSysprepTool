#!/usr/bin/env python3
"""校验某个提交（或工作区）相对基线只产生空白与换行变化。

为什么不用 `git diff -w`：`-w` 忽略行内空白，但 clang-format 会合并或拆分
手工折行的表达式，行数一变，diff 就无法逐行对齐，`-w` 会报出实际上不存在的内容差异。
本脚本改用更严格且不受行数影响的判据：

    剔除全部空白字符后，两份文本必须逐字节相同。

相等 => 只可能是空白 / 换行变化，非空白字符一个都没动。

用法:
  python tests/tools/check_whitespace_only.py <基线 rev> [目标 rev]
  python tests/tools/check_whitespace_only.py <基线 rev> --worktree

示例:
  python tests/tools/check_whitespace_only.py HEAD~1 HEAD        # 校验最近一个提交
  python tests/tools/check_whitespace_only.py HEAD --worktree    # 校验未提交的改动

退出码: 0 全部仅空白变化 / 1 存在非空白差异 / 2 用法或 git 错误
"""

from __future__ import annotations

import re
import subprocess
import sys

SOURCE_SUFFIXES = (".cpp", ".hpp", ".h", ".cc", ".cxx")
SKIP_PREFIXES = ("third_party/",)


def git(*args: str) -> str:
    proc = subprocess.run(["git", *args], capture_output=True)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr.decode("utf-8", "replace"))
        raise SystemExit(2)
    return proc.stdout.decode("utf-8", "replace")


def squeeze(text: str) -> str:
    """剔除所有空白字符。"""
    return re.sub(r"\s+", "", text)


def is_source(path: str) -> bool:
    return path.endswith(SOURCE_SUFFIXES) and not path.startswith(SKIP_PREFIXES)


def read_side(rev: str, path: str) -> str | None:
    """rev 为 --worktree 时读工作区，否则读该 rev 的版本。不存在返回 None。"""
    if rev == "--worktree":
        try:
            with open(path, encoding="utf-8") as handle:
                return handle.read()
        except FileNotFoundError:
            return None
        except OSError as exc:
            sys.stderr.write(f"读取失败 {path}: {exc}\n")
            raise SystemExit(2)

    proc = subprocess.run(["git", "show", f"{rev}:{path}"], capture_output=True)
    if proc.returncode != 0:
        return None
    return proc.stdout.decode("utf-8", "replace")


def main(argv: list[str]) -> int:
    if len(argv) < 2 or len(argv) > 3:
        sys.stderr.write(__doc__ or "")
        return 2

    base = argv[1]
    target = argv[2] if len(argv) == 3 else "HEAD"

    diff_ref = target if target == "--worktree" else f"{target}"
    if target == "--worktree":
        changed = git("diff", "--name-only", base)
    else:
        changed = git("diff", "--name-only", "--diff-filter=ACMR", base, target)

    files = [line.strip() for line in changed.splitlines() if line.strip()]
    sources = [f for f in files if is_source(f)]

    print(f"基线: {base}   目标: {target}")
    print(f"改动文件: {len(files)} 个，其中源文件: {len(sources)} 个")

    if not sources:
        print("没有需要校验的源文件 —— 视为通过")
        return 0

    bad: list[str] = []
    for path in sources:
        before = read_side(base, path)
        after = read_side(target, path)

        if before is None and after is None:
            print(f"  ??  {path}  两侧都读不到")
            continue
        if before is None:
            print(f"  新增 {path}  （新文件，无基线可比）")
            continue
        if after is None:
            print(f"  删除 {path}")
            continue

        if squeeze(before) == squeeze(after):
            print(f"  OK  {path}  （仅空白变化）")
        else:
            print(f"  BAD {path}  （存在非空白差异）")
            bad.append(path)

    print()
    if bad:
        print(f"结果: 失败 —— {len(bad)} 个文件含非空白差异")
        for path in bad:
            print(f"  - {path}")
        return 1

    print("结果: 通过 —— 所有源文件均只发生空白 / 换行变化")
    return 0


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, OSError):
        pass
    sys.exit(main(sys.argv))

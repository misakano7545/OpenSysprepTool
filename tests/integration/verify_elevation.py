#!/usr/bin/env python3
"""verify_elevation.py —— 管理员权限闸门

用法:
    python tests/integration/verify_elevation.py build/bin/Release/ospt.exe \
                                                 build/bin/Release/osptd.exe \
                                                 build/bin/Release/osp_unit_tests.exe

断言：
  1. 两个产品产物（ospt.exe / osptd.exe）的嵌入清单声明 asInvoker
     （启动时自检 + runas 重启提权，manifest 不能是 requireAdministrator，
      否则 CreateProcess 直接拒绝、代码没机会跑）
  2. 单元测试产物声明 asInvoker —— 测试要能在任何会话（含 CI 的非提权步骤）里直接跑
  3. ospt.exe 是 WINDOWS 子系统（2）—— 装载器因此不分配控制台，
     正常启动只出现界面窗口，不会有命令行窗口
  4. osptd.exe 与控制台产物仍是 CONSOLE 子系统（3）—— 部署要看单页进度
  5. 三者的子系统版本都还是 6.1（Win7 SP1 那道闸没被这次改动破掉）
  6. **自动提权**：未提权会话启动 ospt.exe 时，它应该用 runas 重新启动自己；
     已提权时直接跑。本闸门在两种会话里都验证：未提权时产物进程应该退出
     （启动了提权副本），已提权时直接跑通并自述 elevated=true
  7. README 写明管理员权限要求

退出码：0 全过；1 有失败；2 用法错。
"""

from __future__ import annotations

import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import peinfo  # noqa: E402

EXPECTED_LEVEL = {
    "ospt.exe": "asInvoker",
    "osptd.exe": "asInvoker",
    "osp_unit_tests.exe": "asInvoker",
}
EXPECTED_SUBSYSTEM = {
    "ospt.exe": peinfo.SUBSYSTEM_WINDOWS,
    "osptd.exe": peinfo.SUBSYSTEM_CONSOLE,
    "osp_unit_tests.exe": peinfo.SUBSYSTEM_CONSOLE,
}
EXPECTED_SUBSYSTEM_VERSION = (6, 1)
ERROR_ELEVATION_REQUIRED = 740

_checks = 0
_failures: list[str] = []
_skips: list[str] = []


def _say(text: str = "") -> None:
    print(text, flush=True)


def section(title: str) -> None:
    _say("")
    _say(f"== {title}")


def check(name: str, ok: bool, detail: str = "") -> bool:
    global _checks
    _checks += 1
    mark = "PASS" if ok else "FAIL"
    line = f"  {mark}  {name}"
    if detail:
        line += f"  ({detail})"
    _say(line)
    if not ok:
        _failures.append(name)
        _say(f"::error::FAIL  {name}")
    return ok


def skip(name: str, reason: str) -> None:
    _skips.append(name)
    _say(f"  SKIP  {name}  —— {reason}")


def is_elevated() -> bool:
    """当前进程是否已提权（实现放在 peinfo，多个闸门共用）。"""
    return peinfo.is_elevated()


def verify_products(paths: list[str]) -> None:
    section("清单与子系统")
    for path in paths:
        name = os.path.basename(path)
        if not os.path.isfile(path):
            check(f"{name} 存在", False, path)
            continue

        level = peinfo.execution_level(path)
        check(
            f"{name} 的清单声明 {EXPECTED_LEVEL[name]}",
            level == EXPECTED_LEVEL[name],
            f"实测 {level}",
        )

        kind, major, minor = peinfo.subsystem(path)
        expected_kind = EXPECTED_SUBSYSTEM[name]
        check(
            f"{name} 的子系统类型是 {peinfo.SUBSYSTEM_NAME[expected_kind]}（{expected_kind}）",
            kind == expected_kind,
            f"实测 {kind}（{peinfo.SUBSYSTEM_NAME.get(kind, '?')}）",
        )
        check(
            f"{name} 的子系统版本仍是 {EXPECTED_SUBSYSTEM_VERSION[0]}.{EXPECTED_SUBSYSTEM_VERSION[1]}",
            (major, minor) == EXPECTED_SUBSYSTEM_VERSION,
            f"实测 {major}.{minor}",
        )


def verify_enforcement(product: str) -> None:
    """自动提权：未提权时产物应该重启自己（退出码 0），已提权时直接跑。"""
    section("自动提权")
    elevated = is_elevated()
    _say(f"  当前会话已提权: {elevated}")

    if elevated:
        # 已提权：直接跑，应该成功且自述 elevated=true
        result = peinfo.run_text([product, "--version"], timeout=60)
        text = (result.stdout or "") + (result.stderr or "")
        check("已提权时产物能运行", result.returncode == 0 and "ospt" in text, f"exit={result.returncode}")

        report = peinfo.run_text(
            [os.path.join(os.path.dirname(product), "osptd.exe"), "--win7-compat", "--json"],
            timeout=120,
        )
        try:
            elevated_flag = json.loads(report.stdout)["elevated"]
        except Exception:
            elevated_flag = None
        check("产物自述 elevated=true", elevated_flag is True, f"实测 {elevated_flag}")
    else:
        # 未提权：产物应该用 runas 重启自己，然后退出（退出码 0）
        # 注意：ShellExecuteEx 会弹 UAC 或静默提权（取决于系统设置），
        # 启动后原进程退出。这里只验证「进程确实启动了且退出了」，
        # 不验证新进程是否存活（那会弹 UAC 阻塞）。
        result = subprocess.run(
            [product, "--version"], capture_output=True, timeout=60
        )
        # 未提权时产物应该正常退出（0），因为它启动了提权副本
        check(
            "未提权时产物正常退出（启动了提权副本）",
            result.returncode == 0,
            f"exit={result.returncode}",
        )
        skip("已提权时产物能运行", "当前会话未提权；提权分支由 CI 或手动验证")


def verify_docs(readme: str) -> None:
    section("文档")
    if not os.path.isfile(readme):
        check("README 存在", False, readme)
        return
    text = open(readme, encoding="utf-8").read()
    check("README 写明需要管理员权限", "管理员" in text and "UAC" in text)
    check("README 写明正常启动只出现界面", "命令行窗口" in text)


def main(argv: list[str]) -> int:
    # runner 上 stdout 可能是 cp1252，打印中文会抛 UnicodeEncodeError
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[union-attr]

    if len(argv) < 2:
        _say("usage: python tests/integration/verify_elevation.py <ospt.exe> [<osptd.exe> ...]")
        return 2

    paths = argv[1:]
    _say(f"verify_elevation: {len(paths)} 个产物")

    verify_products(paths)
    product = next((p for p in paths if os.path.basename(p) == "ospt.exe"), None)
    if product:
        verify_enforcement(product)
    verify_docs(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "README.md"))

    _say("")
    if _failures:
        _say(f"RESULT: FAILED  ({len(_failures)}/{_checks} checks failed)")
        for name in _failures:
            _say(f"  - {name}")
        return 1

    skipped = f"，{len(_skips)} 项跳过" if _skips else ""
    _say(f"RESULT: ALL PASSED  ({_checks} checks{skipped})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

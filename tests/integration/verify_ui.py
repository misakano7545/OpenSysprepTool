#!/usr/bin/env python3
"""verify_ui.py —— M7a 主界面框架与系统信息页验收

用法:
    python tests/integration/verify_ui.py [<ospt.exe>]

验的是宪章 §8 M7a 的字面验收：
    build/bin/Release/ospt.exe --ui-smoke-test   → 退出码 0

外加三条约束与一条 CI 要求：
  1. 界面用的是原生标准控件，产物因此必须导入 USER32（没有它说明窗口层没进去）
  2. Win7 三道闸没被破坏：把 PE 交给 verify_win7_compat.py 复核
  3. 无头环境下必须**显式**打印 skip 原因，不许静默略过（宪章 §9.7 M7）
"""

from __future__ import annotations

import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))

_checks = 0
_failures: list[str] = []


def _say(text: str = "") -> None:
    print(text, flush=True)


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


def run(args: list[str]) -> tuple[int, str]:
    completed = subprocess.run(args, capture_output=True, timeout=300, check=False)
    return completed.returncode, (completed.stdout + completed.stderr).decode("utf-8", "replace")


def imported_dlls(path: str) -> list[str]:
    import struct

    data = open(path, "rb").read()
    pe_off = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe_off + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe_off + 20)[0]
    opt = pe_off + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    data_dirs = opt + (112 if magic == 0x20B else 96)

    sec_off = opt + opt_size
    sections = []
    for i in range(nsec):
        base = sec_off + i * 40
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, base + 8)
        sections.append((vaddr, max(vsize, rawsize), rawptr))

    def rva2off(rva: int):
        for vaddr, size, rawptr in sections:
            if vaddr <= rva < vaddr + size:
                return rawptr + (rva - vaddr)
        return None

    imp_rva = struct.unpack_from("<I", data, data_dirs + 8)[0]
    base = rva2off(imp_rva)
    if base is None:
        return []

    names = []
    index = 0
    while True:
        entry = base + index * 20
        _oft, _ts, _fwd, name_rva, _ft = struct.unpack_from("<IIIII", data, entry)
        if name_rva == 0:
            break
        offset = rva2off(name_rva)
        if offset is None:
            break
        end = data.index(b"\0", offset)
        names.append(data[offset:end].decode("ascii", "replace"))
        index += 1
    return names


def main(argv: list[str]) -> int:
    ospt = argv[1] if len(argv) > 1 else os.path.join(REPO, "build", "bin", "Release", "ospt.exe")
    report_path = os.path.join(REPO, "build", "m7a", "smoke.json")

    _say(f"verify_ui: ospt={ospt}")

    section("前置")
    if not check("ospt.exe 存在", os.path.isfile(ospt), ospt):
        return 1

    os.makedirs(os.path.dirname(report_path), exist_ok=True)

    section("gate：--ui-smoke-test")
    code, output = run([ospt, "--ui-smoke-test", "--report", report_path])
    check("退出码 0", code == 0, f"code={code}")

    if not os.path.isfile(report_path):
        check("生成了 JSON 报告", False, report_path)
        return 1
    check("生成了 JSON 报告", True)

    report = json.load(open(report_path, encoding="utf-8-sig"))
    skipped = bool(report.get("skipped"))

    if skipped:
        # 宪章 §9.7 M7：无头环境可以跳过，但必须显式说出原因
        reason = str(report.get("skipReason", "")).strip()
        check("跳过时给出了原因", bool(reason), reason)
        check("stdout 里出现了 SMOKE-SKIP 标记", "SMOKE-SKIP" in output)
        check("stdout 里印出了跳过原因", "跳过的原因" in output and reason[:12] in output)
        _say("  本机没有窗口站，UI 冒烟测试被显式跳过 —— 这是允许的结果")
    else:
        check("报告 ok=true", report.get("ok") is True)
        check("报告 skipped=false", report.get("skipped") is False)
        checks = int(report.get("checks", 0))
        failures = int(report.get("failures", 0))
        check("检查项数 >= 20", checks >= 20, str(checks))
        check("失败项为 0", failures == 0, str(failures))
        check("stdout 里出现 RESULT: SMOKE-OK", "SMOKE-OK" in output)

        lines = "\n".join(report.get("lines", []))
        for marker in [
            "创建主窗口",
            "页签控件存在",
            "选中第一个页签",
            "字段文本与页面模型一致: 系统名称",
            "字段文本与页面模型一致: 运行环境",
            "「重新体检」按钮响应并触发处理器",
            "单选按钮状态可读写",
            "下拉框选中项可读写",
            "编辑框文本往返一致",
            "窗口销毁无崩溃",
        ]:
            check(f"冒烟记录里有「{marker}」", marker in lines)

    section("约束：原生控件进了产物、且 Win7 三道闸没破")
    dlls = [d.lower() for d in imported_dlls(ospt)]
    check("导入表里有 USER32.dll（窗口层确实编进去了）", "user32.dll" in dlls, ", ".join(dlls))
    for forbidden in ["vcruntime140.dll", "msvcp140.dll", "ucrtbase.dll"]:
        check(f"没有 {forbidden}", forbidden not in dlls)

    # 「用的是原生标准控件」不靠导入表猜（宿主标准控件并不需要静态导入
    # gdi32/comctl32），而是看冒烟测试的实测结论。
    if not skipped:
        lines = "\n".join(report.get("lines", []))
        check("页签控件实测为系统标准的 SysTabControl32",
              "页签控件是系统标准的 SysTabControl32" in lines)
        check("通用控件库 comctl32 实测已加载", "通用控件库 comctl32 已加载" in lines)

    win7_gate = os.path.join(HERE, "verify_win7_compat.py")
    if os.path.isfile(win7_gate):
        code, gate_output = run([sys.executable, win7_gate, ospt])
        check("Win7 兼容闸门对 ospt.exe 仍然全绿", code == 0,
              gate_output.strip().splitlines()[-1] if gate_output else "")

    _say("")
    if _failures:
        _say(f"RESULT: FAILED  ({len(_failures)}/{_checks} checks failed)")
        for name in _failures:
            _say(f"  - {name}")
        return 1

    _say(f"RESULT: ALL PASSED  ({_checks} checks)")
    return 0


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[union-attr]
    sys.exit(main(sys.argv))

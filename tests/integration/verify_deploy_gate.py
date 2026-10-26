#!/usr/bin/env python3
"""verify_deploy_gate.py —— M10 部署前闸门验收

用法:
    python tests/integration/verify_deploy_gate.py <osptd.exe> [<ospt.exe>]

覆盖 SC 部署设置里三项「部署时才生效」的功能：
    Cb_Tampering        校验文件防篡改（锁定文件列表 + MD5，不匹配则提示并中止）
    Btn_DisableVmrun    禁止在虚拟机里部署
    SA_Rb_hijack_*      部署主控被劫持：继续 / 终止 + 弹提示

为什么必须用真文件核对 MD5：这一项错了不会崩，只会「放行本该拦住的文件」，
是最危险的失败方式。所以期望值用 certutil 独立算一遍交叉验证。
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

_checks = 0
_skipped = 0
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


def run(args: list[str], timeout: int = 180) -> tuple[int, str]:
    completed = subprocess.run(args, capture_output=True, timeout=timeout, check=False)
    return completed.returncode, (completed.stdout + completed.stderr).decode("utf-8", "replace")


def md5_of(path: str) -> str:
    """用系统 certutil 独立算 MD5，与 osptd 内部的实现交叉验证。"""
    completed = subprocess.run(["certutil", "-hashfile", path, "MD5"], capture_output=True, check=False)
    text = completed.stdout.decode("mbcs", "replace")
    for line in text.splitlines():
        token = line.replace(" ", "").strip()
        if len(token) == 32 and all(c in "0123456789abcdefABCDEF" for c in token):
            return token.lower()
    return ""


def write_plan(directory: str, *, verify: bool, md5: str | None, block_vm: bool = False,
               hijack: str = "terminate", tip: str = "", controller: str = "") -> str:
    os.makedirs(directory, exist_ok=True)
    plan = {
        "schemaVersion": 1,
        "driveTable": {"source": "file", "drives": []},
        "presentation": {
            "module": "console", "background": "systemDefault", "titleText": "闸门验收",
            "backgroundImage": "", "imageSwitchSeconds": 10, "resolutionX": 0, "resolutionY": 0,
            "fontColor": "",
        },
        "deployChecks": {
            "verifyIntegrity": verify,
            "blockVirtualMachine": block_vm,
            "hijackMode": hijack,
            "hijackTip": tip,
            "controllerMd5": controller,
            "tamperFiles": ([{"path": os.path.abspath(os.path.join(directory, "..", "payload.txt")), "md5": md5}]
                            if md5 is not None else []),
        },
        "phases": [{
            "phase": "preinstall", "label": "部署前", "script": "preinstall.cmd", "taskCount": 1,
            "tasks": [{"index": 1, "type": "run", "commandLine": "cmd.exe /c echo hi"}],
        }],
        "taskCount": 1,
    }
    path = os.path.join(directory, "runplan.json")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(plan, handle, ensure_ascii=False, indent=2)
    return directory


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[union-attr]

    if len(sys.argv) < 2:
        _say("用法: python tests/integration/verify_deploy_gate.py <osptd.exe>")
        return 2
    osptd = os.path.abspath(sys.argv[1])
    if not os.path.isfile(osptd):
        _say(f"找不到 osptd.exe: {osptd}")
        return 2

    work = tempfile.mkdtemp(prefix="ospt-gate-")
    try:
        payload = os.path.join(work, "payload.txt")
        with open(payload, "w", encoding="utf-8") as handle:
            handle.write("我要被校验的内容")

        section("文件防篡改（Cb_Tampering）")
        expect = md5_of(payload)
        check("certutil 能算出 MD5 作为独立基准", len(expect) == 32, expect)

        ok_dir = write_plan(os.path.join(work, "p-ok"), verify=True, md5=expect)
        code, output = run([osptd, "--dry-run", "--plan", ok_dir])
        check("MD5 一致时干跑退出码 0", code == 0, f"exit={code}\n{output[-300:]}")
        check("MD5 一致时报告校验通过", "防篡改校验通过" in output, output[-300:])

        bad_dir = write_plan(os.path.join(work, "p-bad"), verify=True, md5="0" * 32)
        code, output = run([osptd, "--dry-run", "--plan", bad_dir])
        check("MD5 不一致时点出被篡改的文件", "被篡改" in output and "payload.txt" in output, output[-300:])
        # 干跑只报告，真实执行才拦
        code, output = run([osptd, "--plan", bad_dir])
        check("MD5 不一致时真实执行中止（退出码非 0）", code != 0, f"exit={code}\n{output[-300:]}")
        check("中止时说明未执行任何任务", "未执行任何任务" in output, output[-300:])

        missing_dir = write_plan(os.path.join(work, "p-missing"), verify=True, md5=expect)
        # 删掉被锁定的文件，验证「读不到」与「被篡改」是两种原因
        os.remove(payload)
        code, output = run([osptd, "--dry-run", "--plan", missing_dir])
        check("文件缺失时原因说的是读不到", "读不到" in output, output[-300:])
        with open(payload, "w", encoding="utf-8") as handle:
            handle.write("我要被校验的内容")

        section("禁止在虚拟机里部署（Btn_DisableVmrun）")
        vm_dir = write_plan(os.path.join(work, "p-vm"), verify=False, md5=None, block_vm=True)
        code, output = run([osptd, "--dry-run", "--plan", vm_dir])
        check("虚拟机检测有明确结论", ("未在虚拟机中运行" in output) or ("虚拟机" in output and "拒绝" in output),
              output[-300:])
        if "拒绝" in output:
            _say("  （本机在虚拟机里，闸门按预期拒绝——这正是该功能要做的）")

        section("部署主控被劫持（SA_Rb_hijack_*）")
        term_dir = write_plan(os.path.join(work, "p-term"), verify=False, md5=None,
                              hijack="terminate", tip="部署主程序已被替换，已停止", controller="deadbeef" * 4)
        code, output = run([osptd, "--plan", term_dir])
        check("terminate 模式下中止部署", code != 0, f"exit={code}\n{output[-300:]}")
        check("提示文案原样给出（SA_Lab_ShowTip）", "部署主程序已被替换" in output, output[-300:])

        cont_dir = write_plan(os.path.join(work, "p-cont"), verify=False, md5=None,
                              hijack="continue", tip="部署主程序已被替换，按设置继续", controller="deadbeef" * 4)
        code, output = run([osptd, "--dry-run", "--plan", cont_dir])
        check("continue 模式下只告警不中止", code == 0 and "告警" in output, f"exit={code}\n{output[-300:]}")

        # 摘要正确时不该误报
        self_md5 = md5_of(osptd)
        good_dir = write_plan(os.path.join(work, "p-good"), verify=False, md5=None,
                              hijack="terminate", controller=self_md5)
        code, output = run([osptd, "--dry-run", "--plan", good_dir])
        check("主控摘要正确时通过校验", "部署主控校验通过" in output, output[-300:])

        section("汇总")
        _say("")
        if _failures:
            _say("RESULT: FAILED（%d 项失败 / %d 项检查，%d 项跳过）" % (len(_failures), _checks, _skipped))
            for name in _failures:
                _say(f"  - {name}")
            return 1
        _say(f"RESULT: ALL PASSED（{_checks} 项检查，{_skipped} 项跳过）")
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())

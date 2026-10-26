#!/usr/bin/env python3
"""verify_prepare.py —— M9 封装前设置验收

用法:
    python tests/integration/verify_prepare.py <ospt.exe>

覆盖：
    1. --prepare --dry-run 产出计划，动作与配置逐条对应，推迟项写明原因
    2. dry-run 零副作用（注册表哨兵前后一致，不写回滚日志）
    3. 不带 --execute 时拒绝执行
    4. --prepare-selftest 在 HKCU 里写入并回滚（不需要管理员）
    5. 配置解析失败 / 未知开关的退出码
    6. 提权会话里真实执行 closeNumLock 并用 --rollback 还原（未提权时显式跳过）
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import winreg

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))

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


def skip(name: str, reason: str) -> None:
    global _skipped
    _skipped += 1
    _say(f"  SKIP  {name}  [{reason}]")


def run(args: list[str], timeout: int = 180) -> tuple[int, str]:
    completed = subprocess.run(args, capture_output=True, timeout=timeout, check=False)
    return completed.returncode, (completed.stdout + completed.stderr).decode("utf-8", "replace")


def is_elevated() -> bool:
    try:
        import ctypes

        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:  # noqa: BLE001 - 查询失败按未提权处理
        return False


def read_registry(root: int, path: str, name: str) -> tuple[bool, object]:
    try:
        with winreg.OpenKey(root, path) as key:
            value, _kind = winreg.QueryValueEx(key, name)
            return True, value
    except FileNotFoundError:
        return False, None
    except OSError:
        return False, None


def write_json(path: str, data: dict) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(data, handle, ensure_ascii=False, indent=2)


def full_config() -> dict:
    return {
        "schemaVersion": 1,
        "systemInfo": {
            "registeredName": "OpenSysprep",
            "registeredOrg": "misakano7545",
            "serialNumber": "00000-00000-00000-00000",
            "computerName": "PC-",
            "computerNameMode": "prefixRandom",
            "bootMenuTimeoutSec": 5,
        },
        "firstLogon": {
            "runDeviceManager": True,
            "rebootAfterRun": True,
            "resolutionX": 1920,
            "resolutionY": 1080,
            "refreshRate": 60,
            "colorDepth": 32,
            "syncIeHomepage": True,
            "syncIeFavorites": True,
        },
        "optimization": {
            "pinTaskbarItems": True,
            "closeNumLock": True,
            "enableHibernate": True,
            "closeRebootDialog": True,
            "myDocumentsTarget": "D:\\Docs",
            "pageFileTarget": "D:\\",
            "autoSetIp": True,
            "ipMode": "fixed",
            "networkLocation": "work",
            "clearSrsDrivers": True,
            "setResolution": True,
        },
        "runPlan": [],
        "deploy": {
            "module": "trueTrans",
            "background": "systemDefault",
            "titleText": "正在部署系统，请稍候…",
            "skipOobe": True,
            "autoCreateUser": True,
            "userName": "User",
            "userGroup": "Administrators",
            "blockVirtualMachine": True,
            "verifyIntegrity": True,
            "enableAdministrator": True,
        },
        "sysprep": {
            "mode": "classic",
            "generateNewSid": True,
            "uninstallDrivers": True,
            "loadSrsDrivers": True,
            "transferDesktopFiles": True,
            "cleanTemp": True,
            "cleanDllCache": True,
            "autoLogon": True,
            "autoLogonCount": 1,
            "workgroup": "WORKGROUP",
            "domain": "",
            "domainUser": "",
            "afterAction": "reboot",
            "perServerLicensing": True,
            "maxConnections": 5,
        },
    }


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8")
    if len(sys.argv) < 2:
        _say("用法: python tests/integration/verify_prepare.py <ospt.exe>")
        return 2

    ospt = os.path.abspath(sys.argv[1])
    if not os.path.isfile(ospt):
        _say(f"找不到 ospt.exe: {ospt}")
        return 2

    work = os.path.join(REPO, "build", "m9", "verify")
    os.makedirs(work, exist_ok=True)

    cfg_path = os.path.join(work, "cfg.json")
    plan_path = os.path.join(work, "plan.json")
    journal_path = os.path.join(work, "journal.json")
    report_path = os.path.join(work, "report.json")
    bad_cfg_path = os.path.join(work, "bad.json")

    for stale in (plan_path, journal_path, report_path):
        if os.path.exists(stale):
            os.remove(stale)

    write_json(cfg_path, full_config())
    with open(bad_cfg_path, "w", encoding="utf-8") as handle:
        handle.write("{ this is not json")

    # ------------------------------------------------------------ dry-run
    section("--prepare --dry-run")
    sentinel_key = r"SOFTWARE\Policies\Microsoft\Windows NT\Reliability"
    before = read_registry(winreg.HKEY_LOCAL_MACHINE, sentinel_key, "ShutdownReasonOn")

    code, output = run([ospt, "--prepare", cfg_path, "--dry-run", "--report", plan_path, "--journal", journal_path])
    check("dry-run 退出码 0", code == 0, f"exit={code}\n{output}")
    check("输出含 RESULT: DRY-RUN OK", "RESULT: DRY-RUN OK" in output)
    check("dry-run 不写回滚日志", not os.path.exists(journal_path))

    after = read_registry(winreg.HKEY_LOCAL_MACHINE, sentinel_key, "ShutdownReasonOn")
    check("dry-run 零副作用（哨兵值未变）", before == after, f"{before} -> {after}")

    plan = {}
    if os.path.isfile(plan_path):
        with open(plan_path, "r", encoding="utf-8") as handle:
            plan = json.load(handle)
    check("计划 JSON 可解析", isinstance(plan, dict) and bool(plan))
    check("计划 ok=true", plan.get("ok") is True)

    actions = plan.get("actions", [])
    ids = {item.get("id") for item in actions}
    expected_ids = {
        "optimization.closeNumLock",
        "optimization.enableHibernate",
        "optimization.closeRebootDialog.reasonOn",
        "optimization.closeRebootDialog.reasonUi",
        "optimization.pageFileTarget",
        "firstLogon.syncIeFavorites",
        "deploy.enableAdministrator",
        "sysprep.transferDesktopFiles",
        "sysprep.cleanTemp.user",
        "sysprep.cleanTemp.windows",
        "sysprep.cleanDllCache",
    }
    for expected in sorted(expected_ids):
        check(f"动作存在: {expected}", expected in ids)

    check("动作总数与 actionCount 一致", plan.get("actionCount") == len(actions), f"{plan.get('actionCount')} vs {len(actions)}")

    pagefile = next((item for item in actions if item.get("id") == "optimization.pageFileTarget"), None)
    check("虚拟内存值按 PagingFiles 格式生成", pagefile is not None and pagefile.get("value") == "D:\\pagefile.sys 0 0", str(pagefile))
    check("虚拟内存标记需要重启", pagefile is not None and pagefile.get("requiresReboot") is True)

    deferred = {item.get("sourceField") for item in plan.get("deferred", [])}
    for field in (
        "optimization.myDocumentsTarget",
        "optimization.autoSetIp",
        "firstLogon.syncIeHomepage",
        "deploy.verifyIntegrity",
        "sysprep.uninstallDrivers",
        "sysprep.loadSrsDrivers",
    ):
        check(f"推迟项写明原因: {field}", field in deferred)

    check("推迟项都有非空原因", all(item.get("reason") for item in plan.get("deferred", [])))

    # ------------------------------------------------------------ 未加 --execute
    section("拒绝执行")
    code, output = run([ospt, "--prepare", cfg_path])
    check("不带 --execute 退出码 2", code == 2, f"exit={code}\n{output}")
    check("提示必须显式加 --execute", "必须显式加 --execute" in output)

    code, output = run([ospt, "--prepare", cfg_path, "--no-such-option"])
    check("未知开关退出码 2", code == 2, f"exit={code}\n{output}")

    code, output = run([ospt, "--prepare", bad_cfg_path, "--dry-run"])
    check("坏配置退出码 1", code == 1, f"exit={code}\n{output}")
    check("坏配置给出解析失败原因", "配置解析失败" in output)

    code, output = run([ospt, "--rollback", bad_cfg_path, "--dry-run"])
    check("坏回滚日志退出码 1", code == 1, f"exit={code}\n{output}")

    # ------------------------------------------------------------ 自检
    section("--prepare-selftest")
    code, output = run([ospt, "--prepare-selftest"])
    check("自检退出码 0", code == 0, f"exit={code}\n{output}")
    check("自检输出 prepare selftest OK", "prepare selftest OK" in output)
    exists, value = read_registry(
        winreg.HKEY_CURRENT_USER, r"Software\OpenSysprepTool\SelfTest", "Text"
    )
    check("自检后测试值已清理", not exists, f"exists={exists} value={value!r}")

    # ------------------------------------------------------------ 真实执行 + 回滚
    section("--prepare --execute 与 --rollback")
    if not is_elevated():
        skip("真实执行 closeNumLock 并回滚", "当前会话未提权；--prepare --execute 需要管理员")
    else:
        keyboard_key = r".DEFAULT\Control Panel\Keyboard"
        before_exists, before_value = read_registry(winreg.HKEY_USERS, keyboard_key, "InitialKeyboardIndicators")

        minimal = {
            "schemaVersion": 1,
            "optimization": {"closeNumLock": True},
            "runPlan": [],
        }
        minimal_path = os.path.join(work, "minimal.json")
        write_json(minimal_path, minimal)

        if os.path.exists(journal_path):
            os.remove(journal_path)

        code, output = run(
            [ospt, "--prepare", minimal_path, "--execute", "--journal", journal_path, "--report", report_path]
        )
        check("execute 退出码 0", code == 0, f"exit={code}\n{output}")
        check("execute 输出 RESULT: OK", "RESULT: OK" in output)
        check("回滚日志已写入", os.path.isfile(journal_path))

        after_exists, after_value = read_registry(winreg.HKEY_USERS, keyboard_key, "InitialKeyboardIndicators")
        check("NumLock 值已写成 2", after_exists and str(after_value) == "2", f"{after_exists} {after_value!r}")

        if os.path.isfile(journal_path):
            with open(journal_path, "r", encoding="utf-8") as handle:
                journal = json.load(handle)
            check("回滚日志有记录", len(journal.get("entries", [])) >= 1, str(journal.get("entries")))
        else:
            journal = {}

        code, output = run([ospt, "--rollback", journal_path, "--dry-run"])
        check("rollback dry-run 退出码 0", code == 0, f"exit={code}\n{output}")
        check("rollback dry-run 打印将还原/删除", ("将还原" in output) or ("将删除" in output))

        code, output = run([ospt, "--rollback", journal_path, "--execute"])
        check("rollback execute 退出码 0", code == 0, f"exit={code}\n{output}")
        check("rollback 输出 RESULT: OK", "RESULT: OK" in output)

        restored_exists, restored_value = read_registry(
            winreg.HKEY_USERS, keyboard_key, "InitialKeyboardIndicators"
        )
        check(
            "NumLock 值已还原",
            (restored_exists, str(restored_value) if restored_exists else None)
            == (before_exists, str(before_value) if before_exists else None),
            f"before={before_exists},{before_value!r} after={restored_exists},{restored_value!r}",
        )

    _say("")
    if _failures:
        _say(f"RESULT: FAILED（{len(_failures)} 项失败 / {_checks} 项检查，{_skipped} 项跳过）")
        for name in _failures:
            _say(f"  - {name}")
        return 1
    _say(f"RESULT: ALL PASSED（{_checks} 项检查，{_skipped} 项跳过）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

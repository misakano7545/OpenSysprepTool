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
    # GitHub windows-* runner 的 Python stdout 是 cp1252，打印中文会在任何断言之前
    # 抛 UnicodeEncodeError；这里强制 UTF-8，与其它验收脚本保持一致。
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[union-attr]
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
    checkup_path = os.path.join(work, "checkup.json")
    sysprep_report_path = os.path.join(work, "sysprep-report.json")
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

    # ------------------------------------------------------------ 体检项（M10）
    section("体检项与阻断判定")
    code, output = run([ospt, "--checkup", "--json", checkup_path])
    check("--checkup --json 退出码 0", code == 0, f"exit={code}")
    checkup = {}
    if os.path.isfile(checkup_path):
        with open(checkup_path, "r", encoding="utf-8") as handle:
            checkup = json.load(handle)

    checks = {item.get("id"): item for item in checkup.get("checks", [])}
    for expected in (
        "pendingServicing",
        "pendingFileRename",
        "bitlocker",
        "domainJoin",
        "inPlaceUpgrade",
        "unattendResidue",
        "dirtyBit",
        "appxPackages",
    ):
        check(f"体检项存在: {expected}", expected in checks)

    for item in checks.values():
        check(f"体检项 {item.get('id')} 有严重级别", item.get("severity") in ("critical", "warning", "info"))
        check(f"体检项 {item.get('id')} 有处理建议", bool(item.get("resolution")))

    # PFRO 实测不阻断，必须是 info
    if "pendingFileRename" in checks:
        check("待重命名文件是 info（不阻断）", checks["pendingFileRename"].get("severity") == "info")

    blocking = checkup.get("blocking", [])
    check("blocking 是数组", isinstance(blocking, list))
    critical_detected = [
        item["id"] for item in checks.values()
        if item.get("severity") == "critical" and item.get("detected")
    ]
    check("blocking 与 critical 命中项一致", sorted(blocking) == sorted(critical_detected),
          f"blocking={blocking} critical={critical_detected}")

    # --sysprep --dry-run 的 JSON 必须带同一份体检结果
    code, output = run([ospt, "--sysprep", "--dry-run", "--report", sysprep_report_path])
    check("--sysprep --dry-run 退出码 0", code == 0, f"exit={code}\n{output[-400:]}")
    sysprep_report = {}
    if os.path.isfile(sysprep_report_path):
        with open(sysprep_report_path, "r", encoding="utf-8") as handle:
            sysprep_report = json.load(handle)
    sysprep_checks = {item.get("id") for item in sysprep_report.get("checks", [])}
    check("sysprep 报告带体检项", len(sysprep_checks) >= 8, str(sorted(sysprep_checks)))
    sysprep_blockers = sysprep_report.get("blockers", [])
    check(
        "sysprep 的阻塞项与体检 critical 项一致",
        len(sysprep_blockers) == len(critical_detected),
        f"blockers={sysprep_blockers} critical={critical_detected}",
    )

    # ------------------------------------------------------------ 封装前清理动作
    section("封装前清理动作")
    clean_cfg = full_config()
    clean_cfg["sysprep"].update({
        "stopSysprepProcess": True,
        "cleanShadowCopies": True,
        "cleanEventLogs": True,
        "cleanRecycleBin": True,
        "cleanComponentStore": True,
        "removeUnattendResidue": True,
    })
    clean_cfg_path = os.path.join(work, "cfg-clean.json")
    clean_plan_path = os.path.join(work, "plan-clean.json")
    write_json(clean_cfg_path, clean_cfg)

    code, output = run([ospt, "--prepare", clean_cfg_path, "--dry-run", "--report", clean_plan_path])
    check("清理计划 dry-run 退出码 0", code == 0, f"exit={code}\n{output[-300:]}")
    clean_plan = {}
    if os.path.isfile(clean_plan_path):
        with open(clean_plan_path, "r", encoding="utf-8") as handle:
            clean_plan = json.load(handle)
    clean_actions = {item.get("id"): item for item in clean_plan.get("actions", [])}

    stop = clean_actions.get("sysprep.stopSysprepProcess")
    check("结束 sysprep 进程动作存在", stop is not None)
    check("taskkill 参数正确", stop is not None and "/F /IM sysprep.exe /T" in stop.get("arguments", ""),
          str(stop))

    shadow = clean_actions.get("sysprep.cleanShadowCopies")
    check("删除卷影副本动作存在", shadow is not None)
    check("vssadmin 参数正确",
          shadow is not None and shadow.get("arguments") == "delete shadows /all /quiet", str(shadow))

    for log in ("System", "Application", "Setup", "Windows PowerShell"):
        item = clean_actions.get(f"sysprep.cleanEventLogs.{log}")
        check(f"事件日志动作存在: {log}", item is not None)
        check(f"事件日志命令正确: {log}",
              item is not None and f'cl "{log}"' in item.get("arguments", ""), str(item))

    dism_action = clean_actions.get("sysprep.cleanComponentStore")
    check("DISM 组件清理动作存在", dism_action is not None)
    check("组件清理不带 /resetbase",
          dism_action is not None and "resetbase" not in dism_action.get("arguments", ""), str(dism_action))
    check("组件清理命令正确",
          dism_action is not None and
          dism_action.get("arguments") == "/online /cleanup-image /startcomponentcleanup", str(dism_action))

    residue_ids = [key for key in clean_actions if key.startswith("sysprep.removeUnattendResidue.")]
    check("残留应答文件动作三条", len(residue_ids) == 3, str(residue_ids))

    recycle_ids = [key for key in clean_actions if key.startswith("sysprep.cleanRecycleBin.")]
    check("回收站动作按固定磁盘生成", len(recycle_ids) >= 1, str(recycle_ids))
    for key in recycle_ids:
        check(f"回收站路径按 SID 定位: {key}",
              "$Recycle.Bin\\S-1-" in clean_actions[key].get("path", ""), str(clean_actions[key]))

    cleanup_ids = [
        "sysprep.stopSysprepProcess",
        "sysprep.cleanShadowCopies",
        "sysprep.cleanComponentStore",
    ]
    cleanup_ids += [key for key in clean_actions
                    if key.startswith("sysprep.cleanEventLogs.")
                    or key.startswith("sysprep.cleanRecycleBin.")
                    or key.startswith("sysprep.removeUnattendResidue.")]
    not_irreversible = [key for key in cleanup_ids
                        if not clean_actions.get(key, {}).get("irreversible")]
    check("清理动作全部标记不可回滚", not not_irreversible, str(not_irreversible))

    # ------------------------------------------------------------ 拒绝执行
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
    try:
        sys.exit(main())
    except Exception:  # noqa: BLE001
        # 未预期的异常：把 traceback 打到 stdout 而不是 stderr。
        # CI 步骤即使只 Tee stdout，也能看到真正的原因，而不是一个无从定位的退出码。
        import traceback

        traceback.print_exc(file=sys.stdout)
        sys.exit(3)

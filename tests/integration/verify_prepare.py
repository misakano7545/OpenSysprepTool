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
    # 我的文档转移已在 B3 落地（离线 hive），不该再出现在推迟项里；
    # 剩下这些仍是推迟项 —— 每一项都必须写清原因，不许静默略过。
    check("我的文档转移不再是推迟项", "optimization.myDocumentsTarget" not in deferred)
    check("自动设置 IP 不再是推迟项", "optimization.autoSetIp" not in deferred)
    # 防篡改在部署端（osptd 闸门）执行，封装前不该再列为推迟项
    check("文件防篡改不再是推迟项", "deploy.verifyIntegrity" not in deferred)
    # 卸载本机驱动在 B9 落地（pnputil 动作），也不该再出现在推迟清单
    check("卸载本机驱动不再是推迟项", "sysprep.uninstallDrivers" not in deferred)
    for field in (
        "firstLogon.syncIeHomepage",
        "sysprep.loadSrsDrivers",
    ):
        check(f"推迟项写明原因: {field}", field in deferred)

    check("推迟项都有非空原因", all(item.get("reason") for item in plan.get("deferred", [])))

    # ------------------------------------------------------------ 宽带连接（B9b）
    section("宽带连接")
    bb_cfg = full_config()
    bb_cfg["firstLogon"] = dict(bb_cfg.get("firstLogon") or {})
    bb_cfg["firstLogon"].update({
        "createConnection": True,
        "connectionMode": "silent",
        "connectionName": "我的宽带",
        "connectionUser": "user001",
        "connectionPassword": "pw-123456",
    })
    bb_path = os.path.join(work, "cfg-broadband.json")
    bb_report = os.path.join(work, "plan-broadband.json")
    write_json(bb_path, bb_cfg)
    code, output = run([ospt, "--prepare", bb_path, "--dry-run", "--report", bb_report])
    check("宽带连接计划 dry-run 退出码 0", code == 0, f"exit={code}\n{output[-300:]}")
    bb_plan = {}
    if os.path.isfile(bb_report):
        with open(bb_report, "r", encoding="utf-8") as handle:
            bb_plan = json.load(handle)
    pppoe = [a for a in bb_plan.get("actions", []) if a.get("kind") == "pppoeCreate"]
    check("静默模式生成 pppoeCreate 动作", len(pppoe) == 1, f"{len(pppoe)} 个")
    if pppoe:
        dest = str(pppoe[0].get("path", ""))
        check("宽带连接写进默认用户电话簿", "rasphone.pbk" in dest.lower() or "Pbk" in dest, dest)
        check("宽带连接名写进计划", pppoe[0].get("connectionName") == "我的宽带", str(pppoe[0].get("connectionName")))

    # 展示模式：封装前不建连接（交给首次进桌面弹向导）
    bb_show = dict(bb_cfg)
    bb_show["firstLogon"] = dict(bb_cfg["firstLogon"], connectionMode="show")
    show_path = os.path.join(work, "cfg-broadband-show.json")
    show_report = os.path.join(work, "plan-broadband-show.json")
    write_json(show_path, bb_show)
    code, output = run([ospt, "--prepare", show_path, "--dry-run", "--report", show_report])
    check("展示模式 dry-run 退出码 0", code == 0, f"exit={code}")
    show_plan = {}
    if os.path.isfile(show_report):
        with open(show_report, "r", encoding="utf-8") as handle:
            show_plan = json.load(handle)
    check("展示模式不生成封装前动作",
          not [a for a in show_plan.get("actions", []) if a.get("kind") == "pppoeCreate"])

    # ------------------------------------------------------------ 任务栏固定 / 取消固定（B9）
    section("任务栏固定与取消固定")
    shortcut = os.path.join(work, "我的工具.lnk")
    with open(shortcut, "wb") as handle:
        handle.write(b"shortcut bytes")

    tb_cfg = full_config()
    tb_cfg["optimization"] = dict(tb_cfg.get("optimization") or {})
    tb_cfg["optimization"].update({
        "lockTaskbarFiles": [shortcut, os.path.join(work, "不存在.lnk")],
        "unlockTaskbarFiles": ["old-tool.lnk"],
    })
    tb_path = os.path.join(work, "cfg-taskbar.json")
    tb_report = os.path.join(work, "plan-taskbar.json")
    write_json(tb_path, tb_cfg)
    code, output = run([ospt, "--prepare", tb_path, "--dry-run", "--report", tb_report])
    check("任务栏计划 dry-run 退出码 0", code == 0, f"exit={code}\n{output[-300:]}")
    tb_plan = {}
    if os.path.isfile(tb_report):
        with open(tb_report, "r", encoding="utf-8") as handle:
            tb_plan = json.load(handle)
    tb_actions = tb_plan.get("actions", [])
    pin = [a for a in tb_actions if a.get("kind") == "fileCopy" and a.get("sourceField") == "optimization.lockTaskbarFiles"]
    unpin = [a for a in tb_actions if a.get("kind") == "fileDelete" and
             a.get("sourceField") == "optimization.unlockTaskbarFiles"]
    check("只给存在的文件生成固定动作", len(pin) == 1, f"{len(pin)} 个")
    if pin:
        dest = str(pin[0].get("destination", ""))
        # 落点必须是默认用户的 User Pinned\TaskBar
        check("固定动作写到 User Pinned", "User Pinned" in dest, dest)
        check("固定动作写到 TaskBar 目录", "TaskBar" in dest, dest)
        check("固定动作保留原文件名", dest.endswith("我的工具.lnk"), dest)
    check("取消固定生成删除动作", len(unpin) == 1, f"{len(unpin)} 个")
    if unpin:
        check("取消固定删的是同名的快捷方式", str(unpin[0].get("path", "")).endswith("old-tool.lnk"),
              str(unpin[0].get("path", "")))
    check("源文件不存在时告警", "源文件不存在" in output, output[-200:])

    # ------------------------------------------------------------ 卸载本机设备驱动（B9）
    section("卸载本机设备驱动")
    drv_cfg = full_config()
    drv_cfg["sysprep"] = dict(drv_cfg.get("sysprep") or {})
    drv_cfg["sysprep"]["uninstallDrivers"] = True
    drv_path = os.path.join(work, "cfg-drivers.json")
    drv_report = os.path.join(work, "plan-drivers.json")
    write_json(drv_path, drv_cfg)
    code, output = run([ospt, "--prepare", drv_path, "--dry-run", "--report", drv_report])
    check("卸载驱动计划 dry-run 退出码 0", code == 0, f"exit={code}\n{output[-300:]}")
    check("卸载驱动不再是推迟项", "sysprep.uninstallDrivers ——" not in output, output[-300:])
    drv_plan = {}
    if os.path.isfile(drv_report):
        with open(drv_report, "r", encoding="utf-8") as handle:
            drv_plan = json.load(handle)
    drv_actions = [a for a in drv_plan.get("actions", [])
                   if a.get("kind") == "runCommand" and a.get("sourceField") == "sysprep.uninstallDrivers"]
    check("生成了 pnputil 卸载动作（或本机无第三方驱动）",
          bool(drv_actions) or "没有枚举出第三方驱动包" in output,
          f"{len(drv_actions)} 个动作")
    for action in drv_actions:
        args = str(action.get("arguments", ""))
        if not args.startswith("/delete-driver oem"):
            check(f"卸载命令形状正确（{action.get('id')}）", False, args)
            break
    else:
        if drv_actions:
            check("卸载命令全部是 /delete-driver oemNN.inf", True)
    # 存储类必须避开：删了会 0x7B 蓝屏
    for action in drv_actions:
        note = str(action.get("note", ""))
        if "SCSIAdapter" in note or "HDC" in note or "DiskDrive" in note:
            check("存储类驱动不得进入卸载清单", False, note)
            break
    else:
        check("存储类驱动已被排除", True)

    # ------------------------------------------------------------ 自动设置 IP（B5）
    section("自动设置 IP：三种模式")
    ip_cfg = full_config()
    ip_cfg["optimization"] = dict(ip_cfg.get("optimization") or {})
    ip_cfg["optimization"].update({
        "autoSetIp": True,
        "ipMode": "fixed",
        "ipAddress": "192.168.50.10",
        "subnetMask": "255.255.255.0",
        "gateway": "192.168.50.1",
        "dnsPreferred": "114.114.114.114",
        "dnsAlternate": "8.8.8.8",
    })
    ip_cfg_path = os.path.join(work, "cfg-ip.json")
    ip_plan_path = os.path.join(work, "plan-ip.json")
    write_json(ip_cfg_path, ip_cfg)

    code, output = run([ospt, "--prepare", ip_cfg_path, "--dry-run", "--report", ip_plan_path])
    check("自动 IP 计划 dry-run 退出码 0", code == 0, f"exit={code}\n{output[-300:]}")
    ip_plan = {}
    if os.path.isfile(ip_plan_path):
        with open(ip_plan_path, "r", encoding="utf-8") as handle:
            ip_plan = json.load(handle)
    ip_actions = {item.get("id"): item for item in ip_plan.get("actions", [])}

    addr = ip_actions.get("optimization.autoSetIp") or {}
    if addr:
        # 网卡名取决于跑测试的机器，所以只断言命令骨架与地址/掩码/网关
        args = str(addr.get("arguments", ""))
        check("自动 IP 用 netsh 设置静态地址", args.startswith("interface ipv4 set address name="), args[:120])
        check("自动 IP 带上指定地址", "192.168.50.10" in args, args[:120])
        check("自动 IP 带上掩码与网关", "255.255.255.0" in args and "192.168.50.1" in args, args[:120])
        check("自动 IP 程序是 netsh", addr.get("program") == "netsh", str(addr.get("program")))
    else:
        # 机器上没有带网关的网卡时不会生成动作，但必须给出原因而不是静默
        check("没有可用网卡时给出告警", "找不到可用的网卡" in output, output[-200:])

    dns = ip_actions.get("optimization.autoSetIp.dns") or {}
    if dns:
        check("首选 DNS 用 set dns static", "set dns name=" in str(dns.get("arguments")) and
              "static 114.114.114.114" in str(dns.get("arguments")), str(dns.get("arguments"))[:120])
    dns_alt = ip_actions.get("optimization.autoSetIp.dnsAlt") or {}
    if dns_alt:
        check("备用 DNS 用 add dns index=2", "add dns name=" in str(dns_alt.get("arguments")) and
              "index=2" in str(dns_alt.get("arguments")), str(dns_alt.get("arguments"))[:120])

    # 网关与地址不同网段时必须告警——这种配置执行完直接断网
    bad_ip = dict(ip_cfg)
    bad_ip["optimization"] = dict(ip_cfg["optimization"], gateway="10.99.99.1")
    bad_path = os.path.join(work, "cfg-ip-bad.json")
    write_json(bad_path, bad_ip)
    code, output = run([ospt, "--prepare", bad_path, "--dry-run"])
    check("网关跨网段时告警", "不在同一子网" in output, output[-200:])

    # ------------------------------------------------------------ 离线 hive 三项（B3）
    section("离线 hive：我的文档 / 收藏夹 / IE 主页")
    hive_cfg = full_config()
    hive_cfg["optimization"] = dict(hive_cfg.get("optimization") or {})
    hive_cfg["optimization"].update({
        "moveDocuments": True,
        "documentsSmart": False,
        "myDocumentsTarget": "D:\\OspDocs",
        "moveFavorites": True,
        "favoritesSmart": False,
        "favoritesTarget": "D:\\OspFavs",
        "saveIeHomePage": True,
        "ieHomePage": "http://osp.example/home",
    })
    hive_cfg_path = os.path.join(work, "cfg-hive.json")
    hive_plan_path = os.path.join(work, "plan-hive.json")
    write_json(hive_cfg_path, hive_cfg)

    code, output = run([ospt, "--prepare", hive_cfg_path, "--dry-run", "--report", hive_plan_path])
    check("离线 hive 计划 dry-run 退出码 0", code == 0, f"exit={code}\n{output[-300:]}")
    hive_plan = {}
    if os.path.isfile(hive_plan_path):
        with open(hive_plan_path, "r", encoding="utf-8") as handle:
            hive_plan = json.load(handle)
    hive_actions = {item.get("id"): item for item in hive_plan.get("actions", [])}

    docs = hive_actions.get("optimization.myDocumentsTarget") or {}
    check("我的文档走 hiveSet 动作", docs.get("kind") == "hiveSet", str(docs)[:150])
    check("我的文档写到 Personal", docs.get("valueName") == "Personal", str(docs.get("valueName")))
    check("我的文档值是配置里的目录", docs.get("value") == "D:\\OspDocs", str(docs.get("value")))
    check("我的文档目标是 NTUSER.DAT", "NTUSER.DAT" in str(docs.get("hiveFile", "")).upper(),
          str(docs.get("hiveFile")))

    favs = hive_actions.get("optimization.moveFavorites") or {}
    check("收藏夹走 hiveSet 动作", favs.get("kind") == "hiveSet")
    check("收藏夹写到 Favorites", favs.get("valueName") == "Favorites")
    check("收藏夹值是配置里的目录", favs.get("value") == "D:\\OspFavs")

    ie = hive_actions.get("optimization.saveIeHomePage") or {}
    check("IE 主页走 hiveSet 动作", ie.get("kind") == "hiveSet")
    check("IE 主页写到 Start Page", ie.get("valueName") == "Start Page")
    check("IE 主页值是配置里的网址", ie.get("value") == "http://osp.example/home")
    # Win10/11 上这个键带拒绝写入的 ACL，必须允许降级为告警，否则整份封装会失败
    check("IE 主页允许写入失败降级", ie.get("tolerateFailure") is True, str(ie)[:150])

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

    # ------------------------------------------------------------ 系统优化项（SC 优化表）
    section("系统优化项来自 SC 优化表")
    tweak_cfg = full_config()
    tweak_cfg["optimization"] = dict(tweak_cfg.get("optimization") or {}, sysTweaks=[
        "tweak.diskTimeout", "tweak.bsodNoReboot", "tweak.menuShowDelay",
    ])
    tweak_cfg_path = os.path.join(work, "cfg-tweaks.json")
    tweak_plan_path = os.path.join(work, "plan-tweaks.json")
    write_json(tweak_cfg_path, tweak_cfg)

    code, output = run([ospt, "--prepare", tweak_cfg_path, "--dry-run", "--report", tweak_plan_path])
    check("优化项计划 dry-run 退出码 0", code == 0, f"exit={code}\n{output[-300:]}")
    tweak_plan = {}
    if os.path.isfile(tweak_plan_path):
        with open(tweak_plan_path, "r", encoding="utf-8") as handle:
            tweak_plan = json.load(handle)
    tweak_actions = {item.get("id"): item for item in tweak_plan.get("actions", [])}

    # 值必须与 SC 二进制里提取的一致，错一个字符就说明数据表被改过
    disk = tweak_actions.get("tweak.diskTimeout", {})
    check("tweak.diskTimeout 写到 HKLM 的 AutoChkTimeOut=5",
          disk.get("value") == "5" and disk.get("valueType") == "dword"
          and "Session Manager" in disk.get("key", "") and disk.get("hive") == "HKLM", str(disk))
    bsod = tweak_actions.get("tweak.bsodNoReboot", {})
    check("tweak.bsodNoReboot 写到 CrashControl AutoReboot=0",
          bsod.get("value") == "0" and bsod.get("valueType") == "dword"
          and "CrashControl" in bsod.get("key", ""), str(bsod))
    menu = tweak_actions.get("tweak.menuShowDelay", {})
    check("tweak.menuShowDelay 写到 HKCU 的 MenuShowDelay=0",
          menu.get("value") == "0" and menu.get("valueType") == "sz" and menu.get("hive") == "HKCU", str(menu))
    # SC 写死 ControlSet001；我们必须落到 CurrentControlSet，否则写进没被使用的控制集
    check("控制集统一用 CurrentControlSet",
          all("ControlSet001" not in item.get("key", "") for item in tweak_actions.values()),
          "出现 ControlSet001")
    check("每条优化项都记录了 SC 语言键",
          all(item.get("note", "").startswith("SC optimize_")
              for key, item in tweak_actions.items() if key.startswith("tweak.")),
          "缺少 note")

    # 未知优化项必须告警而不是静默丢弃
    bad_cfg = full_config()
    bad_cfg["optimization"] = dict(bad_cfg.get("optimization") or {}, sysTweaks=["tweak.no-such-tweak"])
    bad_path = os.path.join(work, "cfg-bad-tweak.json")
    write_json(bad_path, bad_cfg)
    code, output = run([ospt, "--prepare", bad_path, "--dry-run"])
    check("未知优化项给出告警", "未知优化项" in output, output[-200:])

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

#!/usr/bin/env python3
"""M4 验收脚本：校验 ospt --gen-runplan 产出的运行计划。

宪章 §8 M4 要求断言：
  1. 命令条数与阶段匹配
  2. 路径解析在模拟驱动器表上正确
  3. 等待策略映射到正确的「执行标志 + 等待方式」组合

本脚本独立重算一遍解析结果（不调用 ospt），再与 run/ 里的产物逐项比对。
驱动器表从 run/runplan.json 里读回，因此不依赖本机实际盘符。

用法: python tests/integration/verify_runplan.py plan.json run/
退出码: 0 全部通过 / 1 有失败项 / 2 用法或读取错误
"""

from __future__ import annotations

import json
import os
import sys

PHASES = ["preinstall", "install", "postinstall", "logon", "desktop"]

TASK_TYPES = ["run", "reg", "deleteFile", "deleteDir"]
WINDOW_STYLES = ["normal", "hidden", "minimized", "maximized"]
WAIT_STRATEGIES = ["wait", "noWait", "custom"]
PATH_KINDS = ["systemPath", "usbDrive", "cdRom", "allDrives"]

# 宪章 §8 M4 的两列表：等待策略 -> (dwCreationFlags, 等待方式)
CREATE_NEW_PROCESS_GROUP = 0x00000200
WAIT_TABLE = {
    "noWait": (CREATE_NEW_PROCESS_GROUP, "none"),
    "wait": (0, "infinite"),
    "custom": (0, "timeout"),
}

# STARTUPINFO.wShowWindow
SHOW_WINDOW = {
    "normal": 1,      # SW_SHOWNORMAL
    "hidden": 0,      # SW_HIDE
    "minimized": 2,   # SW_SHOWMINIMIZED
    "maximized": 3,   # SW_SHOWMAXIMIZED
}

_failures: list[str] = []
_checks = 0


def _say(text: str = "") -> None:
    sys.stdout.write(text + "\n")


def check(name: str, condition: bool, detail: str = "") -> bool:
    global _checks
    _checks += 1
    if condition:
        _say(f"  PASS  {name}" + (f"  ({detail})" if detail else ""))
    else:
        _say(f"  FAIL  {name}" + (f"  ({detail})" if detail else ""))
        _failures.append(name)
    return condition


def section(title: str) -> None:
    _say("")
    _say(f"[{title}]")


# ---------------------------------------------------------------- 路径解析

def strip_drive(path: str) -> str:
    """去掉盘符与前导分隔符，得到相对部分。"""
    start = 0
    if len(path) >= 2 and path[0].isalpha() and path[1] == ":":
        start = 2
    while start < len(path) and path[start] in ("\\", "/"):
        start += 1
    return path[start:]


def join_root(root: str, relative: str) -> str:
    base = root if root.endswith(("\\", "/")) else root + "\\"
    return base + relative


def is_absolute(path: str) -> bool:
    if len(path) >= 2 and path[0].isalpha() and path[1] == ":":
        return True
    if path.startswith("\\\\"):
        return True
    if path.startswith("%") and "%" in path[1:]:
        return True
    return False


def matches_kind(entry: dict, kind: str) -> bool:
    if kind == "systemPath":
        return bool(entry.get("systemDrive"))
    if kind == "usbDrive":
        return entry.get("driveType") == "removable"
    if kind == "cdRom":
        return entry.get("driveType") == "cdrom"
    if kind == "allDrives":
        return True
    return False


def select_drives(drives: list[dict], kind: str, label: str) -> list[dict]:
    selected = []
    for entry in drives:
        if not matches_kind(entry, kind):
            continue
        if label and entry.get("volumeLabel") != label:
            continue
        selected.append(entry)
    return selected


def command_line(task_type: str, path: str, arguments: str) -> str:
    if task_type == "run":
        return path if not arguments else path + " " + arguments
    if task_type == "reg":
        return f'reg.exe import "{path}"'
    if task_type == "deleteFile":
        return f'cmd /c del /f /q "{path}"'
    if task_type == "deleteDir":
        return f'cmd /c rd /s /q "{path}"'
    return path


def expected_phase_tasks(cfg: dict, drives: list[dict]) -> tuple[dict[str, list[dict]], list[str]]:
    """按同一套规则重算每个阶段应有哪些任务。无法解析时写进 errors。"""
    result: dict[str, list[dict]] = {phase: [] for phase in PHASES}
    errors: list[str] = []

    for index, task in enumerate(cfg.get("runPlan", []), start=1):
        phase = task.get("phase", "")
        task_type = task.get("type", "")
        style = task.get("windowStyle", "")
        wait = task.get("waitMode", "")
        kind = task.get("searchScope", "")
        target = task.get("target", "")
        label = task.get("expectedVolumeLabel", "")
        cleanup = task.get("cleanup", "")
        timeout_action = task.get("timeoutAction") or "continue"

        if phase not in PHASES:
            errors.append(f"runPlan[{index}] 阶段非法: {phase}")
            continue
        if task_type not in TASK_TYPES:
            errors.append(f"runPlan[{index}] 类型非法: {task_type}")
            continue
        if style not in WINDOW_STYLES:
            errors.append(f"runPlan[{index}] 运行方式非法: {style}")
            continue
        if wait not in WAIT_STRATEGIES:
            errors.append(f"runPlan[{index}] 等待策略非法: {wait}")
            continue
        if kind not in PATH_KINDS:
            errors.append(f"runPlan[{index}] 路径类型非法: {kind}")
            continue

        timeout_ms = int(task.get("waitSeconds", 0)) * 1000 if wait == "custom" else 0
        flags, wait_mode = WAIT_TABLE[wait]

        if kind == "systemPath":
            if not is_absolute(target):
                errors.append(f"runPlan[{index}] systemPath 需要绝对路径: {target}")
                continue
            resolved = [(target, "")]
        else:
            relative = strip_drive(target)
            candidates = select_drives(drives, kind, label)
            if not candidates:
                errors.append(f"runPlan[{index}] 路径类型 {kind} 在驱动器表里找不到候选")
                continue
            resolved = [(join_root(entry["root"], relative), entry["root"]) for entry in candidates]

        for path, drive in resolved:
            result[phase].append(
                {
                    "type": task_type,
                    "sourceTarget": target,
                    "resolvedPath": path,
                    "searchedDrive": drive,
                    "resolvedFromDriveTable": kind != "systemPath",
                    "isCleanupCommand": False,
                    "commandLine": command_line(task_type, path, task.get("arguments", "")),
                    "windowStyle": style,
                    "showWindow": SHOW_WINDOW[style],
                    "waitStrategy": wait,
                    "creationFlags": flags,
                    "waitMode": wait_mode,
                    "timeoutMs": timeout_ms,
                    "timeoutAction": timeout_action,
                }
            )
            if cleanup == "deleteFile":
                result[phase].append(
                    {
                        "type": "deleteFile",
                        "sourceTarget": target,
                        "resolvedPath": path,
                        "searchedDrive": drive,
                        "resolvedFromDriveTable": kind != "systemPath",
                        "isCleanupCommand": True,
                        "commandLine": command_line("deleteFile", path, ""),
                        "windowStyle": "hidden",
                        "showWindow": SHOW_WINDOW["hidden"],
                        "waitStrategy": "wait",
                        "creationFlags": 0,
                        "waitMode": "infinite",
                        "timeoutMs": 0,
                        "timeoutAction": "continue",
                    }
                )

    return result, errors


# ---------------------------------------------------------------- 断言

def verify_layout(run_dir: str) -> dict:
    section("产物布局")
    plan_path = os.path.join(run_dir, "runplan.json")
    if not check("run/runplan.json 存在", os.path.isfile(plan_path), plan_path):
        return {}

    for phase in PHASES:
        script = os.path.join(run_dir, phase + ".cmd")
        check(f"run/{phase}.cmd 存在", os.path.isfile(script), script)

    with open(plan_path, "rb") as handle:
        return json.loads(handle.read().decode("utf-8-sig"))


def verify_structure(plan_json: dict, cfg: dict) -> None:
    section("runplan.json 结构")
    check("schemaVersion == 1", plan_json.get("schemaVersion") == 1,
          str(plan_json.get("schemaVersion")))

    cfg_drives = cfg.get("drives")
    table = plan_json.get("driveTable", {})
    check("driveTable.source 合法", table.get("source") in {"file", "local"},
          str(table.get("source")))
    # 宪章边界：路径解析不得依赖本机实际盘符。计划文件内嵌驱动器表时必须走 file 分支。
    if isinstance(cfg_drives, list) and cfg_drives:
        check("计划文件内嵌驱动器表时来源为 file", table.get("source") == "file",
              str(table.get("source")))
        check("内嵌驱动器表被原样采用",
              [e.get("root") for e in table.get("drives", [])] ==
              [e.get("root") for e in cfg_drives],
              str([e.get("root") for e in table.get("drives", [])]))
    else:
        check("计划文件未内嵌驱动器表", table.get("source") in {"file", "local"},
              str(table.get("source")))
    drives = table.get("drives", [])
    check("driveTable 非空", isinstance(drives, list) and len(drives) > 0, f"{len(drives)} 个盘符")
    for entry in drives:
        check(f"盘符条目完整: {entry.get('root')}",
              bool(entry.get("root")) and bool(entry.get("driveType")))

    phases = plan_json.get("phases", [])
    check("阶段数为 5", len(phases) == 5, f"{len(phases)} 个")
    order = [p.get("phase") for p in phases]
    check("阶段顺序按文档", order == PHASES, ", ".join(str(x) for x in order))
    for phase in phases:
        name = str(phase.get("phase"))
        check(f"{name} 的脚本名匹配阶段", phase.get("script") == name + ".cmd",
              str(phase.get("script")))


def verify_counts(plan_json: dict, expected: dict[str, list[dict]]) -> None:
    section("验收 1/3：命令条数与阶段匹配")
    total_expected = 0
    total_actual = 0

    for phase in plan_json.get("phases", []):
        name = str(phase.get("phase"))
        tasks = phase.get("tasks", [])
        want = expected.get(name, [])
        total_expected += len(want)
        total_actual += len(tasks)
        check(f"{name} 任务数与输入一致", len(tasks) == len(want),
              f"产出={len(tasks)} 期望={len(want)}")
        check(f"{name} 的 taskCount 字段与实际一致", phase.get("taskCount") == len(tasks),
              f"字段={phase.get('taskCount')} 实际={len(tasks)}")

        indexes = [t.get("index") for t in tasks]
        check(f"{name} 序号从 1 连续", indexes == list(range(1, len(tasks) + 1)),
              str(indexes))

    check("顶层 taskCount 与各阶段之和一致", plan_json.get("taskCount") == total_actual,
          f"字段={plan_json.get('taskCount')} 实际={total_actual}")
    check("总任务数与按输入重算一致", total_actual == total_expected,
          f"产出={total_actual} 期望={total_expected}")


def verify_paths(plan_json: dict, expected: dict[str, list[dict]]) -> None:
    section("验收 2/3：路径解析在模拟驱动器表上正确")
    drives = plan_json.get("driveTable", {}).get("drives", [])
    roots = {str(entry.get("root")) for entry in drives}

    for phase in plan_json.get("phases", []):
        name = str(phase.get("phase"))
        for actual, want in zip(phase.get("tasks", []), expected.get(name, [])):
            tag = f"{name}[{actual.get('index')}]"
            check(f"{tag} 解析路径一致", actual.get("resolvedPath") == want["resolvedPath"],
                  f"产出={actual.get('resolvedPath')} 期望={want['resolvedPath']}")
            check(f"{tag} 命令行一致", actual.get("commandLine") == want["commandLine"],
                  str(actual.get("commandLine")))
            check(f"{tag} 来源标记一致",
                  bool(actual.get("resolvedFromDriveTable")) == want["resolvedFromDriveTable"])
            check(f"{tag} 类型一致", actual.get("type") == want["type"], str(actual.get("type")))

            drive = str(actual.get("searchedDrive", ""))
            if drive:
                check(f"{tag} 命中的盘符来自驱动器表", drive in roots, drive)
            if want["resolvedFromDriveTable"]:
                check(f"{tag} 解析结果落在某个盘符下",
                      any(str(actual.get("resolvedPath", "")).startswith(r) for r in roots),
                      str(actual.get("resolvedPath")))

    # 不依赖本机盘符：产出里出现的盘符必须都在模拟表里
    used = {str(t.get("searchedDrive")) for p in plan_json.get("phases", [])
            for t in p.get("tasks", []) if t.get("searchedDrive")}
    unknown = used - roots
    check("没有使用驱动器表之外的盘符", not unknown, ", ".join(sorted(unknown)))


def verify_wait_mapping(plan_json: dict, expected: dict[str, list[dict]]) -> None:
    section("验收 3/3：等待策略映射到执行标志 + 等待方式")
    seen: dict[str, int] = {}

    for phase in plan_json.get("phases", []):
        name = str(phase.get("phase"))
        for task, want in zip(phase.get("tasks", []), expected.get(name, [])):
            tag = f"{name}[{task.get('index')}]"
            strategy = str(task.get("waitStrategy"))
            if strategy not in WAIT_TABLE:
                check(f"{tag} 等待策略合法", False, strategy)
                continue
            seen[strategy] = seen.get(strategy, 0) + 1

            flags, wait_mode = WAIT_TABLE[strategy]
            check(f"{tag} dwCreationFlags 符合两列表", task.get("creationFlags") == flags,
                  f"产出={task.get('creationFlags')} 期望={flags}")
            check(f"{tag} 等待方式符合两列表", task.get("waitMode") == wait_mode,
                  f"产出={task.get('waitMode')} 期望={wait_mode}")

            if strategy == "noWait":
                check(f"{tag} 不等待时标志为 CREATE_NEW_PROCESS_GROUP",
                      task.get("creationFlagsName") == "CREATE_NEW_PROCESS_GROUP",
                      str(task.get("creationFlagsName")))
            else:
                check(f"{tag} 其余策略标志为 0", task.get("creationFlagsName") == "0",
                      str(task.get("creationFlagsName")))

            if strategy == "custom":
                check(f"{tag} 超时毫秒数与输入等待秒数一致",
                      task.get("timeoutMs") == want["timeoutMs"],
                      f"产出={task.get('timeoutMs')} 期望={want['timeoutMs']}")
                check(f"{tag} 超时动作与输入一致",
                      task.get("timeoutAction") == want["timeoutAction"],
                      f"产出={task.get('timeoutAction')} 期望={want['timeoutAction']}")
            else:
                check(f"{tag} 非超时策略 timeoutMs 为 0", task.get("timeoutMs") == 0,
                      str(task.get("timeoutMs")))

    for strategy in WAIT_STRATEGIES:
        check(f"样例覆盖了等待策略 {strategy}", seen.get(strategy, 0) > 0,
              f"{seen.get(strategy, 0)} 条")


def verify_window_and_cleanup(plan_json: dict, expected: dict[str, list[dict]]) -> None:
    section("运行方式与善后处理")

    for phase in plan_json.get("phases", []):
        name = str(phase.get("phase"))
        tasks = phase.get("tasks", [])
        for actual, want in zip(tasks, expected.get(name, [])):
            tag = f"{name}[{actual.get('index')}]"
            style = str(actual.get("windowStyle"))
            if style in SHOW_WINDOW:
                check(f"{tag} wShowWindow 与运行方式一致",
                      actual.get("showWindow") == SHOW_WINDOW[style],
                      f"{style} -> {actual.get('showWindow')}")

        # 善后命令必须紧跟其源任务，且按隐藏 + 等待完成执行
        for position, actual in enumerate(tasks):
            if not actual.get("isCleanupCommand"):
                continue
            tag = f"{name}[{actual.get('index')}]"
            check(f"{tag} 善后命令紧跟源任务", position > 0,
                  f"位置={position}")
            if position > 0:
                previous = tasks[position - 1]
                check(f"{tag} 前一条不是善后命令", not previous.get("isCleanupCommand"))
                check(f"{tag} 与源任务路径相同",
                      previous.get("resolvedPath") == actual.get("resolvedPath"))
            check(f"{tag} 善后命令是删除文件", actual.get("type") == "deleteFile",
                  str(actual.get("type")))
            check(f"{tag} 善后命令按等待完成执行", actual.get("waitMode") == "infinite",
                  str(actual.get("waitMode")))


def verify_scripts(run_dir: str, plan_json: dict) -> None:
    section("阶段脚本与任务一一对应")

    for phase in plan_json.get("phases", []):
        name = str(phase.get("phase"))
        tasks = phase.get("tasks", [])
        script_path = os.path.join(run_dir, name + ".cmd")
        if not os.path.isfile(script_path):
            continue

        with open(script_path, "rb") as handle:
            raw = handle.read().decode("utf-8-sig")
        lines = [line.rstrip("\r") for line in raw.split("\n")]
        commands = [line for line in lines
                    if line.strip() and not line.lstrip().startswith("rem")
                    and not line.startswith("@echo")]
        markers = [line for line in lines if line.startswith("rem [")]

        check(f"{name}.cmd 可执行行数与任务数一致", len(commands) == len(tasks),
              f"脚本={len(commands)} 任务={len(tasks)}")
        check(f"{name}.cmd 任务标记数与任务数一致", len(markers) == len(tasks),
              f"标记={len(markers)} 任务={len(tasks)}")

        for task, line in zip(tasks, commands):
            if task.get("waitStrategy") == "noWait":
                check(f"{name}.cmd 不等待的任务用 start /b",
                      line.startswith('start "" /b '), line)
            check(f"{name}.cmd 命令与计划一致",
                  line.replace('start "" /b ', "") == task.get("commandLine"), line)


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        _say("usage: python tests/integration/verify_runplan.py plan.json run/")
        return 2

    plan_path, run_dir = argv[1], argv[2]

    if not os.path.isfile(plan_path):
        _say(f"plan not found: {plan_path}")
        return 2
    if not os.path.isdir(run_dir):
        _say(f"run directory not found: {run_dir}")
        return 2

    try:
        with open(plan_path, "rb") as handle:
            cfg = json.loads(handle.read().decode("utf-8-sig"))
    except (OSError, json.JSONDecodeError) as exc:
        _say(f"cannot read plan: {exc}")
        return 2

    _say(f"verify_runplan: {plan_path}  {run_dir}")

    plan_json = verify_layout(run_dir)
    if not plan_json:
        _say("")
        _say("RESULT: FAILED  (产物缺失)")
        return 1

    drives = plan_json.get("driveTable", {}).get("drives", [])
    expected, errors = expected_phase_tasks(cfg, drives)

    section("输入可解析性")
    check("输入计划全部可解析", not errors, "; ".join(errors) if errors else "无错误")

    verify_structure(plan_json, cfg)
    verify_counts(plan_json, expected)
    verify_paths(plan_json, expected)
    verify_wait_mapping(plan_json, expected)
    verify_window_and_cleanup(plan_json, expected)
    verify_scripts(run_dir, plan_json)

    _say("")
    if _failures:
        _say(f"RESULT: FAILED  ({len(_failures)}/{_checks} checks failed)")
        for name in _failures:
            _say(f"  - {name}")
        return 1

    _say(f"RESULT: ALL PASSED  ({_checks} checks)")
    return 0


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, OSError):
        pass
    sys.exit(main(sys.argv))

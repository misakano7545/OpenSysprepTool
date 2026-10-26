#!/usr/bin/env python3
"""verify_deploy_progress.py —— 「运行」页进度通道的验收

用法:
    python tests/integration/verify_deploy_progress.py <ospt.exe> <osptd.exe>

SC 是同进程跑封装、直接刷 `运行` 页的两条进度条；我们的部署在 `osptd` 里跑，
靠 `--progress-json` 把进度一行一个 JSON 传回界面。这条通道是运行页的唯一数据
来源，所以两头都要验：

  1. osptd 侧：真的吐出合法的事件序列（顺序、计数、最终结论都对）；
  2. ospt 侧：DeployRunner 真的能拉起子进程、按行解析、拿到完成回调。

界面本身的点击在 CI 里做不了，`ospt --deploy-smoke-test` 就是这条链路的
可自动化替身 —— 它用的是同一份 DeployRunner 代码。
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile

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


def run(args: list[str], timeout: int = 180) -> tuple[int, str]:
    completed = subprocess.run(args, capture_output=True, timeout=timeout, check=False)
    return completed.returncode, (completed.stdout + completed.stderr).decode("utf-8", "replace")


def make_plan(directory: str) -> None:
    """造一份最小运行计划：两个阶段、3 个必然成功的任务。"""
    os.makedirs(directory, exist_ok=True)
    task = {
        "type": "run",
        "commandLine": "cmd.exe /c exit 0",
        "windowStyle": "hidden",
        "waitStrategy": "wait",
    }
    plan = {
        "schemaVersion": 1,
        "driveTable": {"source": "file", "drives": []},
        "presentation": {
            "module": "console", "background": "systemDefault", "titleText": "进度验收",
            "backgroundImage": "", "imageSwitchSeconds": 10, "resolutionX": 0, "resolutionY": 0,
            "fontColor": "",
        },
        "phases": [
            {
                "phase": "preinstall", "label": "部署前", "script": "preinstall.cmd", "taskCount": 2,
                "tasks": [dict(task, index=1), dict(task, index=2)],
            },
            {
                "phase": "install", "label": "部署中", "script": "install.cmd", "taskCount": 1,
                "tasks": [dict(task, index=1)],
            },
        ],
        "taskCount": 3,
    }
    with open(os.path.join(directory, "runplan.json"), "w", encoding="utf-8") as handle:
        json.dump(plan, handle, ensure_ascii=False, indent=2)


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[union-attr]

    if len(sys.argv) < 3:
        _say("用法: python tests/integration/verify_deploy_progress.py <ospt.exe> <osptd.exe>")
        return 2
    ospt = os.path.abspath(sys.argv[1])
    osptd = os.path.abspath(sys.argv[2])
    for path in (ospt, osptd):
        if not os.path.isfile(path):
            _say(f"找不到可执行文件: {path}")
            return 2

    work = tempfile.mkdtemp(prefix="ospt-progress-")
    try:
        plan_dir = os.path.join(work, "run")
        make_plan(plan_dir)

        section("osptd --progress-json：事件序列")
        code, output = run([osptd, "--no-ui", "--progress-json", "--plan", plan_dir])
        check("部署退出码 0", code == 0, f"exit={code}\n{output[-300:]}")

        events = []
        for line in output.splitlines():
            line = line.strip()
            if not line.startswith("{"):
                continue
            try:
                events.append(json.loads(line))
            except json.JSONDecodeError:
                check("进度行都是合法 JSON", False, line[:120])

        kinds = [event.get("event") for event in events]
        check("有 phaseStart 事件", kinds.count("phaseStart") == 2, str(kinds))
        check("有 taskStart 事件", kinds.count("taskStart") == 3, str(kinds))
        check("有 taskFinish 事件", kinds.count("taskFinish") == 3, str(kinds))
        check("有恰好一条 result 事件", kinds.count("result") == 1, str(kinds))
        check("result 是最后一条", kinds[-1:] == ["result"], str(kinds[-2:]))

        result = events[-1] if events else {}
        check("result 报告成功", result.get("ok") is True, str(result))
        check("result 报告 3 个任务已执行", result.get("executed") == 3, str(result))
        check("result 报告 0 个失败", result.get("failed") == 0, str(result))
        check("result 带上总任务数 3", result.get("totalTasks") == 3, str(result))

        # 每个 phaseStart 必须带阶段名与序号，运行页靠它显示中文阶段
        phases = [event for event in events if event.get("event") == "phaseStart"]
        check("phaseStart 带 phase 名",
              all(event.get("phase") in ("preinstall", "install") for event in phases), str(phases))
        check("phaseStart 序号从 1 开始",
              [event.get("phaseOrdinal") for event in phases] == [1, 2],
              str([event.get("phaseOrdinal") for event in phases]))

        # finished 必须单调递增到总任务数，界面进度条靠它
        finished = [event.get("finished") for event in events if event.get("event") == "taskFinish"]
        check("taskFinish 的 finished 依次为 1,2,3", finished == [1, 2, 3], str(finished))

        section("ospt --deploy-smoke-test：界面侧 DeployRunner")
        code, output = run([ospt, "--deploy-smoke-test", "--plan", plan_dir])
        check("冒烟退出码 0", code == 0, f"exit={code}\n{output[-400:]}")
        check("冒烟报告 SMOKE-OK", "RESULT: SMOKE-OK" in output, output[-300:])
        check("冒烟确认子进程起来了", "started=1" in output, output[-300:])
        check("冒烟确认拿到完成回调", "完成回调=1" in output, output[-300:])
        check("冒烟确认收到最终结果", "result=1" in output, output[-300:])
        # 进度事件数必须是 2 phaseStart + 3 taskStart + 3 taskFinish + 2 phaseFinish + 1 result = 11
        check("冒烟收到 11 条进度事件", "events=11" in output, output[-300:])

        section("坏计划要明确失败，不能装作跑完了")
        broken_dir = os.path.join(work, "broken")
        os.makedirs(broken_dir, exist_ok=True)
        code, output = run([ospt, "--deploy-smoke-test", "--plan", broken_dir])
        check("没有 runplan.json 时冒烟失败", code != 0, f"exit={code}")
        check("失败原因指向读不到计划",
              "runplan" in output.lower() or "计划" in output, output[-300:])

        section("汇总")
        _say("")
        if _failures:
            _say("RESULT: FAILED（%d 项失败 / %d 项检查）" % (len(_failures), _checks))
            for name in _failures:
                _say(f"  - {name}")
            return 1
        _say(f"RESULT: ALL PASSED（{_checks} 项检查，0 项跳过）")
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())

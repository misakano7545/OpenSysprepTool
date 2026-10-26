#!/usr/bin/env python3
"""verify_deploy_exec.py —— osptd 的真实执行语义验收

用法:
    python tests/integration/verify_deploy_exec.py [<ospt.exe>] [<osptd.exe>]

干跑（--dry-run）只能证明「打印得对」，证明不了「等待策略真的生效」。这个脚本
用**无害命令**（cmd /c exit、ping、echo 重定向）真跑一遍，逐一验证：

  1. --dry-run 逐阶段打印，且没有任何副作用
  2. 等待策略真的把 dwCreationFlags 传给了 CreateProcess：
     不等待 -> not-waited（句柄立刻关掉）
     等待完成 -> exited + 退出码
  3. 超时按 timeoutAction 分流，两种结果都写进日志与报告：
     kill -> timeout-killed，continue -> timeout-continued
  4. 任务失败时中止后续任务与阶段，并留下反序清单
  5. 执行真的改动了文件系统（否则上面的结论都没有意义）

不碰任何系统路径，不需要管理员权限。
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import peinfo  # noqa: E402  # noqa: E402  （同目录的共享 PE 工具）
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


def system32(name: str) -> str:
    root = os.environ.get("SystemRoot", r"C:\Windows")
    return os.path.join(root, "System32", name)


def task(phase: str, target: str, arguments: str, wait_mode: str, wait_seconds: int = 0,
         timeout_action: str = "continue") -> dict:
    return {
        "phase": phase,
        "type": "run",
        "target": target,
        "arguments": arguments,
        "windowStyle": "hidden",   # 别弹窗
        "waitMode": wait_mode,
        "waitSeconds": wait_seconds,
        "cleanup": "keep",
        "searchScope": "systemPath",
        "expectedVolumeLabel": "",
        "timeoutAction": timeout_action,
    }


def write_config(path: str, tasks: list[dict]) -> None:
    payload = {
        "schemaVersion": 1,
        "systemInfo": {"computerName": "PC-", "computerNameMode": "prefixRandom"},
        "deploy": {"userName": "User", "userGroup": "Administrators"},
        "sysprep": {"autoLogon": True, "autoLogonCount": 1, "afterAction": "shutdown"},
        "runPlan": tasks,
    }
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2)


def generate(ospt: str, cfg_path: str, run_dir: str) -> tuple[int, str]:
    return run([ospt, "--gen-runplan", cfg_path, "--out", run_dir])


def deploy(osptd: str, run_dir: str, extra: list[str]) -> tuple[int, str]:
    return run([osptd, "--plan", run_dir] + extra)


def load_report(path: str) -> dict | None:
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8-sig") as handle:
        return json.load(handle)


def main(argv: list[str]) -> int:
    # GitHub 的 windows runner 上 Python 的 stdout 是 cp1252，打印中文会直接抛
    # UnicodeEncodeError 并让整步挂掉（而且 Traceback 走 stderr，Tee-Object 抓不到，
    # 日志里只剩一句「Process completed with exit code 1」）。脚本自己切到 UTF-8。
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[union-attr]

    ospt = argv[1] if len(argv) > 1 else os.path.join(REPO, "build", "bin", "Release", "ospt.exe")
    osptd = argv[2] if len(argv) > 2 else os.path.join(REPO, "build", "bin", "Release", "osptd.exe")
    work = os.path.join(REPO, "build", "m5exec")

    _say(f"verify_deploy_exec: ospt={ospt}  osptd={osptd}")

    section("前置：产物")
    if not check("ospt.exe 存在", os.path.isfile(ospt), ospt):
        return 1
    if not check("osptd.exe 存在", os.path.isfile(osptd), osptd):
        return 1

    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work, exist_ok=True)

    cmd = system32("cmd.exe")
    ping = system32("ping.exe")
    marker = os.path.join(work, "marker.txt")
    nope = os.path.join(work, "nope.txt")
    nope2 = os.path.join(work, "nope2.txt")

    # ---------------------------------------------------------------- 场景 A
    section("场景 A：正常执行 + 两种超时结果")
    run_a = os.path.join(work, "run-a")
    cfg_a = os.path.join(work, "cfg-a.json")
    write_config(cfg_a, [
        task("preinstall", cmd, "/c exit 0", "wait"),
        # waitSeconds 的单位是**秒**（core/runplan 里 *1000 变成 timeoutMs）
        task("preinstall", ping, "-n 6 127.0.0.1", "custom", 1, "kill"),
        task("preinstall", ping, "-n 6 127.0.0.1", "custom", 1, "continue"),
        task("install", cmd, f'/c echo deployed> "{marker}"', "wait"),
        task("install", cmd, "/c exit 0", "noWait"),
    ])

    code, output = generate(ospt, cfg_a, run_a)
    check("--gen-runplan 退出码 0", code == 0, output.strip().splitlines()[-1] if output else "")

    report_path = os.path.join(work, "dryrun.json")
    code, output = run([osptd, "--dry-run", "--plan", run_a, "--report", report_path])
    check("--dry-run 退出码 0", code == 0, output.strip().splitlines()[-1] if output else "")
    check("--dry-run 打印了全部 5 个阶段",
          all(f"[{i}/5]" in output for i in range(1, 6)),
          "缺少阶段标题")
    check("--dry-run 说明了将要执行的动作",
          "运行程序" in output and "等待：" in output and "标志：" in output)
    check("--dry-run 明确声明没有启动进程", "没有启动任何进程" in output)
    check("--dry-run 没有副作用：marker.txt 未生成", not os.path.isfile(marker), marker)

    dry = load_report(report_path)
    if dry is None:
        check("dry-run 报告可解析", False, report_path)
        return 1
    check("dry-run 报告可解析", True)
    check("dry-run 报告 phaseCount=5", dry.get("phaseCount") == 5, str(dry.get("phaseCount")))
    check("dry-run 报告 taskCount=5", dry.get("taskCount") == 5, str(dry.get("taskCount")))
    waits = [t.get("waitMode") for p in dry.get("phases", []) for t in p.get("tasks", [])]
    check("dry-run 报告带上解析后的等待模式",
          waits.count("infinite") == 2 and waits.count("timeout") == 2 and waits.count("none") == 1,
          str(waits))
    check("dry-run 报告带上创建标志",
          any(t.get("creationFlagsName") == "CREATE_NEW_PROCESS_GROUP"
              for p in dry.get("phases", []) for t in p.get("tasks", [])))

    log_a = os.path.join(work, "exec-a.log")
    exec_report = os.path.join(work, "exec-a.json")
    code, output = deploy(osptd, run_a, ["--log", log_a, "--report", exec_report])
    check("真实执行退出码 0", code == 0,
          output.strip().splitlines()[-1] if output else "")

    result = load_report(exec_report)
    if result is None:
        check("执行报告可解析", False, exec_report)
        return 1
    check("执行报告可解析", True)

    check("报告 ok=true", result.get("ok") is True)
    check("5 个任务全部执行", result.get("executedTasks") == 5, str(result.get("executedTasks")))
    check("失败数为 0", result.get("failedTasks") == 0, str(result.get("failedTasks")))

    outcomes = result.get("outcomes", [])
    check("报告里 5 条任务结果齐全", len(outcomes) == 5, str(len(outcomes)))
    modes = [o.get("waitResult") for o in outcomes]
    check("等待完成的任务给出 exited",
          modes[0] == "exited" and outcomes[0].get("exitCode") == 0, str(modes[0]))
    check("超时 + kill 给出 timeout-killed", modes[1] == "timeout-killed", str(modes[1]))
    check("超时 + continue 给出 timeout-continued", modes[2] == "timeout-continued", str(modes[2]))
    check("不等待的任务给出 not-waited", modes[4] == "not-waited", str(modes[4]))
    check("不等待的任务拿到了 pid（确实启动了）", outcomes[4].get("pid", 0) > 0,
          str(outcomes[4].get("pid")))
    check("被终止的超时任务耗时接近 1 秒",
          700 <= outcomes[1].get("elapsedMs", 0) <= 4000, str(outcomes[1].get("elapsedMs")))
    check("被终止的超时任务写了终止退出码 1",
          outcomes[1].get("exitCode") == 1, str(outcomes[1].get("exitCode")))

    check("执行真的改动了文件系统：marker.txt 已生成", os.path.isfile(marker), marker)
    if os.path.isfile(marker):
        with open(marker, encoding="utf-8", errors="replace") as handle:
            check("marker.txt 内容正确", "deployed" in handle.read())

    if not check("日志文件已生成", os.path.isfile(log_a), log_a):
        return 1
    with open(log_a, encoding="utf-8", errors="replace") as handle:
        log_text = handle.read()
    check("日志里留下 timeout-killed 的证据", "timeout-killed" in log_text)
    check("日志里留下 timeout-continued 的证据", "timeout-continued" in log_text)
    check("日志两条超时都带警告级别", log_text.count("WARN") >= 2, str(log_text.count("WARN")))
    check("日志记录了阶段开始与结束", "开始：" in log_text and "阶段结束" in log_text)
    check("日志记录了系统与 Win7 支持判定", "是否满足最低目标" in log_text)

    # ---------------------------------------------------------------- 场景 B
    section("场景 B：任务失败 -> 中止后续并留反序清单")
    run_b = os.path.join(work, "run-b")
    cfg_b = os.path.join(work, "cfg-b.json")
    write_config(cfg_b, [
        task("preinstall", cmd, "/c exit 3", "wait"),
        task("preinstall", cmd, f'/c echo nope> "{nope}"', "wait"),
        task("install", cmd, f'/c echo nope2> "{nope2}"', "wait"),
    ])

    code, output = generate(ospt, cfg_b, run_b)
    check("--gen-runplan 退出码 0", code == 0, output.strip()[-200:] if output else "")

    log_b = os.path.join(work, "exec-b.log")
    report_b = os.path.join(work, "exec-b.json")
    code, output = deploy(osptd, run_b, ["--log", log_b, "--report", report_b])
    check("执行失败时退出码为 1", code == 1, str(code))

    result_b = load_report(report_b)
    if result_b is None:
        check("失败报告可解析", False, report_b)
        return 1
    check("失败报告可解析", True)

    check("报告 ok=false", result_b.get("ok") is False)
    check("只执行了 1 个任务（后面的都没跑）",
          result_b.get("executedTasks") == 1, str(result_b.get("executedTasks")))
    check("失败数为 1", result_b.get("failedTasks") == 1, str(result_b.get("failedTasks")))
    check("中止位置指向失败任务",
          result_b.get("abortedAtPhase") == "preinstall" and result_b.get("abortedAtIndex") == 1,
          f"{result_b.get('abortedAtPhase')} #{result_b.get('abortedAtIndex')}")

    outcomes_b = result_b.get("outcomes", [])
    check("失败任务的退出码被记录下来",
          outcomes_b and outcomes_b[0].get("exitCode") == 3,
          str(outcomes_b[0].get("exitCode") if outcomes_b else None))
    check("非 0 退出码判为失败", outcomes_b and outcomes_b[0].get("ok") is False)

    rollback = result_b.get("rollbackRecord", [])
    check("留下了已执行任务的反序清单（1 条）", len(rollback) == 1, str(rollback))
    check("清单里能看出是哪条命令", rollback and "exit 3" in rollback[0], str(rollback))

    check("中止后第二个任务没有执行", not os.path.isfile(nope), nope)
    check("中止后下一阶段没有执行", not os.path.isfile(nope2), nope2)

    with open(log_b, encoding="utf-8", errors="replace") as handle:
        log_b_text = handle.read()
    check("日志记录了中止原因与位置", "执行已中止" in log_b_text and "#1" in log_b_text)

    # ---------------------------------------------------------------- 汇总
    _say("")
    if _failures:
        _say(f"RESULT: FAILED  ({len(_failures)}/{_checks} checks failed)")
        for name in _failures:
            _say(f"  - {name}")
        return 1

    _say(f"RESULT: ALL PASSED  ({_checks} checks)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

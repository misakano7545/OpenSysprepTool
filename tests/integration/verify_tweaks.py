#!/usr/bin/env python3
"""verify_tweaks.py —— M10 系统优化表与一键修复验收

用法:
    python tests/integration/verify_tweaks.py <ospt.exe>

为什么需要它：优化项全部是注册表写入，最容易出的问题是「数据抄错」和
「修复了不该修的项」。所以这里不只数条数，还要：
    - 核对从 SC 二进制提取的 27 条确实都在（条数与抽样内容）
    - --repair 只碰 optimization.sysTweaks 的动作（不许顺手改别的字段）
    - 真实执行后再体检，必须从「未优化」变成「已优化」，然后回滚还原
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile

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


def load_checks(path: str) -> dict:
    with open(path, encoding="utf-8") as handle:
        data = json.load(handle)
    return {item["id"]: item for item in data.get("checks", [])}


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[union-attr]

    if len(sys.argv) < 2:
        _say("用法: python tests/integration/verify_tweaks.py <ospt.exe>")
        return 2

    ospt = os.path.abspath(sys.argv[1])
    if not os.path.isfile(ospt):
        _say(f"找不到 ospt.exe: {ospt}")
        return 2

    work = tempfile.mkdtemp(prefix="ospt-tweaks-")
    try:
        section("体检覆盖 SC 优化表")
        report = os.path.join(work, "checkup.json")
        code, output = run([ospt, "--checkup", "--json", report])
        check("--checkup 退出码 0", code == 0, f"exit={code}\n{output[-300:]}")
        if not os.path.isfile(report):
            check("生成了体检 JSON", False, report)
            return 1
        checks = load_checks(report)

        tweak_ids = [k for k in checks if k.startswith("tweak.")]
        check("优化项条数是 SC 表的 27 条", len(tweak_ids) == 27, f"{len(tweak_ids)} 条")

        # 抽样核对：这几条的值是从 SC 二进制逐字提取的，错一个字符就说明数据被改过
        samples = {
            "tweak.ioPageLockLimit": "134217728",
            "tweak.bsodNoReboot": "0",
            "tweak.defaultTtl": "64",
            "tweak.win32PriorityControl": "38",
            "tweak.maxCachedIcons": "2000",
        }
        for tweak_id, expected in samples.items():
            item = checks.get(tweak_id)
            if item is None:
                check(f"{tweak_id} 出现在体检里", False)
                continue
            check(
                f"{tweak_id} 期望值是 {expected}",
                expected in item.get("message", ""),
                item.get("message", "")[:120],
            )

        # 能检查的项都必须带 autoFix = 自己的 id，否则「一键修复」修不了它；
        # 读不到的项（ACL 只给 SYSTEM）必须**没有** autoFix —— 修不了就不许声称能修
        readable = [k for k in tweak_ids if checks[k].get("evaluated", True)]
        missing_autofix = [k for k in readable if checks[k].get("autoFix") != k]
        check("能检查的优化项都标了 autoFix", not missing_autofix, ",".join(missing_autofix[:4]))
        unreadable = [k for k in tweak_ids if not checks[k].get("evaluated", True)]
        bad_fix = [k for k in unreadable if checks[k].get("autoFix")]
        check("读不到的项没有标 autoFix", not bad_fix, ",".join(bad_fix[:3]))

        # 需要人工处理的项不许标 autoFix（例如杀软、克隆版、BitLocker）
        manual = ["antivirus", "cloneIndicator", "bitlocker", "pendingServicing"]
        bad = [k for k in manual if k in checks and checks[k].get("autoFix")]
        check("需人工处理的项没有标 autoFix", not bad, ",".join(bad))

        section("一键修复：预览与边界")
        code, output = run([ospt, "--repair", "--dry-run"])
        check("--repair --dry-run 退出码 0", code == 0, f"exit={code}\n{output[-300:]}")
        check("预览不写注册表（有『以上为预览』）", "以上为预览" in output, output[-200:])
        # 关键边界：一键修复只处理优化项，不许把启动菜单超时一起改掉
        check("不包含启动菜单超时动作", "bootMenuTimeoutSec" not in output or "bcdedit" not in output,
              "一键修复不该改 BCD")
        check("列出了可修项", "[可修]" in output or "无需修复" in output, output[-200:])

        code, output = run([ospt, "--repair", "--dry-run", "--execute"])
        check("--dry-run 与 --execute 同给时退出码 2", code == 2, f"exit={code}")

        section("真实修复 + 回滚（需要管理员）")
        # 非管理员会话下 HKLM 写入会失败，这是预期；如实 SKIP 而不是假装通过
        probe = os.path.join(work, "probe.json")
        code, output = run([ospt, "--repair", "--execute", "--journal", os.path.join(work, "j.json"),
                            "--report", probe])
        if "拒绝访问" in output or "ACCESS_DENIED" in output or "需要管理员" in output or code == 1 and "拒绝" in output:
            skip("真实修复", "当前会话不是管理员，HKLM 写入被拒（CI 的提权步骤会覆盖这一段）")
        else:
            check("--repair --execute 退出码 0", code == 0, f"exit={code}\n{output[-400:]}")
            check("给出 SC 的完成文案", "优化完成，已修复" in output, output[-200:])

            journal = os.path.join(work, "j.json")
            # 只断言日志存在，不去 open() 读它 —— 提权进程写出的文件属主是管理员，
            # 测试进程读它会被拒绝访问（实测 PermissionError）。回滚由 ospt 自己执行，
            # 它读得动自己的日志；「是否真的还原」用复查体检来证明，比读文件更硬。
            check("生成了回滚日志", os.path.isfile(journal), journal)
            if os.path.isfile(journal):
                # 复查：之前「未优化」的项现在应当变成已优化
                after = os.path.join(work, "after.json")
                run([ospt, "--checkup", "--json", after])
                after_checks = load_checks(after)
                # 只要求「能检查且命中」的项被修好；读不到的项（如 ACL 只给 SYSTEM 的
                # SecurePipeServers\winreg）本就无法判定，不能算修复失败
                still = [
                    k for k in tweak_ids
                    if after_checks.get(k, {}).get("detected") and after_checks.get(k, {}).get("evaluated", True)
                ]
                check("修复后全部转为已优化", not still, f"仍待修 {len(still)} 项：{','.join(still[:5])}")

                # 读不到的项必须显式标成「未检查」，不能被当成「已修好」
                unevaluated = [k for k in tweak_ids if not after_checks.get(k, {}).get("evaluated", True)]
                check(
                    "读不到的项如实标为未检查",
                    all("无法判定" in after_checks[k].get("message", "") for k in unevaluated),
                    ",".join(unevaluated[:3]),
                )

                # 回滚
                code, output = run([ospt, "--rollback", journal, "--execute"])
                check("--rollback --execute 退出码 0", code == 0, f"exit={code}\n{output[-300:]}")

                # 回滚后再复查：应当回到修复前的「未优化」状态
                restored = os.path.join(work, "restored.json")
                run([ospt, "--checkup", "--json", restored])
                restored_checks = load_checks(restored)
                back = [k for k in tweak_ids if restored_checks.get(k, {}).get("detected")]
                check("回滚后待修项恢复", len(back) >= len([k for k in tweak_ids if checks[k].get("detected")]) - 1,
                      f"回滚后待修 {len(back)} 项")

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

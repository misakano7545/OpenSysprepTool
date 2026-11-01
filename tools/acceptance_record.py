#!/usr/bin/env python3
"""acceptance_record.py —— 真机验收记录的唯一写入入口

为什么要这个脚本
----------------
宪章 §8 M8 要求「Win7 SP1 / Win10 22H2 / Win11 23H2 × x86 / x64 各跑一遍完整封装流程」。
2026-11-01 之前的做法是：`verify_release.py` 里写死一张表，六格永远标「真机项待人工」，
三个版本的 json 都是 9,015 字节。那不是记录，是免责声明。

现在的规矩：**没有记录就是没跑过**，矩阵会照实写「未执行」。
把某一格从「未执行」变成「已通过」，只有这一条路 —— 跑完之后用本脚本写一条记录。

一条记录必须带齐五项，缺一项就写不进去：
    os        六格之一（Windows 7 SP1 / Windows 10 22H2 / Windows 11 23H2）
    arch      x64 或 x86
    result    pass 或 fail
    machine   真机/虚拟机的具体标识（不能写「本地」这种没法复核的东西）
    evidence  证据文件路径，必须真实存在且非空（日志/截图说明/录屏文件名都行）
另外自动带上：当前 HEAD 的 sha（矩阵发版时会校验它是不是 tag 的祖先）、时间戳、操作者。

用法:
    python tools/acceptance_record.py --os "Windows 7 SP1" --arch x64 --result pass \\
        --machine "VMware 17 / Win7SP1-x64" --operator 你的名字 \\
        --evidence evidence/win7-x64-deploy.log --notes "五阶段跑完，进桌面"
    python tools/acceptance_record.py --list
退出码: 0 写入成功 / 1 校验失败 / 2 用法错误
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from datetime import datetime, timezone

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
STORE = os.environ.get(
    "OSPT_ACCEPTANCE_STORE", os.path.join(REPO, "docs", "acceptance", "real-machine.json")
)

MATRIX_OSES = ("Windows 7 SP1", "Windows 10 22H2", "Windows 11 23H2")
ARCHES = ("x64", "x86")
RESULTS = ("pass", "fail")

# 这些词写进 --machine 就等于没写：无法复核到底是什么环境。
# 只做**全等**匹配 —— 用 startswith 会把 "VMware 17 / Win7SP1-x64" 这种正当标识误杀
# （第一版就这么错的，当场被自己的检查拦下来）。
VAGUE_MACHINE_WORDS = ("本地", "本机", "我的电脑", "local", "localhost", "测试机", "vm", "虚拟机")


def _say(text: str = "") -> None:
    sys.stdout.write(text + "\n")


def head_sha() -> str:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=REPO, capture_output=True, text=True
        )
    except OSError:
        return ""
    return result.stdout.strip() if result.returncode == 0 else ""


def load_store() -> dict:
    if not os.path.isfile(STORE):
        return {"schemaVersion": 1, "records": []}
    try:
        with open(STORE, encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, json.JSONDecodeError) as exc:
        _say(f"现有记录文件读不动：{exc}")
        raise SystemExit(2)
    if isinstance(data, list):
        return {"schemaVersion": 1, "records": data}
    data.setdefault("records", [])
    return data


def validate(args: argparse.Namespace) -> list[str]:
    problems: list[str] = []
    if args.os not in MATRIX_OSES:
        problems.append(f"--os 必须是 {'/'.join(MATRIX_OSES)} 之一，收到「{args.os}」")
    if args.arch not in ARCHES:
        problems.append(f"--arch 必须是 {'/'.join(ARCHES)} 之一，收到「{args.arch}」")
    if args.result not in RESULTS:
        problems.append(f"--result 必须是 pass / fail 之一，收到「{args.result}」")

    machine = args.machine.strip()
    if len(machine) < 8:
        problems.append("--machine 太短：至少要能认出是哪台机器（≥8 字符）")
    lowered = machine.lower()
    if lowered in VAGUE_MACHINE_WORDS:
        problems.append(f"--machine 写「{machine}」等于没写，请给出可复核的标识（如 VMware 17 / Win7SP1-x64）")

    if not args.evidence.strip():
        problems.append("--evidence 必填：没有证据的记录不算记录")
    elif not os.path.isfile(args.evidence):
        problems.append(f"--evidence 指向的文件不存在：{args.evidence}")
    elif os.path.getsize(args.evidence) == 0:
        problems.append(f"--evidence 是空文件：{args.evidence}")

    if not args.operator.strip():
        problems.append("--operator 必填：谁跑的这件事必须留名")

    if not head_sha():
        problems.append("拿不到 git HEAD：记录必须绑定一个 sha，否则将来无法核对它属于哪个版本")

    return problems


def cmd_record(args: argparse.Namespace) -> int:
    problems = validate(args)
    if problems:
        _say("记录被拒绝写入，原因：")
        for item in problems:
            _say(f"  - {item}")
        return 1

    store = load_store()
    records = store["records"]
    sha = head_sha()
    stamp = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    record = {
        "os": args.os,
        "arch": args.arch,
        "result": args.result,
        "machine": args.machine.strip(),
        "operator": args.operator.strip(),
        "evidence": os.path.relpath(args.evidence, REPO).replace("\\", "/"),
        "evidenceBytes": os.path.getsize(args.evidence),
        "sha": sha,
        "recordedAtUtc": stamp,
        "notes": args.notes.strip(),
    }

    # 同一格只保留最新一条：矩阵读的是「这个版本现在是什么状态」，
    # 历史条目留在 history 里，不覆盖也不丢。
    history = store.setdefault("history", [])
    for index, existing in enumerate(list(records)):
        if existing.get("os") == record["os"] and existing.get("arch") == record["arch"]:
            history.append(existing)
            records.pop(index)
            break
    records.append(record)

    store["updatedAtUtc"] = stamp
    os.makedirs(os.path.dirname(STORE), exist_ok=True)
    with open(STORE, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(store, handle, ensure_ascii=False, indent=2)
        handle.write("\n")

    _say(f"已记录：{record['os']} × {record['arch']} = {record['result']}")
    _say(f"  机器：{record['machine']}")
    _say(f"  sha ：{sha[:12]}")
    _say(f"  证据：{record['evidence']}（{record['evidenceBytes']} 字节）")
    _say(f"  写入：{os.path.relpath(STORE, REPO)}")
    return 0


def cmd_list() -> int:
    store = load_store()
    records = store.get("records", [])
    _say(f"{os.path.relpath(STORE, REPO)}：{len(records)} 条当前记录")
    for record in records:
        _say(
            f"  {record.get('os', '?'):<16} {record.get('arch', '?'):<4} "
            f"{record.get('result', '?'):<5} {record.get('machine', '?'):<28} "
            f"{str(record.get('sha', ''))[:8]}  {record.get('recordedAtUtc', '')}"
        )
    covered = {(record.get("os"), record.get("arch")) for record in records}
    missing = [
        f"{os_name} × {arch}"
        for os_name in MATRIX_OSES
        for arch in ARCHES
        if (os_name, arch) not in covered
    ]
    if missing:
        _say("")
        _say(f"还没有记录的格（{len(missing)}/6）：")
        for item in missing:
            _say(f"  - {item}")
    else:
        _say("")
        _say("六格都有记录")
    return 0


def main(argv: list[str]) -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[union-attr]

    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("--os", default="")
    parser.add_argument("--arch", default="")
    parser.add_argument("--result", default="")
    parser.add_argument("--machine", default="")
    parser.add_argument("--operator", default="")
    parser.add_argument("--evidence", default="")
    parser.add_argument("--notes", default="")
    parser.add_argument("--list", action="store_true")
    try:
        args = parser.parse_args(argv[1:])
    except SystemExit:
        return 2

    if args.list:
        return cmd_list()
    if not args.os:
        _say("usage: python tools/acceptance_record.py --os <系统> --arch <x64|x86> --result <pass|fail> "
             "--machine <标识> --operator <名字> --evidence <文件> [--notes <说明>]")
        _say("       python tools/acceptance_record.py --list")
        return 2
    return cmd_record(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv))

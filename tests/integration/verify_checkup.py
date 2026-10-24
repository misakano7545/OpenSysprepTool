#!/usr/bin/env python3
"""M2 验收脚本：校验 ospt --checkup 产出的体检报告。

宪章 M2 要求校验脚本能证明三件事：
  1. 本机 OS 版本与 winver 一致
  2. sysprep 路径真实存在
  3. 分区容量与资源管理器一致（±1%）

winver 与资源管理器都读同一套系统 API，所以这里用等价的独立来源对照：
  - 版本：`cmd /c ver`（显示 RtlGetVersion 的真实版本）+ Python platform.version()
  - 容量：shutil.disk_usage()（与资源管理器同一底层调用）
  - sysprep：直接 os.stat 探测文件

用法: python tests/integration/verify_checkup.py out.json
退出码: 0 全部通过 / 1 有失败项 / 2 用法或读取错误
"""

from __future__ import annotations

import json
import os
import platform
import re
import shutil
import subprocess
import sys

TOLERANCE = 0.01  # 分区容量允许 1% 偏差

_failures: list[str] = []
_checks = 0


def _say(text: str = "") -> None:
    sys.stdout.write(text + "\n")


def check(name: str, condition: bool, detail: str = "") -> bool:
    """记录一条断言。condition 为 False 时计入失败。"""
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


def load_report(path: str) -> dict:
    with open(path, "rb") as handle:
        raw = handle.read()
    # ospt 以 UTF-8 写出；BOM 容忍
    text = raw.decode("utf-8-sig")
    return json.loads(text)


def cmd_ver() -> str | None:
    """`cmd /c ver` 输出形如 Microsoft Windows [Version 10.0.26100.6899]。
    中文系统上是 [版本 10.0.26200.9711]，所以只认方括号里的版本号，不认英文单词。
    """
    try:
        completed = subprocess.run(
            ["cmd", "/c", "ver"],
            capture_output=True,
            text=True,
            timeout=15,
            encoding="oem",
            errors="replace",
        )
    except (OSError, LookupError, subprocess.SubprocessError):
        return None
    if completed.returncode != 0:
        return None
    match = re.search(r"\[\D*?(\d+\.\d+\.\d+(?:\.\d+)?)\]", completed.stdout)
    return match.group(1) if match else None


def verify_structure(report: dict) -> None:
    section("结构完整性")
    check("schemaVersion == 1", report.get("schemaVersion") == 1, str(report.get("schemaVersion")))
    check("generatedAtUtc 非空", bool(report.get("generatedAtUtc")), report.get("generatedAtUtc", ""))
    check("hostName 非空", bool(report.get("hostName")), report.get("hostName", ""))

    required_keys = [
        "os",
        "sysprep",
        "partitions",
        "requiredFiles",
        "cloneIndicators",
        "isLikelyClone",
        "antivirus",
        "startupItems",
        "warnings",
    ]
    for key in required_keys:
        check(f"顶层键存在: {key}", key in report)

    os_info = report.get("os", {})
    for key in ["productName", "version", "architecture", "major", "minor", "build", "is64BitOs"]:
        check(f"os.{key} 存在", key in os_info)

    sysprep = report.get("sysprep", {})
    for key in ["available", "exePath", "imageState", "sysprepPending", "generalizationState"]:
        check(f"sysprep.{key} 存在", key in sysprep)

    antivirus = report.get("antivirus", {})
    check("antivirus.queried 是布尔", isinstance(antivirus.get("queried"), bool))
    check("antivirus.products 是数组", isinstance(antivirus.get("products"), list))


def verify_os_version(report: dict) -> None:
    section("验收 1/3：OS 版本与 winver 一致")
    os_info = report.get("os", {})
    reported = str(os_info.get("version", ""))

    ver_text = cmd_ver()
    if ver_text is None:
        _say("  SKIP  `cmd /c ver` 不可用，改用 platform 对照")
    else:
        check(
            "report os.version == cmd /c ver",
            reported == ver_text,
            f"report={reported} ver={ver_text}",
        )

    py_version = platform.version()
    check(
        "report 主次版本与 platform.version() 一致",
        py_version.startswith(f"{os_info.get('major')}.{os_info.get('minor')}"),
        f"platform={py_version} report={os_info.get('major')}.{os_info.get('minor')}",
    )

    # 内部版本号必须落在 Python 报的 build 上（platform.version() 形如 10.0.26100）
    parts = py_version.split(".")
    if len(parts) >= 3 and parts[2].isdigit():
        check(
            "report os.build == platform 的 build",
            int(parts[2]) == int(os_info.get("build", -1)),
            f"platform={parts[2]} report={os_info.get('build')}",
        )

    check("os.version 是四段数字", bool(re.fullmatch(r"\d+\.\d+\.\d+\.\d+", reported)), reported)
    check(
        "os.architecture 合法",
        os_info.get("architecture") in {"x64", "x86", "arm64", "unknown"},
        str(os_info.get("architecture")),
    )
    expected_64 = os_info.get("architecture") in {"x64", "arm64"}
    check("is64BitOs 与 architecture 自洽", os_info.get("is64BitOs") is expected_64)


def verify_sysprep(report: dict) -> None:
    section("验收 2/3：sysprep 路径真实存在")
    sysprep = report.get("sysprep", {})
    exe_path = str(sysprep.get("exePath", ""))

    check("exePath 非空", bool(exe_path), exe_path)
    check("exePath 是绝对路径", os.path.isabs(exe_path), exe_path)
    check(
        "exePath 指向 System32\\Sysprep\\sysprep.exe",
        exe_path.lower().replace("/", "\\").endswith("system32\\sysprep\\sysprep.exe"),
        exe_path,
    )

    exists = os.path.isfile(exe_path)
    check("sysprep.exe 在磁盘上真实存在", exists, exe_path)
    check("report 的 available 与实际存在一致", bool(sysprep.get("available")) == exists)

    if exists:
        size = os.path.getsize(exe_path)
        check("sysprep.exe 体积 > 0", size > 0, f"{size} B")

    state = sysprep.get("generalizationState")
    check("generalizationState 是整数", isinstance(state, int), str(state))
    check("sysprepPending 是布尔", isinstance(sysprep.get("sysprepPending"), bool))
    check("oobeInProgress 是布尔", isinstance(sysprep.get("oobeInProgress"), bool))
    # ImageState 是判据来源，不能为空
    check("imageState 非空", bool(str(sysprep.get("imageState", ""))), str(sysprep.get("imageState")))


def verify_partitions(report: dict) -> None:
    section("验收 3/3：分区容量与资源管理器一致（±1%）")
    partitions = report.get("partitions", [])
    check("至少枚举到一个分区", len(partitions) > 0, f"{len(partitions)} 个")

    system_drives = [p for p in partitions if p.get("systemDrive")]
    check("恰好一个系统盘", len(system_drives) == 1, f"{len(system_drives)} 个")

    for part in partitions:
        drive = str(part.get("drive", ""))
        if not drive:
            check("分区条目有盘符", False, str(part))
            continue

        root = drive + "\\"
        try:
            usage = shutil.disk_usage(root)
        except OSError as exc:
            _say(f"  SKIP  {drive} 无法读取容量: {exc}")
            continue

        total = int(part.get("totalBytes", 0))
        free = int(part.get("freeBytes", 0))
        used = int(part.get("usedBytes", 0))

        check(f"{drive} 总容量偏差 ≤ 1%", abs(total - usage.total) <= usage.total * TOLERANCE,
              f"report={total} explorer={usage.total}")
        check(f"{drive} 可用容量偏差 ≤ 1%", abs(free - usage.free) <= usage.total * TOLERANCE,
              f"report={free} explorer={usage.free}")
        check(f"{drive} used + free == total", used + free == total, f"{used}+{free}={total}")

        if total > 0:
            expected_percent = used * 100.0 / total
            check(
                f"{drive} usedPercent 与字节数自洽",
                abs(float(part.get("usedPercent", 0.0)) - expected_percent) < 0.01,
                f"report={part.get('usedPercent'):.2f} expected={expected_percent:.2f}",
            )

        check(f"{drive} driveType 合法",
              part.get("driveType") in
              {"fixed", "removable", "cdrom", "network", "ramdisk", "unknown"},
              str(part.get("driveType")))


def verify_other_groups(report: dict) -> None:
    section("其余检查项存在性")

    files = report.get("requiredFiles", [])
    check("必备文件条目 >= 4", len(files) >= 4, f"{len(files)} 个")
    check("必备文件含 sysprep.exe",
          any("sysprep.exe" in str(f.get("path", "")).lower() for f in files))
    for item in files:
        if item.get("exists"):
            check(f"已存在文件的体积 > 0: {os.path.basename(str(item.get('path', '')))}",
                  int(item.get("sizeBytes", 0)) > 0)

    indicators = report.get("cloneIndicators", [])
    check("克隆版指标 >= 4", len(indicators) >= 4, f"{len(indicators)} 个")
    check("isLikelyClone 是布尔", isinstance(report.get("isLikelyClone"), bool))

    antivirus = report.get("antivirus", {})
    if antivirus.get("queried"):
        check("WMI 查询成功时 products 是数组", isinstance(antivirus.get("products"), list))
        for product in antivirus.get("products", []):
            check("杀软条目有 displayName", bool(product.get("displayName")))
    else:
        _say(f"  SKIP  杀软 WMI 未查询成功（{antivirus.get('error', '原因未知')}）—— 非阻断项")

    startup = report.get("startupItems", [])
    check("startupItems 是数组", isinstance(startup, list))
    for item in startup:
        check("启动项含 scope/name/command",
              bool(item.get("scope")) and bool(item.get("name")) and bool(item.get("command")))

    check("warnings 是数组", isinstance(report.get("warnings"), list))


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        _say("usage: python tests/integration/verify_checkup.py <out.json>")
        return 2

    path = argv[1]
    if not os.path.isfile(path):
        _say(f"report not found: {path}")
        return 2

    try:
        report = load_report(path)
    except (OSError, json.JSONDecodeError) as exc:
        _say(f"cannot read report: {exc}")
        return 2

    _say(f"verify_checkup: {path}")
    _say(f"host={report.get('hostName')} generatedAt={report.get('generatedAtUtc')}")

    verify_structure(report)
    verify_os_version(report)
    verify_sysprep(report)
    verify_partitions(report)
    verify_other_groups(report)

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

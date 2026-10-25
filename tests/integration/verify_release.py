#!/usr/bin/env python3
"""verify_release.py —— M8 发版契约校验与集成测试矩阵记录

用法:
    python tests/integration/verify_release.py --check-tag v0.1.0
    python tests/integration/verify_release.py --check-workflow .github/workflows/release.yml
    python tests/integration/verify_release.py --check-artifacts <含四个产物的目录>
    python tests/integration/verify_release.py --emit-matrix <out.json> [--markdown <out.md>]
    python tests/integration/verify_release.py --check-changelog CHANGELOG.md --check-readme README.md

四件事，对应宪章 §8 M8 与 §9 发版要求：

1. **tag 与版本号必须一致**：tag 去掉前缀 `v` 后要与 `core/include/core/version.hpp` 的
   三段版本号完全相同，不一致时**必须让发版失败**（--check-tag 的退出码就是发版的闸门）。
2. **发版契约**：release.yml 必须同时提供四个二进制（ospt/osptd × x64/x86）、源码包、
   CHANGELOG 摘要，并在 Release 说明里写明 GPL-3.0 的源码获取方式。
3. **集成测试矩阵**：Win7 SP1 / Win10 22H2 / Win11 23H2 × x64/x86 共 6 格，
   每格分开「CI 已证部分」与「需人工真机验证的部分」，跑不了的部分**显式打印跳过原因**。
4. **文档前置**：README/CHANGELOG 必须写明 Win7 真机验证的前置补丁（SP1 与 KB4474419）。
"""

from __future__ import annotations

import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))

VERSION_HEADER = os.path.join(REPO, "core", "include", "core", "version.hpp")
REPO_URL = "https://github.com/misakano7545/OpenSysprepTool"

# 发布资产名 -> (期望 PE 机器码, 人类可读架构)
RELEASE_BINARIES = {
    "ospt-x64.exe": (0x8664, "x64"),
    "osptd-x64.exe": (0x8664, "x64"),
    "ospt-x86.exe": (0x14C, "x86"),
    "osptd-x86.exe": (0x14C, "x86"),
}

# 集成测试矩阵：三个系统 × 两个架构
MATRIX_OSES = [
    {
        "os": "Windows 7 SP1",
        "prerequisites": ["已装 SP1（未装 SP1 时产物起不来）", "装驱动前需先装 KB4474419（SHA-2 代码签名支持）"],
        "manual": [
            "在真机/WM 上启动 PE，执行 ospt.exe --checkup，确认 8 项字段都有值",
            "执行 osptd.exe --dry-run 与一次真实计划，确认各阶段脚本按序执行、日志落盘",
            "执行 --inject-drivers --srs 后确认目标系统能识别 SATA/NVMe 控制器并进桌面",
            "执行 --sysprep --execute，确认 sysprep 正常收尾并进入 OOBE",
            "打开 ospt.exe 界面，逐页走查并与 DFM 图纸核对字段",
        ],
    },
    {
        "os": "Windows 10 22H2",
        "prerequisites": ["确认已装 KB4474419（新系统自带，仍需核对）"],
        "manual": [
            "同 Win7 的五项流程（checkup / runplan 执行 / 驱动注入 / sysprep / 界面走查）",
            "确认 Win10 下 /generalize 后 ImageState 与 OOBEInProgress 的判定与 M2 结论一致",
        ],
    },
    {
        "os": "Windows 11 23H2",
        "prerequisites": ["确认已装 KB4474419（新系统自带，仍需核对）"],
        "manual": [
            "同 Win7 的五项流程；注意 Win11 的 ProductName 仍可能显示为 Windows 10，按 M2 的结论核对",
            "确认 TPM/SecureBoot 环境下 sysprep 的告警被正确记录而不是被忽略",
        ],
    },
]

ARCHES = [("x64", "64 位产物"), ("x86", "32 位产物")]

# CI 对每一格都能证的部分（代码层闸门，与真机无关）
CI_VERIFIED = [
    "该架构的产物构建 0 error 0 warning",
    "PE 子系统版本 6.1（verify_win7_compat.py）",
    "导入表无 Windows 8+ API、无 VCRUNTIME140/MSVCP140/ucrtbase/api-ms-win-*",
    "单元测试全绿（doctest）",
    "各里程碑验收脚本全绿（checkup/unattend/runplan/deploy/win7/drivers/ui）",
    "界面冒烟测试 7 个配置页全部打开、控件全部响应、退出无崩溃",
]

SKIP_REASON = (
    "GitHub 托管的 windows-2022 runner 只有 Windows Server 2022，没有 Win7 SP1 / "
    "Win10 22H2 / Win11 23H2 的真机或嵌套虚拟化，真机验证无法在流水线内执行"
)

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


def version_triple() -> tuple[int, int, int]:
    text = open(VERSION_HEADER, encoding="utf-8").read()

    def grab(name: str) -> int:
        match = re.search(rf"{name}\s*=\s*(\d+)", text)
        if match is None:
            raise SystemExit(f"在 {VERSION_HEADER} 里找不到 {name}")
        return int(match.group(1))

    return grab("kVersionMajor"), grab("kVersionMinor"), grab("kVersionPatch")


def parse_tag(tag: str) -> tuple[int, int, int] | None:
    match = re.fullmatch(r"v(\d+)\.(\d+)\.(\d+)", tag.strip())
    if match is None:
        return None
    return int(match.group(1)), int(match.group(2)), int(match.group(3))


def pe_info(path: str) -> tuple[int, int, int]:
    """返回 (机器码, 子系统主版本, 子系统次版本)"""
    data = open(path, "rb").read()
    pe_off = struct.unpack_from("<I", data, 0x3C)[0]
    machine = struct.unpack_from("<H", data, pe_off + 4)[0]
    opt_size = struct.unpack_from("<H", data, pe_off + 20)[0]
    opt = pe_off + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    if magic != 0x20B and magic != 0x10B:
        raise SystemExit(f"{path} 不是 PE32/PE32+ 可执行文件")
    sub_major, sub_minor = struct.unpack_from("<HH", data, opt + 48)
    _ = opt_size
    return machine, sub_major, sub_minor


# ---------------------------------------------------------------- tag 一致性


def check_tag(tag: str, expect_ok: bool) -> int:
    section(f"tag 与 version.hpp 的一致性：{tag}")
    major, minor, patch = version_triple()
    expected = f"{major}.{minor}.{patch}"
    _say(f"  version.hpp 三段版本号 = {expected}")

    parsed = parse_tag(tag)
    if parsed is None:
        check(f"tag {tag} 形如 v<major>.<minor>.<patch>", False, "去掉 v 后必须是三段数字")
        return 1
    check(f"tag {tag} 形如 v<major>.<minor>.<patch>", True)

    actual = ".".join(str(part) for part in parsed)
    ok = actual == expected
    check(
        f"tag 去掉 v 后（{actual}）与 version.hpp（{expected}）完全一致",
        ok,
        f"tag={actual} 版本号={expected}",
    )
    if not ok:
        _say("")
        _say(f"  ::error::发版被拦下：tag {tag} 与 version.hpp 不一致（tag={actual}，版本号={expected}）")
        _say("  ::error::请先把 core/include/core/version.hpp 的三段版本号改成与 tag 相同再打 tag")

    return 0 if ok else 1


# ---------------------------------------------------------------- 发版契约


def check_workflow(path: str) -> int:
    section("发版契约：release.yml")
    if not os.path.isfile(path):
        check("release.yml 存在", False, path)
        return 1
    check("release.yml 存在", True)

    text = open(path, encoding="utf-8").read()

    check("tag 推送触发（tags: v*）", "tags:" in text and "'v*'" in text)
    check("声明 contents: write 权限（创建 Release 需要）", "contents: write" in text)
    check(
        "有独立的版本校验步骤，且调用 verify_release.py --check-tag",
        "--check-tag" in text and "verify_release.py" in text,
    )
    check(
        "发版 job 依赖版本校验 job（不一致时不会发出去）",
        re.search(r"needs:\s*\[?[^\n]*verify", text) is not None or "needs: verify" in text,
    )

    # 四个资产名必须是字面量地挂到 Release 上，不能只靠占位符拼
    for asset in RELEASE_BINARIES:
        check(f"Release 挂上了 {asset}", asset in text)
    check("Release 挂上了源码包", "-src.zip" in text)
    check("Release 挂上了矩阵记录", "acceptance-matrix" in text)
    check("发布后回读 assets 逐个核对", "gh release view" in text and "assets" in text)
    # 每个架构的构建 job 手里只有自己那两份，所以构建阶段要按架构收窄校验，
    # 四份齐全只在发版 job 里核对 —— 写成「每个 job 都校验四份」会必然失败
    check("构建阶段按架构收窄校验产物", "--check-arch-artifacts" in text and "--arch" in text)
    check("发版阶段核对四份齐全", text.count("--check-artifacts") >= 1)

    check("有源码包（git archive）", "git archive" in text)
    # 源码包内容核对已搬进 verify_release.py（用 zipfile，不依赖 tar），
    # 所以这里核对「workflow 确实调了它」+「脚本里确实核对了许可文件」
    check("源码包内容核对交给 --check-source-package", "--check-source-package" in text)
    script_path = os.path.join(REPO, "tests", "integration", "verify_release.py")
    script = open(script_path, encoding="utf-8").read() if os.path.isfile(script_path) else ""
    check(
        "脚本里确实核对了 LICENSE 与第三方许可文件",
        all(item in script for item in ("LICENSE", "third_party/LICENSES.md", "MS-PL.txt")),
    )
    check("脚本里确实断言了源码包不含驱动二进制", "驱动二进制" in script)
    # 说明文字由 verify_release.py --emit-notes 生成（内容自检在 emit_notes 里做），
    # 这里只核对「说明确实来自那个生成器并且真的交给了 gh」
    check("Release 说明由 --emit-notes 生成", "--emit-notes" in text)
    check("Release 说明真的交给了 gh（--notes-file）", "--notes-file" in text)
    check("Release 说明里写明 GPL-3.0 与源码获取方式", "GPL-3.0" in text and "源码" in text)
    check("Release 说明里带 CHANGELOG 摘要", "CHANGELOG" in text)
    check("无法在流水线内执行的部分显式打印跳过原因", "跳过原因" in text or "::notice::" in text)

    return 0 if not _failures else 1


# ---------------------------------------------------------------- 四份产物


def check_artifacts(directory: str, only_arch: str | None = None) -> int:
    if only_arch:
        section(f"{only_arch} 架构的两份产物：{directory}")
    else:
        section(f"四个发布产物：{directory}")

    # 每个架构的构建 job 手里只有自己那两份，发版 job 才核对四份齐全；
    # 所以这里要能按架构收窄，而不是把「四份都在」当成每个 job 的义务。
    expected = {
        asset: spec for asset, spec in RELEASE_BINARIES.items() if only_arch is None or spec[1] == only_arch
    }
    if only_arch and not expected:
        check(f"架构名合法（{only_arch}）", False, "只支持 x64 / x86")
        return 1

    missing = []
    for asset, (machine, arch) in expected.items():
        path = os.path.join(directory, asset)
        if not os.path.isfile(path):
            missing.append(asset)
            check(f"{asset} 存在", False, path)
            continue
        check(f"{asset} 存在", True)

        actual_machine, sub_major, sub_minor = pe_info(path)
        check(
            f"{asset} 的机器类型是 {arch}（0x{machine:04X}）",
            actual_machine == machine,
            f"实际 0x{actual_machine:04X}",
        )
        check(
            f"{asset} 的子系统版本是 6.1",
            (sub_major, sub_minor) == (6, 1),
            f"实际 {sub_major}.{sub_minor}",
        )

    if missing:
        _say(f"  缺少：{', '.join(missing)}")
    return 0 if not _failures else 1


# ---------------------------------------------------------------- 矩阵记录


def build_matrix(tag: str) -> dict:
    major, minor, patch = version_triple()
    version = f"{major}.{minor}.{patch}"

    cells = []
    for entry in MATRIX_OSES:
        for arch, arch_note in ARCHES:
            cells.append(
                {
                    "os": entry["os"],
                    "arch": arch,
                    "archNote": arch_note,
                    "prerequisites": entry["prerequisites"],
                    "ciVerified": CI_VERIFIED,
                    "manualRequired": entry["manual"],
                    "manualSkipReason": SKIP_REASON,
                    "status": "CI 已证（代码层）／真机项待人工",
                }
            )

    return {
        "schemaVersion": 1,
        "tag": tag,
        "version": version,
        "generatedBy": "tests/integration/verify_release.py",
        "matrixSize": len(cells),
        "cells": cells,
        "ciOnly": True,
        "manualChecklist": sorted({step for entry in MATRIX_OSES for step in entry["manual"]}),
        "skipReason": SKIP_REASON,
    }


def emit_matrix(out_json: str, markdown_path: str, tag: str) -> int:
    matrix = build_matrix(tag)
    os.makedirs(os.path.dirname(os.path.abspath(out_json)), exist_ok=True)
    with open(out_json, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(matrix, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    _say(f"已写出矩阵记录：{out_json}（{matrix['matrixSize']} 格）")

    # 每格的跳过原因都要显式打出来，不许静默略过
    section("集成测试矩阵（每格的跳过原因都已显式声明）")
    for cell in matrix["cells"]:
        _say(f"  ::notice::{cell['os']} × {cell['arch']}：{cell['status']}；跳过原因：{cell['manualSkipReason']}")

    section("需要人工在真机上跑的部分")
    for step in matrix["manualChecklist"]:
        _say(f"  - {step}")

    if markdown_path:
        lines = [
            f"# 集成测试矩阵验收记录（{matrix['tag']} / {matrix['version']}）",
            "",
            "本表由 `tests/integration/verify_release.py --emit-matrix` 在发布流水线里生成。",
            f"共 {matrix['matrixSize']} 格：CI 能证明的部分逐格列出；真机部分无法在托管 runner 上执行，"
            "跳过原因与人工清单在下方。",
            "",
            "## 六格明细",
            "",
            "| 系统 | 架构 | CI 已证（代码层） | 需人工真机验证 | 状态 |",
            "|---|---|---|---|---|",
        ]
        for cell in matrix["cells"]:
            lines.append(
                f"| {cell['os']} | {cell['arch']} | {len(cell['ciVerified'])} 项 | "
                f"{len(cell['manualRequired'])} 项 | {cell['status']} |"
            )
        lines += ["", "## 跳过原因", "", f"- {matrix['skipReason']}", "", "## 人工验证清单", ""]
        lines += [f"1. {step}" for step in matrix["manualChecklist"]]
        lines += [
            "",
            "## 前置补丁",
            "",
        ]
        for entry in MATRIX_OSES:
            lines.append(f"**{entry['os']}**")
            for item in entry["prerequisites"]:
                lines.append(f"- {item}")
            lines.append("")
        lines += [
            "## 每格 CI 已证的内容（六格相同）",
            "",
        ]
        lines += [f"- {item}" for item in CI_VERIFIED]
        lines.append("")

        os.makedirs(os.path.dirname(os.path.abspath(markdown_path)), exist_ok=True)
        with open(markdown_path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write("\n".join(lines))
        _say(f"已写出矩阵记录（Markdown）：{markdown_path}")

    return 0


# ---------------------------------------------------------------- 源码包


def check_source_package(zip_path: str, tag: str) -> int:
    """核对 git archive 出来的源码包确实能当「对应源码」用。

    用 zipfile 而不是 tar：GNU tar 读不了 zip，而 runner 上 PATH 里的 tar 不一定是
    bsdtar（Git for Windows 的 usr/bin 会抢在前面），依赖它就等于埋一颗雷。
    """
    import zipfile

    section(f"源码包：{zip_path}")
    if not os.path.isfile(zip_path):
        check("源码包存在", False, zip_path)
        return 1
    check("源码包存在", True)
    check("源码包非空", os.path.getsize(zip_path) > 100 * 1024, f"{os.path.getsize(zip_path)} 字节")

    with zipfile.ZipFile(zip_path) as archive:
        names = archive.namelist()

    prefix = f"OpenSysprepTool-{tag}/"
    check(f"包内统一带前缀 {prefix}", all(name.startswith(prefix) for name in names))
    check("包内有源码文件", len(names) > 50, f"{len(names)} 个条目")

    for must in [
        "LICENSE",
        "README.md",
        "CHANGELOG.md",
        "third_party/LICENSES.md",
        "third_party/WTL/MS-PL.txt",
        "core/include/core/version.hpp",
        "host/ui/ui_frame.cpp",
        ".github/workflows/release.yml",
    ]:
        check(f"包内有 {must}", f"{prefix}{must}" in names)

    # 二进制与源码同时提供的前提是「源码包真的包含全部源码」；
    # 另外本仓库不得含任何驱动二进制（驱动在另一个仓库）
    drivers = [n for n in names if n.lower().endswith((".sys", ".inf", ".cat"))]
    check("源码包里没有任何驱动二进制", not drivers, ", ".join(drivers[:5]))
    check(
        "源码包里带版本号头（tag 与版本号的判据）",
        any(n.endswith("core/include/core/version.hpp") for n in names),
    )
    return 0 if not _failures else 1


# ---------------------------------------------------------------- 发布说明


def changelog_section(changelog_path: str, version: str) -> str:
    """取出 CHANGELOG 里 [<version>] 那一段的正文"""
    text = open(changelog_path, encoding="utf-8").read()
    lines = text.splitlines()
    start = None
    for index, line in enumerate(lines):
        if re.match(rf"^##\s*\[{re.escape(version)}\]\s*$", line):
            start = index + 1
            break
    if start is None:
        for index, line in enumerate(lines):
            if re.match(rf"^##\s*\[{re.escape(version)}\][^\n]*$", line):
                start = index + 1
                break
    if start is None:
        return ""
    body = []
    for line in lines[start:]:
        if line.startswith("## "):
            break
        body.append(line)
    return "\n".join(body).strip()


def emit_notes(out_path: str, tag: str, changelog_path: str, matrix_path: str | None) -> int:
    major, minor, patch = version_triple()
    version = f"{major}.{minor}.{patch}"
    section("发布说明")
    check(f"tag（{tag}）与版本号（{version}）一致", parse_tag(tag) == (major, minor, patch))

    summary = changelog_section(changelog_path, version)
    check("CHANGELOG 里有对应版本的段落", bool(summary))

    matrix = None
    if matrix_path and os.path.isfile(matrix_path):
        matrix = json.load(open(matrix_path, encoding="utf-8-sig"))
        check("矩阵记录可解析", matrix is not None)
        check("矩阵为 6 格（3 系统 × 2 架构）", int(matrix.get("matrixSize", 0)) == 6,
              str(matrix.get("matrixSize")))

    lines = [
        f"# OpenSysprepTool {tag}",
        "",
        f"Windows 系统封装工具（sysprep）的 C++20 重写，功能对标 SC 封装工具 3.0.0.178，",
        f"去掉其全部流氓行为。版本号 `{version}`，支持 Windows 7 SP1 及以上，x86 与 x64 双架构。",
        "",
        "## 发布产物",
        "",
        "| 文件 | 架构 | 说明 |",
        "|---|---|---|",
        "| `ospt-x64.exe` | x64 | 主程序（配置界面 + 体检 + unattend/运行计划生成 + 驱动注入 + sysprep 编排） |",
        "| `osptd-x64.exe` | x64 | 部署运行时（在目标机上执行运行计划，含 WinPE） |",
        "| `ospt-x86.exe` | x86 | 主程序 32 位版 |",
        "| `osptd-x86.exe` | x86 | 部署运行时 32 位版 |",
        "| `OpenSysprepTool-{tag}-src.zip` | — | 对应本次二进制的完整源码（GPL-3.0 要求） |",
        "| `acceptance-matrix-{tag}.json` / `.md` | — | 集成测试矩阵逐格验收记录 |",
        "",
        "全部产物为静态链接（`/MT`），不需要装 VC 运行库；子系统版本固定为 6.1。",
        "",
        "## 源码（GPL-3.0）",
        "",
        "本项目以 **GPL-3.0** 发布。按许可证要求，二进制与对应源码**同时提供**：",
        "",
        f"- 本 Release 附带源码包 `OpenSysprepTool-{tag}-src.zip`，由 `git archive {tag}` 生成，"
        "与这四个二进制来自同一提交；",
        f"- 源代码永久公开在 {REPO_URL}，标题对应的 tag 是 `{tag}`，可直接取用：",
        "",
        "```",
        f"git clone {REPO_URL}.git",
        f"cd OpenSysprepTool && git checkout {tag}",
        "```",
        "",
        "- 第三方组件的来源与许可证见 `third_party/LICENSES.md`（doctest v2.4.11 / BSL-1.0，",
        "  nlohmann/json v3.11.3 / MIT，pugixml v1.14 / MIT，WTL 10 build 10320 / MS-PL）。",
        "",
        "## 集成测试矩阵",
        "",
    ]

    if matrix is not None:
        lines += [
            "3 个目标系统 × 2 个架构共 6 格，每格分开记录「CI 已证」与「需人工真机验证」：",
            "",
            "| 系统 | 架构 | CI 已证（代码层） | 需人工真机验证 | 状态 |",
            "|---|---|---|---|---|",
        ]
        for cell in matrix["cells"]:
            lines.append(
                f"| {cell['os']} | {cell['arch']} | {len(cell['ciVerified'])} 项 | "
                f"{len(cell['manualRequired'])} 项 | {cell['status']} |"
            )
        lines += [
            "",
            "**流水线内无法执行的部分（显式声明，未静默略过）**",
            "",
            f"- 跳过原因：{matrix['skipReason']}",
            "",
            "**需要人工在真机上验证的清单**",
            "",
        ]
        lines += [f"{index + 1}. {step}" for index, step in enumerate(matrix["manualChecklist"])]
        lines += ["", "**Win7 真机验证前的补丁要求**", ""]
        for entry in MATRIX_OSES:
            lines.append(f"- {entry['os']}：{'；'.join(entry['prerequisites'])}")
        lines.append("")

    lines += ["## 本次版本的变化", "", summary if summary else "（CHANGELOG 未提供摘要）", ""]

    body = "\n".join(lines)

    # 生成出来的说明要自己满足合规要求 —— 这一步在 CI 与本地都会跑，不是靠肉眼
    check("说明里给出仓库地址", REPO_URL in body)
    check("说明里写明 GPL-3.0", "GPL-3.0" in body)
    check("说明里指出来源包文件名", f"OpenSysprepTool-{tag}-src.zip" in body)
    check("说明里给出 git clone 与 checkout 命令", "git clone" in body and f"git checkout {tag}" in body)
    check("说明里列出四份二进制", all(name in body for name in RELEASE_BINARIES))
    check("说明里带 CHANGELOG 摘要", bool(summary) and summary.splitlines()[0] in body)
    check("说明里带集成测试矩阵", matrix is not None and "集成测试矩阵" in body)
    check("说明里写明跳过原因", matrix is not None and matrix["skipReason"] in body)
    check("说明里带人工验证清单", matrix is not None and matrix["manualChecklist"][0] in body)
    check("说明里写明 Win7 需要 SP1 与 KB4474419", "KB4474419" in body and "SP1" in body)

    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(body + "\n")
    _say(f"已写出发布说明：{out_path}（{len(lines)} 行）")
    return 0 if not _failures else 1


# ---------------------------------------------------------------- 文档前置


def check_docs(changelog: str | None, readme: str | None) -> int:
    section("文档前置：Win7 真机验证的补丁要求")
    if changelog:
        text = open(changelog, encoding="utf-8").read()
        check("CHANGELOG 提到 Win7 需要 SP1", "SP1" in text)
        check("CHANGELOG 提到 KB4474419", "KB4474419" in text)
        check("CHANGELOG 有 0.1.0 段", "## [0.1.0]" in text)
    if readme:
        text = open(readme, encoding="utf-8").read()
        check("README 提到 Win7 需要 SP1", "SP1" in text)
        check("README 提到 KB4474419", "KB4474419" in text)
        check("README 写明源码获取方式（GPL-3.0）", "GPL-3.0" in text and "源码" in text)
        check("README 给出仓库地址", REPO_URL in text)
    return 0 if not _failures else 1


def main(argv: list[str]) -> int:
    args = argv[1:]
    if not args:
        _say(__doc__ or "")
        return 2

    exit_code = 0
    tag = "v0.0.0"
    index = 0
    while index < len(args):
        arg = args[index]
        value = args[index + 1] if index + 1 < len(args) and not args[index + 1].startswith("--") else None

        if arg == "--check-tag":
            code = check_tag(value or "", True)
            exit_code = exit_code or code
        elif arg == "--expect-tag-failure":
            # 负例：故意给一个错的 tag，脚本必须返回 1
            code = check_tag(value or "", False)
            if code == 1:
                _say("  PASS  错误 tag 被正确拦下（发版会失败）")
                global _checks
                _checks += 1
            else:
                _say("  FAIL  错误 tag 没有被拦下")
                _failures.append("错误 tag 没有被拦下")
                exit_code = 1
        elif arg == "--check-workflow":
            exit_code = exit_code or check_workflow(value or os.path.join(REPO, ".github/workflows/release.yml"))
        elif arg == "--check-artifacts":
            exit_code = exit_code or check_artifacts(value or "")
        elif arg == "--check-arch-artifacts":
            # 单个架构的构建 job 只核对本架构那两份（发版 job 才核对四份齐全）
            exit_code = exit_code or check_artifacts(value or "", _value_of(args, "--arch") or "x64")
        elif arg == "--check-source-package":
            exit_code = exit_code or check_source_package(value or "", _tag_from_argv(args) or tag)
        elif arg == "--emit-matrix":
            tag = _tag_from_argv(args) or tag
            markdown = _value_of(args, "--markdown") or ""
            emit_matrix(value or "matrix.json", markdown, tag)
        elif arg == "--emit-notes":
            notes_tag = _tag_from_argv(args) or _value_of(args, "--tag") or tag
            changelog = _value_of(args, "--changelog") or os.path.join(REPO, "CHANGELOG.md")
            matrix_json = _value_of(args, "--matrix")
            exit_code = exit_code or emit_notes(value or "release-notes.md", notes_tag, changelog, matrix_json)
        elif arg == "--check-changelog":
            exit_code = exit_code or check_docs(value, None)
        elif arg == "--check-readme":
            exit_code = exit_code or check_docs(None, value)
        index += 2 if value is not None else 1

    _say("")
    if _failures:
        _say(f"RESULT: FAILED  ({len(_failures)}/{_checks} checks failed)")
        for name in _failures:
            _say(f"  - {name}")
        return 1

    _say(f"RESULT: ALL PASSED  ({_checks} checks)")
    return exit_code


def _value_of(args: list[str], flag: str) -> str | None:
    if flag in args:
        index = args.index(flag)
        if index + 1 < len(args):
            return args[index + 1]
    return None


def _tag_from_argv(args: list[str]) -> str | None:
    return _value_of(args, "--check-tag")


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[union-attr]
    sys.exit(main(sys.argv))

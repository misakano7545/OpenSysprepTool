#!/usr/bin/env python3
"""verify_win7_compat.py —— Windows 7 SP1 兼容闸门（宪章 §8 M5.1）

用法:
    python tests/integration/verify_win7_compat.py build/bin/Release/osptd.exe
    python tests/integration/verify_win7_compat.py <x64 产物> <x86 产物>

纯静态分析 PE + 跑一次产物自己的 --win7-compat 自述，不需要 Windows 7 虚拟机，
所以能在 windows-2022 runner 上跑。

断言：
  1. PE 的 MajorSubsystemVersion.MinorSubsystemVersion == 6.1（默认停在 6.0/Vista，
     必须显式用 /SUBSYSTEM:...,6.01 钉死）
  1b. 子系统**类型**与 target 相符：ospt.exe 是 WINDOWS(2)（不分配控制台，正常启动
     只出现界面），osptd.exe 与测试产物是 CONSOLE(3)
  2. 导入表里没有已知的 Windows 8 及以上 API
  3. 导入表里没有 VCRUNTIME140 / MSVCP140 / ucrtbase / api-ms-win-*
  4. 产物里出现的每个 Win8+ API 名字，都必须出现在它自己声明的动态解析白名单里
  5. 白名单里的每一条都必须真的在产物里出现（防止名单烂掉）
  6. 产物自述的 WINVER / _WIN32_WINNT / NTDDI_VERSION 必须是 Win7 SP1 的值，
     且自述的子系统版本必须等于实测值

关于第 2 条的范围：这是**策展**名单，不是 Windows 7 的完整导出表 ——
完整拦阻靠的是编译期的 WINVER/_WIN32_WINNT（第一道闸，头文件直接拒绝 Win8+ 的
声明）。本脚本的作用是验证「编出来的二进制确实是它自称的那样」。
"""

from __future__ import annotations

import json
import os
import struct
import subprocess
import sys

# ---------------------------------------------------------------- 期望值

EXPECTED_SUBSYSTEM = (6, 1)

# 子系统**类型**：ospt.exe 是 GUI 子系统（正常启动不出现命令行窗口），
# 其余产物是控制台程序。宪章 §10 记的「/SUBSYSTEM 曾对所有 exe 写死，
# M7 改 GUI 子系统要按 target 区分」就在这里被钉住。
EXPECTED_SUBSYSTEM_KIND = {
    "ospt.exe": 2,  # WINDOWS
    "osptd.exe": 3,  # CONSOLE
    "osp_unit_tests.exe": 3,
}
SUBSYSTEM_KIND_NAME = {2: "WINDOWS(GUI)", 3: "CONSOLE"}
EXPECTED_WINVER = 0x0601
EXPECTED_WIN32_WINNT = 0x0601
EXPECTED_NTDDI_VERSION = 0x06010000

# 第三道闸：静态 /MT 不得被破坏
FORBIDDEN_DLL_EXACT = {
    "vcruntime140.dll",
    "vcruntime140_1.dll",
    "msvcp140.dll",
    "ucrtbase.dll",
}
FORBIDDEN_DLL_PREFIX = "api-ms-win-"

# 第二道闸：已知的 Windows 8 及以上才有的 API（策展名单）
WIN8_PLUS_APIS = {
    # kernel32 / kernelbase：Win8+ 的同步原语 —— VS2022 STL 对 Win7 的最大风险点
    "WaitOnAddress": "Windows 8",
    "WakeByAddressSingle": "Windows 8",
    "WakeByAddressAll": "Windows 8",
    "GetSystemTimePreciseAsFileTime": "Windows 8",
    "CreateFile2": "Windows 8",
    "CopyFile2": "Windows 8",
    "SetThreadDescription": "Windows 10 1607",
    "GetThreadDescription": "Windows 10 1607",
    "GetProcessInformation": "Windows 8.1",
    "SetProcessInformation": "Windows 8.1",
    "GetThreadInformation": "Windows 8.1",
    "SetThreadInformation": "Windows 8.1",
    "CreateWaitableTimerExW": "Windows 8",
    "LoadPackagedLibrary": "Windows 8",
    "GetPackageFullName": "Windows 8",
    "GetCurrentPackageFullName": "Windows 8",
    "GetApplicationUserModelId": "Windows 8",
    "GetSystemTimeAdjustmentPrecise": "Windows 10 2004",
    "SetSystemTimeAdjustmentPrecise": "Windows 10 2004",
    "GetFileVersionInfoExW": "Windows 8",
    "GetFileVersionInfoSizeExW": "Windows 8",
    "CreateThreadpoolTimer2": "Windows 10",
    "GetTempPath2W": "Windows 10 2004",
    # user32：DPI 感知的新接口全是 Win8.1/Win10
    "SetProcessDpiAwarenessContext": "Windows 10 1703",
    "GetDpiForWindow": "Windows 10 1607",
    "GetDpiForSystem": "Windows 10 1607",
    "GetSystemMetricsForDpi": "Windows 10 1607",
    "AdjustWindowRectExForDpi": "Windows 10 1607",
    "SetThreadDpiAwarenessContext": "Windows 10 1607",
    "GetThreadDpiAwarenessContext": "Windows 10 1607",
    "AreDpiAwarenessContextsEqual": "Windows 10 1607",
    "GetWindowDpiAwarenessContext": "Windows 10 1607",
    "GetAwarenessFromDpiAwarenessContext": "Windows 10 1607",
    "SystemParametersInfoForDpi": "Windows 10 1607",
    "EnableNonClientDpiScaling": "Windows 10 1607",
    "RegisterTouchHitTestingWindow": "Windows 8",
    "SetProcessDpiAwareness": "Windows 8.1",
    "GetDpiForMonitor": "Windows 8.1",
    "GetPointerInputTransform": "Windows 8",
    "CreateSyntheticPointerDevice": "Windows 10",
    # advapi32 / ole32 / ntdll 里的 Win8+ 项
    "EventSetInformation": "Windows 8",
    "RoInitialize": "Windows 8",
    "RoGetActivationFactory": "Windows 8",
    "RoUninitialize": "Windows 8",
    "RtlGetDeviceFamilyInfoEnum": "Windows 10",
}

# 产物里出现也算正常（不是 API 名，别误报）
SAFE_STRINGS = {
    "RtlGetVersion",  # XP 起就有，只是不是导入库函数，必须动态取
}

MACHINE_NAMES = {0x14C: "x86", 0x8664: "x64", 0xAA64: "arm64"}


# ---------------------------------------------------------------- 报告工具

_checks = 0
_failures: list[str] = []
_skips: list[str] = []


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
    _skips.append(name)
    _say(f"  SKIP  {name}  [{reason}]")


# ---------------------------------------------------------------- PE 解析


class Pe:
    def __init__(self, path: str) -> None:
        self.path = path
        self.data = open(path, "rb").read()
        self.raw = self.data
        if self.data[:2] != b"MZ":
            raise ValueError("不是 PE 文件（缺 MZ）")
        pe_off = struct.unpack_from("<I", self.data, 0x3C)[0]
        if self.data[pe_off:pe_off + 4] != b"PE\0\0":
            raise ValueError("不是 PE 文件（缺 PE\\0\\0 签名）")

        self.coff = pe_off + 4
        (self.machine, self.section_count, _stamp, _sym, _nsym, self.opt_size,
         self.characteristics) = struct.unpack_from("<HHIIIHH", self.data, self.coff)

        self.opt = self.coff + 20
        self.magic = struct.unpack_from("<H", self.data, self.opt)[0]
        self.pe32plus = self.magic == 0x20B
        if not self.pe32plus and self.magic != 0x10B:
            raise ValueError(f"未知的 optional header magic: 0x{self.magic:04X}")

        self.major_subsystem, self.minor_subsystem = struct.unpack_from(
            "<HH", self.data, self.opt + 48)

        # 子系统**类型**：2=WINDOWS(GUI) 3=CONSOLE。同一个 6.1 版本号下，
        # 类型决定了装载器会不会分配控制台（也就是有没有命令行窗口）。
        self.subsystem_kind = struct.unpack_from("<H", self.data, self.opt + 68)[0]

        if self.pe32plus:
            self.data_dirs = self.opt + 112
        else:
            self.data_dirs = self.opt + 96

        self.sections = []
        sec_off = self.opt + self.opt_size
        for i in range(self.section_count):
            base = sec_off + i * 40
            name = self.data[base:base + 8].rstrip(b"\0").decode("ascii", "replace")
            vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", self.data, base + 8)
            self.sections.append((name, vaddr, max(vsize, rawsize), rawptr))

    @property
    def machine_name(self) -> str:
        return MACHINE_NAMES.get(self.machine, f"0x{self.machine:04X}")

    @property
    def subsystem(self) -> tuple[int, int]:
        return (self.major_subsystem, self.minor_subsystem)

    def rva_to_offset(self, rva: int) -> int | None:
        for _name, vaddr, size, rawptr in self.sections:
            if vaddr <= rva < vaddr + size:
                return rawptr + (rva - vaddr)
        return None

    def _cstring(self, offset: int) -> str:
        end = self.data.index(b"\0", offset)
        return self.data[offset:end].decode("ascii", "replace")

    def imports(self) -> dict[str, list[str]]:
        """返回 {dll 名: [函数名, ...]}"""
        imp_rva, _imp_size = struct.unpack_from("<II", self.data, self.data_dirs + 8)
        result: dict[str, list[str]] = {}
        if imp_rva == 0:
            return result

        base = self.rva_to_offset(imp_rva)
        if base is None:
            return result

        index = 0
        while True:
            entry = base + index * 20
            oft, _tstamp, _fwd, name_rva, first_thunk = struct.unpack_from("<IIIII", self.data, entry)
            if name_rva == 0:
                break
            name_off = self.rva_to_offset(name_rva)
            dll = self._cstring(name_off) if name_off is not None else f"<rva 0x{name_rva:X}>"

            funcs: list[str] = []
            thunk_rva = oft or first_thunk
            thunk_off = self.rva_to_offset(thunk_rva)
            if thunk_off is not None:
                step = 8 if self.pe32plus else 4
                fmt = "<Q" if self.pe32plus else "<I"
                bit = 1 << (63 if self.pe32plus else 31)
                slot = 0
                while True:
                    value = struct.unpack_from(fmt, self.data, thunk_off + slot * step)[0]
                    if value == 0:
                        break
                    if value & bit:
                        funcs.append(f"#{value & 0xFFFF}")
                    else:
                        hint_name = self.rva_to_offset(value)
                        if hint_name is not None:
                            funcs.append(self._cstring(hint_name + 2))
                    slot += 1
                    if slot > 100000:  # 防御性上限
                        break

            result[dll] = funcs
            index += 1
            if index > 1000:
                break

        return result

    def ascii_strings(self, min_length: int = 6) -> set[str]:
        """扫描整个文件里的可打印 ASCII 串（够用：符号名与 API 名都是 ASCII）"""
        found: set[str] = set()
        current = bytearray()
        for byte in self.data:
            if 0x20 <= byte < 0x7F:
                current.append(byte)
            else:
                if len(current) >= min_length:
                    found.add(current.decode("ascii"))
                current = bytearray()
        if len(current) >= min_length:
            found.add(current.decode("ascii"))
        return found


# ---------------------------------------------------------------- 自述交叉比对


def self_report(path: str) -> dict | None:
    """跑 <pe> --win7-compat --json。跑不动或不是本项目的产物就返回 None。"""
    try:
        completed = subprocess.run([path, "--win7-compat", "--json"],
                                   capture_output=True, timeout=30, check=False)
    except (OSError, subprocess.SubprocessError):
        return None

    if completed.returncode != 0:
        return None

    text = completed.stdout.decode("utf-8", "replace").strip()
    start = text.find("{")
    if start < 0:
        return None
    try:
        return json.loads(text[start:])
    except json.JSONDecodeError:
        return None


def parse_hex(text: str) -> int | None:
    try:
        return int(text, 16)
    except (TypeError, ValueError):
        return None


def verify_file(path: str) -> None:
    section(f"产物 {path}")

    if not os.path.isfile(path):
        check("产物存在", False, path)
        return
    check("产物存在", True)

    try:
        pe = Pe(path)
    except (ValueError, OSError, struct.error) as exc:
        check("PE 可解析", False, str(exc))
        return
    check("PE 可解析", True)

    _say(f"  机器类型 = {pe.machine_name} (0x{pe.machine:04X})"
         f"{'  PE32+' if pe.pe32plus else '  PE32'}")
    _say(f"  子系统版本 = {pe.major_subsystem}.{pe.minor_subsystem}")

    # 第 1 条：子系统版本
    check(f"子系统的版本是 {EXPECTED_SUBSYSTEM[0]}.{EXPECTED_SUBSYSTEM[1]}",
          pe.subsystem == EXPECTED_SUBSYSTEM,
          f"实测 {pe.major_subsystem}.{pe.minor_subsystem}")

    # 第 1b 条：子系统类型（决定有没有命令行窗口）
    want_kind = EXPECTED_SUBSYSTEM_KIND.get(os.path.basename(path))
    if want_kind is not None:
        check(
            f"{os.path.basename(path)} 的子系统类型是 {SUBSYSTEM_KIND_NAME[want_kind]}（{want_kind}）",
            pe.subsystem_kind == want_kind,
            f"实测 {pe.subsystem_kind}（{SUBSYSTEM_KIND_NAME.get(pe.subsystem_kind, '?')}）",
        )

    # 第 3 条：运行库
    imports = pe.imports()
    _say(f"  导入 DLL ({len(imports)}) = {', '.join(sorted(imports))}")

    check("导入表非空（链接成功）", len(imports) > 0)

    lowered = {dll.lower(): dll for dll in imports}
    bad_exact = sorted(d for d in lowered if d in FORBIDDEN_DLL_EXACT)
    check("没有 VCRUNTIME140 / MSVCP140 / ucrtbase 的动态依赖",
          not bad_exact, ", ".join(bad_exact))

    bad_prefix = sorted(d for d in lowered if d.startswith(FORBIDDEN_DLL_PREFIX))
    check("没有 api-ms-win-* 的动态依赖", not bad_prefix, ", ".join(bad_prefix))

    # 第 2 条：导入表里不得出现 Win8+ API
    static_win8: list[str] = []
    for dll, funcs in imports.items():
        for func in funcs:
            if func in WIN8_PLUS_APIS:
                static_win8.append(f"{dll}!{func}（{WIN8_PLUS_APIS[func]}）")
    check("导入表里没有 Windows 8 及以上才有的 API",
          not static_win8, "; ".join(static_win8))

    strings = pe.ascii_strings()

    # 自述交叉比对
    report = self_report(path)
    if report is None:
        skip("自述值与实测值交叉比对", "该产物不支持 --win7-compat（非 osptd 或非本项目产物）")
    else:
        section(f"自述交叉比对 {path}")

        compile_time = report.get("compileTime", {})
        winver = parse_hex(compile_time.get("WINVER", ""))
        win32winnt = parse_hex(compile_time.get("_WIN32_WINNT", ""))
        ntddi = parse_hex(compile_time.get("NTDDI_VERSION", ""))

        check(f"自述 WINVER == 0x{EXPECTED_WINVER:04X}", winver == EXPECTED_WINVER,
              str(compile_time.get("WINVER")))
        check(f"自述 _WIN32_WINNT == 0x{EXPECTED_WIN32_WINNT:04X}",
              win32winnt == EXPECTED_WIN32_WINNT, str(compile_time.get("_WIN32_WINNT")))
        check(f"自述 NTDDI_VERSION == 0x{EXPECTED_NTDDI_VERSION:08X}",
              ntddi == EXPECTED_NTDDI_VERSION, str(compile_time.get("NTDDI_VERSION")))

        expected = report.get("expected", {})
        declared_subsystem = (expected.get("subsystemMajor"), expected.get("subsystemMinor"))
        check("自述的期望子系统版本等于 PE 实测值",
              declared_subsystem == pe.subsystem,
              f"自述 {declared_subsystem} vs 实测 {pe.subsystem}")

        declared_forbidden = {d.lower() for d in report.get("forbiddenRuntimeDlls", [])}
        check("自述的禁止运行库名单覆盖 PE 里实际禁止的那几个",
              FORBIDDEN_DLL_EXACT.issubset(declared_forbidden),
              ", ".join(sorted(FORBIDDEN_DLL_EXACT - declared_forbidden)))

        whitelist = report.get("dynamicApiWhitelist", [])
        declared_procs = {(item.get("module", "").lower(), item.get("proc", ""))
                          for item in whitelist}
        _say(f"  动态解析白名单 ({len(declared_procs)} 条): "
             + (", ".join(f"{m}!{p}" for m, p in sorted(declared_procs)) or "（空）"))

        # 第 4 条：产物里出现的 Win8+ 名字必须都在白名单里
        declared_names = {p for _m, p in declared_procs}
        undeclared = sorted(
            name for name in WIN8_PLUS_APIS
            if name in strings and name not in declared_names and name not in SAFE_STRINGS
        )
        check("产物里出现的每个 Win8+ API 名字都已登记进白名单",
              not undeclared, ", ".join(undeclared))

        # 第 5 条：白名单里的每一条都必须真有调用点
        stale = sorted(name for name in declared_names if name not in strings)
        check("白名单里的每条都真的在产物里出现（没有烂名单）",
              not stale, ", ".join(stale))

        # 白名单的每条都填了回退路径 —— 这是「留 Win7 回退」的书面证据
        missing_fallback = sorted(
            item.get("proc", "") for item in whitelist
            if not str(item.get("fallback", "")).strip()
            or not str(item.get("since", "")).strip()
        )
        check("白名单每条都写明起始系统与 Win7 回退路径",
              not missing_fallback, ", ".join(missing_fallback))

        resolved = set(report.get("resolvedProcs", []))
        check("自述里解析成功的 API 都在白名单里（或属于 Win7 本身就有的原生 API）",
              all(
                  any(entry.lower() == name.lower() for _m, p in declared_procs
                      for entry in [f"{_m}!{p}"])
                  or name.split("!")[-1] in SAFE_STRINGS
                  for name in resolved
              ),
              ", ".join(sorted(resolved)))

    # 只做提示：未登记的非系统 DLL
    known_dlls = {
        "kernel32.dll", "kernelbase.dll", "ntdll.dll", "advapi32.dll", "user32.dll",
        "gdi32.dll", "gdi32full.dll", "shell32.dll", "shlwapi.dll", "ole32.dll",
        "oleaut32.dll", "combase.dll", "shcore.dll", "winmm.dll", "version.dll",
        "userenv.dll", "ws2_32.dll", "sechost.dll", "rpcrt4.dll", "msvcrt.dll",
        "dbghelp.dll", "psapi.dll", "setupapi.dll", "newdev.dll", "cfgmgr32.dll",
        "wbemuuid.dll", "crypt32.dll", "bcrypt.dll", "powrprof.dll", "dwmapi.dll",
        "uxtheme.dll", "comctl32.dll", "comdlg32.dll", "imm32.dll", "mpr.dll",
        "netapi32.dll", "wintrust.dll", "wininet.dll", "iphlpapi.dll", "dnsapi.dll",
    }
    unknown = sorted(d for d in imports if d.lower() not in known_dlls)
    if unknown:
        _say(f"  提示：导入表里出现未登记的系统 DLL：{', '.join(unknown)}")


def main(argv: list[str]) -> int:
    # runner 上 stdout 可能是 cp1252，打印中文会抛 UnicodeEncodeError
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[union-attr]

    if len(argv) < 2:
        _say("usage: python tests/integration/verify_win7_compat.py <pe> [<pe> ...]")
        return 2

    _say(f"verify_win7_compat: {len(argv) - 1} 个产物")
    for path in argv[1:]:
        verify_file(path)

    _say("")
    if _failures:
        _say(f"RESULT: FAILED  ({len(_failures)}/{_checks} checks failed)")
        for name in _failures:
            _say(f"  - {name}")
        return 1

    skipped = f"，{len(_skips)} 项跳过" if _skips else ""
    _say(f"RESULT: ALL PASSED  ({_checks} checks{skipped})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

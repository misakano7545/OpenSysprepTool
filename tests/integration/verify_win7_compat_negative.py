#!/usr/bin/env python3
"""verify_win7_compat_negative.py —— 证明 win7 兼容闸门真的会失败

用法:
    python tests/integration/verify_win7_compat_negative.py build/bin/Release/osptd.exe

闸门本身全绿只能说明「没报错」，说明不了「能报错」。这里对产物做四组等长字节篡改
（不改文件结构，只改字符串/字段），逐组断言闸门退出码非 0：

  a. 子系统版本 6.1 -> 6.0
  b. 导入 DLL 名 KERNEL32.dll -> ucrtbase.dll（等长）
  c. 导入 DLL 名 ADVAPI32.dll -> api-ms-win-x（等长，命中前缀规则）
  d. 某个导入函数名 -> 等长的 Windows 8+ API 名

任何一组没能让闸门失败，本脚本就失败。
"""

from __future__ import annotations

import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
GATE = os.path.join(HERE, "verify_win7_compat.py")

# 用来做等长替换的 Windows 8+ API 名
WIN8_CANDIDATES = ["WaitOnAddress", "CreateFile2", "CopyFile2", "EventSetInformation",
                   "WakeByAddressAll", "LoadPackagedLibrary", "RoInitialize"]


def _say(text: str = "") -> None:
    print(text, flush=True)


def _pe_offsets(data: bytes) -> tuple[int, int]:
    pe_off = struct.unpack_from("<I", data, 0x3C)[0]
    opt = pe_off + 24
    opt_size = struct.unpack_from("<H", data, pe_off + 20)[0]
    magic = struct.unpack_from("<H", data, opt)[0]
    data_dirs = opt + (112 if magic == 0x20B else 96)
    return opt, data_dirs


def imported_function_names(data: bytes) -> list[str]:
    """列出导入表里的函数名（原始偏移，用于等长替换）"""
    opt, data_dirs = _pe_offsets(data)
    pe_off = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe_off + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe_off + 20)[0]
    sec_off = opt + opt_size
    sections = []
    for i in range(nsec):
        base = sec_off + i * 40
        _vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, base + 8)
        sections.append((vaddr, max(_vsize, rawsize), rawptr))

    def rva2off(rva: int) -> int | None:
        for vaddr, size, rawptr in sections:
            if vaddr <= rva < vaddr + size:
                return rawptr + (rva - vaddr)
        return None

    names: list[str] = []
    imp_rva = struct.unpack_from("<I", data, data_dirs + 8)[0]
    base = rva2off(imp_rva)
    if base is None:
        return names

    is_pe32plus = struct.unpack_from("<H", data, opt)[0] == 0x20B
    index = 0
    while True:
        entry = base + index * 20
        oft, _ts, _fwd, _name_rva, first_thunk = struct.unpack_from("<IIIII", data, entry)
        if _name_rva == 0:
            break
        thunk_off = rva2off(oft or first_thunk)
        if thunk_off is not None:
            step = 8 if is_pe32plus else 4
            fmt = "<Q" if is_pe32plus else "<I"
            bit = 1 << (63 if is_pe32plus else 31)
            slot = 0
            while True:
                value = struct.unpack_from(fmt, data, thunk_off + slot * step)[0]
                if value == 0:
                    break
                if not value & bit:
                    hint = rva2off(value)
                    if hint is not None:
                        end = data.index(b"\0", hint + 2)
                        names.append(data[hint + 2:end].decode("ascii", "replace"))
                slot += 1
        index += 1
    return names


def tamper_subsystem(data: bytearray) -> bool:
    opt, _dd = _pe_offsets(bytes(data))
    major, minor = struct.unpack_from("<HH", data, opt + 48)
    if (major, minor) != (6, 1):
        return False
    struct.pack_into("<HH", data, opt + 48, 6, 0)
    return True


def tamper_string(data: bytearray, old: str, new: str) -> bool:
    if len(old.encode()) != len(new.encode()):
        raise ValueError("必须等长替换")
    needle = old.encode()
    if data.count(needle) != 1:
        return False
    index = data.index(needle)
    data[index:index + len(needle)] = new.encode()
    return True


def run_gate(path: str) -> tuple[int, str]:
    completed = subprocess.run([sys.executable, GATE, path],
                               capture_output=True, timeout=120, check=False)
    return completed.returncode, completed.stdout.decode("utf-8", "replace")


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        _say("usage: python tests/integration/verify_win7_compat_negative.py <osptd.exe>")
        return 2

    source = argv[1]
    if not os.path.isfile(source):
        _say(f"产物不存在: {source}")
        return 2

    # 先确认原产物是绿的，否则篡改后的失败说明不了任何事
    ok_code, _ = run_gate(source)
    _say(f"原产物跑闸门: exit={ok_code}（{source}）")
    if ok_code != 0:
        _say("原产物本身就没过闸门，先修好它再谈负例")
        return 1

    original = open(source, "rb").read()
    funcs = imported_function_names(original)

    work = tempfile.mkdtemp(prefix="osp-win7-neg-")
    cases: list[tuple[str, str, bytearray]] = []

    # a. 子系统版本
    data = bytearray(original)
    if tamper_subsystem(data):
        cases.append(("子系统版本 6.1 -> 6.0", "子系统", data))
    else:
        _say("跳过 a：产物子系统版本不是 6.1")

    # b. 运行库 DLL
    data = bytearray(original)
    if tamper_string(data, "KERNEL32.dll", "ucrtbase.dll"):
        cases.append(("KERNEL32.dll -> ucrtbase.dll", "ucrtbase", data))
    else:
        _say("跳过 b：找不到唯一的 KERNEL32.dll 字符串")

    # c. api-ms-win-* 前缀
    data = bytearray(original)
    if tamper_string(data, "ADVAPI32.dll", "api-ms-win-x"):
        cases.append(("ADVAPI32.dll -> api-ms-win-x", "api-ms-win", data))
    else:
        _say("跳过 c：找不到唯一的 ADVAPI32.dll 字符串")

    # d. 等长的 Win8+ API 名
    chosen = None
    for candidate in WIN8_CANDIDATES:
        for func in funcs:
            if len(func) == len(candidate) and func != candidate:
                probe = bytearray(original)
                if tamper_string(probe, func, candidate):
                    chosen = (func, candidate, probe)
                    break
        if chosen:
            break
    if chosen:
        func, candidate, probe = chosen
        cases.append((f"导入 {func} -> {candidate}（Win8+）", "Windows 8 及以上", probe))
    else:
        _say("跳过 d：找不到等长的可替换导入函数名")

    if not cases:
        _say("没有可执行的负例，视为失败")
        return 1

    failures: list[str] = []
    for label, expect_hint, data in cases:
        target = os.path.join(work, f"tampered-{len(failures)}-{abs(hash(label)) % 10000}.exe")
        with open(target, "wb") as handle:
            handle.write(data)

        code, output = run_gate(target)
        hit = any(line.strip().startswith("FAIL") and expect_hint in line
                  for line in output.splitlines())
        if code != 0 and hit:
            _say(f"  PASS  {label}  -> 闸门 exit={code}，命中断言含「{expect_hint}」")
        elif code != 0:
            _say(f"  PASS  {label}  -> 闸门 exit={code}（未命中「{expect_hint}」字样，但确实失败了）")
        else:
            _say(f"  FAIL  {label}  -> 闸门仍然 exit=0，闸门是橡皮章")
            failures.append(label)

    shutil.rmtree(work, ignore_errors=True)

    _say("")
    if failures:
        _say(f"RESULT: FAILED  ({len(failures)}/{len(cases)} 组篡改没能让闸门失败)")
        return 1

    _say(f"RESULT: ALL PASSED  ({len(cases)} 组篡改全部被闸门拦下)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

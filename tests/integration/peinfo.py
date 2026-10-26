"""peinfo.py —— 极小的 PE 读取工具：机器类型、子系统、嵌入清单。

给闸门脚本共用，不依赖任何第三方库，也不依赖 Windows SDK 的 mt.exe
（CI 上能跑、本机也能跑）。

只做几件事，够用即止：
  * machine(path)          -> int        机器类型（0x8664 / 0x014C）
  * subsystem(path)        -> (类型, 主版本, 次版本)
  * embedded_manifest(path)-> str | None 资源类型 24、ID 1 的清单正文
  * execution_level(path)  -> str | None 清单里的 requestedExecutionLevel
  * is_elevated()          -> bool       当前进程是否已提权

PE 资源目录是三层：类型 -> 名称/ID -> 语言 -> IMAGE_RESOURCE_DATA_ENTRY。
少写一层就会永远读到「没有清单」—— 这是本项目实际踩过的坑。
"""
from __future__ import annotations

import os
import re
import struct
import subprocess

RT_MANIFEST = 24
SUBSYSTEM_WINDOWS = 2
SUBSYSTEM_CONSOLE = 3
SUBSYSTEM_NAME = {SUBSYSTEM_WINDOWS: "WINDOWS", SUBSYSTEM_CONSOLE: "CONSOLE"}


def _sections(data: bytes):
    off = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, off + 6)[0]
    opt_size = struct.unpack_from("<H", data, off + 20)[0]
    start = off + 24 + opt_size
    out = []
    for i in range(nsec):
        s = start + i * 40
        vaddr, vsize = struct.unpack_from("<II", data, s + 12)
        raw, rawsize = struct.unpack_from("<II", data, s + 20)
        out.append((vaddr, max(vsize, rawsize), raw))
    return out


def _rva_to_off(secs, rva: int) -> int:
    for vaddr, span, raw in secs:
        if vaddr <= rva < vaddr + span:
            return raw + (rva - vaddr)
    raise ValueError(f"rva 不在任何节里: 0x{rva:X}")


def _dir_entries(data: bytes, base: int):
    n_named, n_id = struct.unpack_from("<HH", data, base + 12)
    out = []
    for i in range(n_named + n_id):
        e = base + 16 + i * 8
        name_id, off_field = struct.unpack_from("<II", data, e)
        ident = name_id if (name_id & 0x80000000) == 0 else None
        out.append((ident, off_field & 0x7FFFFFFF))
    return out


def _read(path: str) -> bytes:
    with open(path, "rb") as fp:
        return fp.read()


def machine(path: str) -> int:
    data = _read(path)
    off = struct.unpack_from("<I", data, 0x3C)[0]
    return struct.unpack_from("<H", data, off + 4)[0]


def pe32plus(path: str) -> bool:
    data = _read(path)
    off = struct.unpack_from("<I", data, 0x3C)[0]
    return struct.unpack_from("<H", data, off + 24)[0] == 0x20B


def subsystem(path: str) -> tuple[int, int, int]:
    """返回 (子系统类型, 主版本, 次版本)。类型 2=WINDOWS(GUI) 3=CONSOLE。"""
    data = _read(path)
    off = struct.unpack_from("<I", data, 0x3C)[0]
    kind = struct.unpack_from("<H", data, off + 0x5C)[0]
    major, minor = struct.unpack_from("<HH", data, off + 0x48)
    return kind, major, minor


def embedded_manifest(path: str) -> str | None:
    data = _read(path)
    secs = _sections(data)
    off = struct.unpack_from("<I", data, 0x3C)[0]
    magic = struct.unpack_from("<H", data, off + 24)[0]
    dd_off = off + 24 + (112 if magic == 0x20B else 96)
    res_rva, _res_size = struct.unpack_from("<II", data, dd_off + 2 * 8)
    if not res_rva:
        return None
    res = _rva_to_off(secs, res_rva)

    for type_id, type_off in _dir_entries(data, res):
        if type_id != RT_MANIFEST:
            continue
        for name_id, name_off in _dir_entries(data, res + type_off):
            if name_id != 1:
                continue
            langs = _dir_entries(data, res + name_off)
            if not langs:
                continue
            _lang, entry_off = langs[0]
            ent_rva, ent_size = struct.unpack_from("<II", data, res + entry_off)
            raw = data[_rva_to_off(secs, ent_rva) :][:ent_size]
            if raw[:2] == b"\xff\xfe":
                return raw.decode("utf-16-le", "replace")
            return raw.decode("utf-8", "replace")
    return None


def execution_level(path: str) -> str | None:
    """清单里 requestedExecutionLevel 的 level；读不到返回 None。

    注意引号：链接器写出来的清单用的是**单引号**
    （`<requestedExecutionLevel level='requireAdministrator' .../>`），
    手工写的清单多为双引号，两种都要认。
    """
    text = embedded_manifest(path)
    if not text:
        return None
    match = re.search(r"""level\s*=\s*["']([^"']+)["']""", text)
    return match.group(1) if match else None


def is_elevated() -> bool:
    """当前进程是否已提权（管理员令牌）。

    ospt.exe / osptd.exe 的清单声明 requireAdministrator：未提权的会话根本
    启动不了它们（CreateProcess 报 740），据此可以提前给出明确原因，
    而不是让闸门看起来像产品坏了。
    """
    import sys

    if sys.platform != "win32":
        return False
    try:
        import ctypes
        from ctypes import wintypes

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        advapi32 = ctypes.WinDLL("advapi32", use_last_error=True)

        # 必须写明原型：GetCurrentProcess 返回的是伪句柄 -1，按默认的 32 位 int 传进
        # OpenProcessToken 会失败（实测提权会话里也被判成未提权）。
        kernel32.GetCurrentProcess.restype = wintypes.HANDLE
        advapi32.OpenProcessToken.argtypes = [
            wintypes.HANDLE, wintypes.DWORD, ctypes.POINTER(wintypes.HANDLE)
        ]
        advapi32.OpenProcessToken.restype = wintypes.BOOL
        advapi32.GetTokenInformation.argtypes = [
            wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD,
            ctypes.POINTER(wintypes.DWORD)
        ]
        advapi32.GetTokenInformation.restype = wintypes.BOOL
        kernel32.CloseHandle.argtypes = [wintypes.HANDLE]

        token = wintypes.HANDLE()
        if not advapi32.OpenProcessToken(
            kernel32.GetCurrentProcess(), 0x0008, ctypes.byref(token)  # TOKEN_QUERY
        ):
            return False
        try:
            elevation = wintypes.DWORD(0)
            returned = wintypes.DWORD(0)
            # 20 = TokenElevation（令牌是否已提权）；18 是 TokenElevationType，
            # 它返回的是枚举（1/2/3），恒为非零 —— 填错就变成「永远 True」。
            ok = advapi32.GetTokenInformation(
                token, 20, ctypes.byref(elevation), ctypes.sizeof(elevation),
                ctypes.byref(returned)
            )
            return bool(ok) and bool(elevation.value)
        finally:
            kernel32.CloseHandle(token)
    except Exception:
        return False


def run_text(cmd: list[str], timeout: int = 300) -> subprocess.CompletedProcess[str]:
    """跑子进程并把 stdout/stderr 解成 str。

    本仓库的产物用 UTF-8 打印（含中文）。subprocess 的 text=True 若不给 encoding，
    Python 会用系统默认编码（中文系统是 GBK，CI 的 runner 是 cp1252）去解，中文直接
    抛 UnicodeDecodeError —— M6 那次正是踩的这个。所有要读子进程文本的闸门一律走这里。
    """
    return subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
        check=False,
    )

#!/usr/bin/env python3
"""verify_ui.py —— M7b 配置页验收

用法:
    python tests/integration/verify_ui.py [<ospt.exe>]

验的是宪章 §8 M7 的字面验收：
    build/bin/Release/ospt.exe --ui-smoke-test
    → 退出码 0，且所有配置页能打开、控件能响应、退出无崩溃

外加四组约束：
  1. 页签清单与 DFM 图纸一一对应（母盘系统/目标系统/计划任务/部署过程/封装过程/运行/完成）
  2. 宪章 §7 红线涉及的控件名一个都不许出现在界面上
  3. Win7 三道闸没被破坏：把 PE 交给 verify_win7_compat.py 复核
  4. 无头环境下必须**显式**打印 skip 原因，不许静默略过（宪章 §9.7 M7）
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import peinfo  # noqa: E402  # noqa: E402  （同目录的共享 PE 工具）
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))

# 与 host/ui/ui_pages.cpp 的 pagesFromConfig 顺序一致
EXPECTED_PAGES = [
    ("system", "母盘系统", "MlSkinTabSheet1"),
    ("target", "目标系统", "MlSkinTabSheet2"),
    ("tasks", "计划任务", "MlSkinTabSheet3"),
    ("deploy", "部署过程", "MlSkinTabSheet4"),
    ("sysprep", "封装过程", "MlSkinTabSheet5"),
    ("run", "运行", "MlSkinTabSheet6"),
    ("done", "完成", "MlSkinTabSheet7"),
]

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


def imported_dlls(path: str) -> list[str]:
    import struct

    data = open(path, "rb").read()
    pe_off = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe_off + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe_off + 20)[0]
    opt = pe_off + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    data_dirs = opt + (112 if magic == 0x20B else 96)

    sec_off = opt + opt_size
    sections = []
    for i in range(nsec):
        base = sec_off + i * 40
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, base + 8)
        sections.append((vaddr, max(vsize, rawsize), rawptr))

    def rva2off(rva: int):
        for vaddr, size, rawptr in sections:
            if vaddr <= rva < vaddr + size:
                return rawptr + (rva - vaddr)
        return None

    imp_rva = struct.unpack_from("<I", data, data_dirs + 8)[0]
    base = rva2off(imp_rva)
    if base is None:
        return []

    names = []
    index = 0
    while True:
        entry = base + index * 20
        _oft, _ts, _fwd, name_rva, _ft = struct.unpack_from("<IIIII", data, entry)
        if name_rva == 0:
            break
        offset = rva2off(name_rva)
        if offset is None:
            break
        end = data.index(b"\0", offset)
        names.append(data[offset:end].decode("ascii", "replace"))
        index += 1
    return names


def appears_as_token(text: str, name: str) -> bool:
    """控件名按标识符边界匹配，避免 Ed_DeployTitle 命中 Cb_DeployTitle 这类误判"""
    return re.search(rf"(?<![A-Za-z0-9_]){re.escape(name)}(?![A-Za-z0-9_])", text) is not None


def check_interactive_ui(ospt: str) -> None:
    """无参数启动必须开出 UI 并常驻；窗口可能属于自动提权后的子进程。"""
    import ctypes
    import time

    from ctypes import wintypes

    user32 = ctypes.WinDLL("user32", use_last_error=True)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.GetWindowThreadProcessId.restype = wintypes.DWORD
    user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.GetClassNameW.restype = ctypes.c_int
    user32.IsWindowVisible.argtypes = [wintypes.HWND]
    user32.IsWindowVisible.restype = wintypes.BOOL
    user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.c_void_p]
    user32.GetWindowRect.restype = wintypes.BOOL
    user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, ctypes.c_size_t, ctypes.c_size_t]
    user32.PostMessageW.restype = wintypes.BOOL

    class RECT(ctypes.Structure):
        _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long),
                    ("right", ctypes.c_long), ("bottom", ctypes.c_long)]

    before: set[int] = set()
    found: list[tuple[int, int]] = []
    consoles: list[int] = []

    def scan(_hwnd=0, _lparam=0):
        hwnd = _hwnd
        buffer = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, buffer, 256)
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if buffer.value == "OSPTMainFrame":
            found.append((hwnd, pid.value))
        if buffer.value == "ConsoleWindowClass":
            consoles.append(hwnd)
        return True

    enum_proc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)(scan)
    user32.EnumWindows.argtypes = [ctypes.c_void_p, wintypes.LPARAM]
    user32.EnumWindows.restype = wintypes.BOOL
    user32.EnumWindows(enum_proc, 0)
    before = {hwnd for hwnd, _pid in found}
    found.clear()

    if not peinfo.is_elevated():
        check("无参数启动开出主窗口（OSPTMainFrame）", False,
              "未提权会话：启动后窗口属于提权子进程，UIPI 会拒绝本脚本发送 WM_CLOSE；"
              "请从已提权会话运行 verify_ui.py")
        return

    proc = subprocess.Popen([ospt], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    deadline = time.time() + 20
    while time.time() < deadline and not found:
        if proc.poll() is not None:
            break
        user32.EnumWindows(enum_proc, 0)
        time.sleep(0.2)

    if not found:
        code = proc.poll()
        output = ""
        if proc.stdout is not None:
            try:
                output = proc.stdout.read().decode("utf-8", "replace")
            except Exception:
                pass
        if "无窗口站" in output:
            check("无窗口站环境：无参数启动显式说明跳过原因并退出", code == 3,
                  f"code={code} / {output.strip()[:80]}")
            _say("  本机没有窗口站，无参数启动的常驻检查被显式跳过")
        else:
            check("无参数启动开出主窗口（OSPTMainFrame）", False,
                  f"启动器状态={code} 输出={output[:120]}")
        proc.kill()
        return

    new_frames = [(hwnd, pid) for hwnd, pid in found if hwnd not in before]
    check("无参数启动开出主窗口（OSPTMainFrame）", bool(new_frames))
    if not new_frames:
        proc.kill()
        return
    hwnd, owner_pid = new_frames[0]
    check("主窗口可见", bool(user32.IsWindowVisible(hwnd)))
    rect = RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    width = rect.right - rect.left
    height = rect.bottom - rect.top
    check("窗口尺寸合理（>600x400）", width > 600 and height > 400, f"{width}x{height}")
    owner_consoles = []
    for console in consoles:
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(console, ctypes.byref(pid))
        if pid.value == owner_pid:
            owner_consoles.append(console)
    check("无参数启动不附带命令行窗口", not owner_consoles,
          f"主窗口 PID={owner_pid}，控制台窗口数={len(owner_consoles)}")

    time.sleep(2.0)
    check("窗口起来 2 秒后启动器仍在等待（UI 常驻）", proc.poll() is None,
          f"启动器退出码={proc.poll()}")
    user32.PostMessageW(hwnd, 0x0010, 0, 0)
    try:
        code = proc.wait(timeout=15)
        check("关窗后启动器收到真实退出码 0", code == 0, f"code={code}")
    except subprocess.TimeoutExpired:
        check("关窗后启动器收到真实退出码 0", False, "15 秒内没退出")
        proc.kill()


def check_usage_lists_ui(ospt: str) -> None:
    """用法里必须能找到打开界面的入口，否则用户不知道界面怎么起"""
    code, output = run([ospt, "--help"])
    check("--help 退出码 0", code == 0, f"code={code}")
    check("用法里列出 --ui（打开配置界面）", "--ui" in output)
    check("用法里列出无参数直接打开界面", re.search(r"^\s*ospt\s+打开配置界面", output, re.M) is not None)
    check("用法里仍有 --ui-smoke-test", "--ui-smoke-test" in output)
    check("用法里列出 --console（显式保留命令行窗口）", "--console" in output)


def main(argv: list[str]) -> int:
    ospt = argv[1] if len(argv) > 1 else os.path.join(REPO, "build", "bin", "Release", "ospt.exe")
    report_path = os.path.join(REPO, "build", "m7b", "smoke.json")

    _say(f"verify_ui: ospt={ospt}")

    section("前置")
    if not check("ospt.exe 存在", os.path.isfile(ospt), ospt):
        return 1

    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    # 先删掉上一次的报告：否则产物没写出报告时会读到残留，闸门就成了空判
    if os.path.isfile(report_path):
        os.remove(report_path)

    section("gate：--ui-smoke-test")
    code, output = run([ospt, "--ui-smoke-test", "--report", report_path])
    check("退出码 0", code == 0, f"code={code}")

    if not os.path.isfile(report_path):
        check("生成了 JSON 报告", False, report_path)
        return 1
    check("生成了 JSON 报告", True)

    report = json.load(open(report_path, encoding="utf-8-sig"))
    skipped = bool(report.get("skipped"))

    if skipped:
        # 宪章 §9.7 M7：无头环境可以跳过，但必须显式说出原因
        reason = str(report.get("skipReason", "")).strip()
        check("跳过时给出了原因", bool(reason), reason)
        check("stdout 里出现了 SMOKE-SKIP 标记", "SMOKE-SKIP" in output)
        check("stdout 里印出了跳过原因", "SMOKE-SKIP" in output and reason[:12] in output)
        _say("  本机没有窗口站，UI 冒烟测试被显式跳过 —— 这是允许的结果，但原因已打印")
    else:
        check("报告 ok=true", report.get("ok") is True)
        check("报告 skipped=false", report.get("skipped") is False)
        checks = int(report.get("checks", 0))
        failures = int(report.get("failures", 0))
        check("检查项数 >= 100", checks >= 100, str(checks))
        check("失败项为 0", failures == 0, str(failures))
        check("stdout 里出现 RESULT: SMOKE-OK", "SMOKE-OK" in output)

    section("配置页清单与 DFM 页对象一一对应")
    pages = report.get("pages", [])
    check("页面数为 7", len(pages) == len(EXPECTED_PAGES), str(len(pages)))
    if not skipped:
        check(
            "打开的页面数为 7",
            int(report.get("pagesOpened", 0)) == len(EXPECTED_PAGES),
            str(report.get("pagesOpened")),
        )

    for index, (page_id, title, sheet) in enumerate(EXPECTED_PAGES):
        if index >= len(pages):
            check(f"页面 {page_id} 出现在报告里", False)
            continue
        page = pages[index]
        check(
            f"第 {index + 1} 页是 {title}（{sheet}）",
            page.get("id") == page_id and page.get("title") == title and page.get("dfmSheet") == sheet,
            f"{page.get('id')}/{page.get('title')}/{page.get('dfmSheet')}",
        )
        check(f"{title} 页打开了", page.get("opened") is True)
        check(
            f"{title} 页控件数与字段数一致",
            int(page.get("fields", -1)) == int(page.get("controls", -2)) and int(page.get("fields", 0)) > 0,
            f"fields={page.get('fields')} controls={page.get('controls')}",
        )

    if not skipped:
        lines = "\n".join(report.get("lines", []))
        for _page_id, title, sheet in EXPECTED_PAGES:
            check(f"冒烟记录里有「页面能打开: {title}（{sheet}）」", f"页面能打开: {title}（{sheet}）" in lines)

        # 「控件能响应」的几类证据
        for marker, expected_min in [
            ("复选框可切换: ", 15),
            ("下拉可选中: ", 10),
            ("编辑框往返一致: ", 15),
            ("按钮已点击: ", 5),
            ("进度条可读写: ", 3),
            ("列表行数与模型一致: ", 1),
        ]:
            count = lines.count(marker)
            check(f"控件响应证据「{marker.strip()}」>= {expected_min} 条", count >= expected_min, str(count))

        check("退出无崩溃的记录在报告里", "窗口销毁无崩溃" in lines)

    section("按 SC 的 DFM 坐标摆放（运行 / 完成 / 目标系统）")
    lines = report.get("lines", [])
    placed = [line for line in lines if "控件按图纸坐标摆放" in line]
    # 七个页面都按 DFM 摆：母盘系统 16 + 目标系统 34 + 计划任务 15 + 部署过程 12 +
    # 封装过程 21 + 运行 5 + 完成 8 = 111（红线项与图纸纯标签不在内）
    check("坐标核对覆盖 112 个控件（七个页面）", len(placed) == 112, f"{len(placed)} 条")
    # 逐页都要有核对记录，防止某一页悄悄退回自动排列
    for page_name, expected in (("母盘系统", 16), ("目标系统", 35), ("计划任务", 15),
                                ("部署过程", 12), ("封装过程", 21), ("运行", 5), ("完成", 8)):
        pages = [p for p in report.get("pages", []) if p.get("title") == page_name]
        if not pages:
            check(f"报告里有 {page_name} 页", False)
            continue
        check(f"{page_name} 页按 DFM 摆放", pages[0].get("dfmLayout") is True, str(pages[0].get("dfmLayout")))
    # 目标系统页的分组框是绝对矩形、组内控件相对它定位；少加分组原点会整体偏移
    target_placed = [line for line in placed if any(
        name in line for name in ("Cb_ShowDevmgmt", "Ed_Owner", "Se_Boottime", "Lab_tempX", "Btn_OemSet"))]
    check("目标系统页控件参与了坐标核对", len(target_placed) == 5, f"{len(target_placed)} 条")
    for name in ("Pic_RunGif", "Pro_Total", "Pro_Task", "Lab_Run", "Lab_Total",
                 "PIC_Success", "Lab_Complete", "PIC_Error", "Pro_End", "Lab_Countdown",
                 "Lab_CompleteErr", "Btn_Shutdown", "Btn_Restart"):
        matched = [line for line in placed if name in line]
        check(f"坐标核对了 {name}", bool(matched), "未出现在报告里")
        if matched:
            check(f"{name} 坐标判定为通过", "PASS" in matched[0] and "控件按图纸坐标摆放" in matched[0],
                  matched[0].strip()[:160])

    section("完成页的关机/重启：冒烟必须 dry-run")
    # 冒烟测试会对每个按钮发 BM_CLICK。完成页的「关机(&T)」「重启(&R)」在新版里
    # 是真执行（ExitWindowsEx），所以冒烟必须是 dry-run —— CI 的 runner 是管理员
    # 会话，真调会把机器关掉，整条流水线挂死（第一次跑就踩了）。
    check("冒烟报告里电源动作是 dry-run", report.get("powerActionsDryRun") is True,
          str(report.get("powerActionsDryRun")))
    check("点击到了关机/重启两个按钮", int(report.get("powerActionRequests") or 0) >= 2,
          str(report.get("powerActionRequests")))

    section("TMFFUN 高级设置窗体")
    tmffun = [line for line in lines if "TMFFUN 高级设置窗体" in line]
    check("冒烟覆盖了 TMFFUN 高级设置窗体", len(tmffun) == 1, f"{len(tmffun)} 条")
    if tmffun:
        check("TMFFUN 判定通过", "PASS" in tmffun[0], tmffun[0].strip()[:260])
        check("TMFFUN 客户区是 740x400", "740x400" in tmffun[0], tmffun[0].strip()[:260])
        check("TMFFUN 保存回写与大小校验都验到", "保存回写" in tmffun[0], tmffun[0].strip()[:260])
        check("TMFFUN 覆盖了预置IP/文件锁定/OEM 三页", "随机网段" in tmffun[0] and "防篡改" in tmffun[0] and "OEM" in tmffun[0], tmffun[0].strip()[:300])

    section("TAPPMSGFORM 提示框")
    msg = [line for line in lines if "TAPPMSGFORM 提示框" in line]
    check("冒烟覆盖了 TAPPMSGFORM 提示框", len(msg) == 1, f"{len(msg)} 条")
    if msg:
        check("TAPPMSGFORM 判定通过", "PASS" in msg[0], msg[0].strip()[:200])

    section("TTLMF 任务对话框")
    ttl = [line for line in lines if "TTLMF 任务对话框" in line]
    check("冒烟覆盖了 TTLMF 任务对话框", len(ttl) == 1, f"{len(ttl)} 条")
    if ttl:
        check("TTLMF 判定通过", "PASS" in ttl[0], ttl[0].strip()[:200])

    section("TSBOX 弹窗（重启方式 / 宽带创建方式）")
    tsbox = [line for line in lines if "TSBOX 弹窗" in line]
    check("冒烟覆盖了 TSBOX 两个页签", len(tsbox) == 2, f"{len(tsbox)} 条")
    for name in ("重启方式", "宽带创建方式"):
        hit = [line for line in tsbox if name in line]
        check(f"TSBOX「{name}」验过了", bool(hit), "未出现在报告里")
        if hit:
            check(f"TSBOX「{name}」判定通过", "PASS" in hit[0], hit[0].strip()[:160])

    section("宪章 §7 红线：这些控件名一个都不许出现")
    excluded = report.get("excludedControls", [])
    check("报告带上了不做的控件清单", len(excluded) >= 20, str(len(excluded)))
    for must in [
        "Gb_ScTools",
        "Mkb_Ceounion",
        "Cb_Saveiehp",
        "Cb_SaveieFavorites",
        # 任务栏固定机制已在 B9 实现（只接受用户列出的文件），不再属于「不做」
        "Rb_Sysprep_Type2",
        "Rb_Sysprep_Type3",
        "Btn_Bugs",
        "Btn_Update",
    ]:
        check(f"不做的清单里有 {must}", must in excluded)

    if not skipped:
        lines = "\n".join(report.get("lines", []))
        leaked = [name for name in excluded if appears_as_token(lines, name)]
        check("冒烟记录里没有任何红线/不做控件名", not leaked, ", ".join(leaked[:8]))

    section("交互式界面：无参数只开 UI、能常驻、能关")
    check_usage_lists_ui(ospt)
    if "--smoke-only" not in argv and not skipped:
        check_interactive_ui(ospt)
    elif "--smoke-only" in argv:
        _say("  --smoke-only：常驻关窗检查由提权子会话单独执行")
    else:
        _say("  本机没有窗口站，跳过 --ui 的常驻检查（原因已在上方打印）")

    section("约束：原生控件进了产物、且 Win7 三道闸没破")
    dlls = [d.lower() for d in imported_dlls(ospt)]
    check("导入表里有 USER32.dll（窗口层确实编进去了）", "user32.dll" in dlls, ", ".join(dlls))
    for forbidden in ["vcruntime140.dll", "msvcp140.dll", "ucrtbase.dll"]:
        check(f"没有 {forbidden}", forbidden not in dlls)

    # 「用的是原生标准控件」不靠导入表猜，而是看冒烟测试的实测结论
    if not skipped:
        lines = "\n".join(report.get("lines", []))
        check("页签控件实测为系统标准的 SysTabControl32", "页签控件是系统标准的 SysTabControl32" in lines)
        check("通用控件库 comctl32 实测已加载", "通用控件库 comctl32 已加载" in lines)

    win7_gate = os.path.join(HERE, "verify_win7_compat.py")
    if os.path.isfile(win7_gate):
        code, gate_output = run([sys.executable, win7_gate, ospt])
        check(
            "Win7 兼容闸门对 ospt.exe 仍然全绿",
            code == 0,
            gate_output.strip().splitlines()[-1] if gate_output else "",
        )

    _say("")
    if _failures:
        _say(f"RESULT: FAILED  ({len(_failures)}/{_checks} checks failed)")
        for name in _failures:
            _say(f"  - {name}")
        return 1

    _say(f"RESULT: ALL PASSED  ({_checks} checks)")
    return 0


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[union-attr]
    sys.exit(main(sys.argv))

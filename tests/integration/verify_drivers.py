#!/usr/bin/env python3
"""verify_drivers.py —— M6 驱动注入与 sysprep 编排验收

用法:
    python tests/integration/verify_drivers.py [<ospt.exe>]

三条 gate（与宪章 §8 M6 的字面验收一致）：
    ospt --inject-drivers --image <映像> --driver <标准驱动包目录> --dry-run
    ospt --inject-drivers --image <映像> --srs  <SRS 包目录>   --dry-run
    ospt --sysprep --dry-run

夹具全部在 build/m6/ 下**运行时生成**，仓库里不落任何真实驱动二进制
（宪章要求：不打包也不内嵌任何驱动文件）。合成映像里的 ntoskrnl.exe 是一个
只带 PE 头的最小文件，只为让 KB4474419 的离线判定有依据。
"""

from __future__ import annotations

import json
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
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


# ---------------------------------------------------------------- 夹具构造


def craft_kernel(path: str, major: int, minor: int) -> None:
    """只带 PE 头的最小 ntoskrnl.exe —— 够读 MajorOperatingSystemVersion。"""
    data = bytearray(0x200)
    data[0:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)          # e_lfanew
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<H", data, 0x84, 0x8664)        # machine = x64
    struct.pack_into("<H", data, 0x84 + 2, 0)         # NumberOfSections
    struct.pack_into("<H", data, 0x84 + 16, 0xF0)     # SizeOfOptionalHeader
    opt = 0x80 + 24
    struct.pack_into("<H", data, opt, 0x20B)          # PE32+
    struct.pack_into("<H", data, opt + 40, major)     # MajorOperatingSystemVersion
    struct.pack_into("<H", data, opt + 42, minor)     # MinorOperatingSystemVersion
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(data)


def write_utf16_reg(path: str, lines: list[str]) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(b"\xff\xfe")
        handle.write("\r\n".join(lines).encode("utf-16-le"))


def build_fixtures(root: str) -> dict:
    paths = {}

    # ---- 合成映像 ----
    image = os.path.join(root, "image")
    paths["image"] = image
    craft_kernel(os.path.join(image, "Windows", "System32", "ntoskrnl.exe"), 6, 1)
    os.makedirs(os.path.join(image, "Windows", "System32", "drivers"), exist_ok=True)
    os.makedirs(os.path.join(image, "Windows", "System32", "config"), exist_ok=True)
    with open(os.path.join(image, "Windows", "System32", "config", "SYSTEM"), "wb") as handle:
        handle.write(b"REGF-STUB")  # 不是真 hive：dry-run 不该碰它
    paths["hive"] = os.path.join(image, "Windows", "System32", "config", "SYSTEM")

    packages = os.path.join(image, "Windows", "servicing", "Packages")
    os.makedirs(packages, exist_ok=True)
    paths["mum"] = os.path.join(packages, "Package_for_KB4474419~31bf3856ad364e35~amd64~~6.1.1.2.mum")
    with open(paths["mum"], "w", encoding="utf-8") as handle:
        handle.write("<!-- KB4474419 占位 -->\n")

    # ---- 合成 SRS 包（裸 sys + Drv.pesrs/Drv.reg，无 inf 无 cat）----
    srs = os.path.join(root, "srs")
    paths["srs"] = srs
    for vendor, service, hwid in [
        ("StubVendor", "stubahci", "PCI#VEN_FFFF&DEV_0001&SUBSYS_00000000"),
        ("StubVendor2", "stubraid", "PCI#VEN_FFFF&DEV_0002&SUBSYS_00000000"),
    ]:
        package = os.path.join(srs, vendor)
        os.makedirs(os.path.join(package, "drivers"), exist_ok=True)
        with open(os.path.join(package, "drivers", f"{service}.sys"), "wb") as handle:
            handle.write(b"STUB-SYS-NOT-A-DRIVER")  # 合成占位，绝不是驱动

        reg_lines = [
            "Windows Registry Editor Version 5.00",
            "",
            rf"[HKEY_LOCAL_MACHINE\SYSTEM\ControlSet001\Services\{service}]",
            '"ErrorControl"=dword:00000001',
            '"Group"="System Bus Extender"',
            '"Start"=dword:00000000',
            '"Type"=dword:00000001',
            '"ImagePath"=hex(2):73,00,79,00,73,00,74,00,65,00,6D,00,33,00,32,00,5C,00,64,00,'
            "72,00,69,00,76,00,65,00,72,00,73,00,5C,00,73,00,74,00,75,00,62,00,2E,00,73,00,"
            "79,00,73,00,00,00",
            "",
            rf"[HKEY_LOCAL_MACHINE\SYSTEM\ControlSet001\Control\CriticalDeviceDatabase\{hwid}]",
            '"ClassGUID"="{4D36E97B-E325-11CE-BFC1-08002BE10318}"',
            f'"Service"="{service}"',
        ]
        write_utf16_reg(os.path.join(package, "Drv.pesrs"), reg_lines)
        write_utf16_reg(os.path.join(package, "Drv.reg"), reg_lines)

    # ---- 合成标准驱动包（inf + sys + cat）----
    vmd = os.path.join(root, "vmd")
    paths["vmd"] = vmd
    package = os.path.join(vmd, "StubIntel", "20240101")
    os.makedirs(package, exist_ok=True)
    for name, payload in [
        ("stubvd.inf", b"[Version]\nSignature=\"$WINDOWS NT$\"\n"),
        ("stubvd.sys", b"STUB-SYS-NOT-A-DRIVER"),
        ("stubvd.cat", b"STUB-CAT"),
    ]:
        with open(os.path.join(package, name), "wb") as handle:
            handle.write(payload)

    return paths


# ---------------------------------------------------------------- PE 导入表

FORBIDDEN_IN_OSPT = ["dismapi.dll", "vcruntime140.dll", "msvcp140.dll", "ucrtbase.dll"]


def imported_dlls(path: str) -> list[str]:
    data = open(path, "rb").read()
    pe_off = struct.unpack_from("<I", data, 0x3C)[0]
    machine, nsec = struct.unpack_from("<HH", data, pe_off + 4)
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


def main(argv: list[str]) -> int:
    # GitHub 的 windows runner 上 Python 的 stdout 是 cp1252，打印中文会直接抛
    # UnicodeEncodeError（这个坑 M5 已经踩过一次）。脚本自己切到 UTF-8。
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[union-attr]

    ospt = argv[1] if len(argv) > 1 else os.path.join(REPO, "build", "bin", "Release", "ospt.exe")
    root = os.path.join(REPO, "build", "m6")

    _say(f"verify_drivers: ospt={ospt}")

    section("前置")
    if not check("ospt.exe 存在", os.path.isfile(ospt), ospt):
        return 1

    shutil.rmtree(root, ignore_errors=True)
    os.makedirs(root, exist_ok=True)
    fixtures = build_fixtures(root)
    check("合成夹具已生成（无任何真实驱动）", os.path.isdir(fixtures["srs"]))

    # ---- 模式 A ----
    section("gate 1：模式 A —— 标准驱动包走 dism")
    report_a = os.path.join(root, "modeA.json")
    code, output = run([ospt, "--inject-drivers", "--image", fixtures["image"],
                        "--driver", fixtures["vmd"], "--dry-run", "--report", report_a])
    check("退出码 0", code == 0, output.strip().splitlines()[-1] if output else "")
    check("打印了完整命令行", '/image:' in output and '/add-driver:' in output and '/recurse' in output)
    check("声明不链接 DismApi.dll", "DismApi.dll" in output)

    plan_a = json.load(open(report_a, encoding="utf-8-sig"))
    check("报告 mode=A", plan_a.get("mode") == "A")
    commands = plan_a.get("commands", [])
    check("每个 .inf 生成一条 dism 命令", len(commands) == 1, str(len(commands)))
    if commands:
        line = commands[0]["commandLine"]
        # 与微软文档一致：dism /image:<挂载点> /add-driver:<inf> /recurse
        check("命令行形状与文档一致",
              '/image:"' in line and '/add-driver:"' in line and line.rstrip().endswith("/recurse"),
              line)
        check("命令行用了绝对 .inf 路径",
              commands[0]["infPath"].lower().endswith(".inf"), commands[0]["infPath"])
    check("dism 程序路径指向 Dism.exe",
          plan_a.get("dismProgram", "").lower().endswith("dism.exe"), plan_a.get("dismProgram"))

    # ---- 模式 B ----
    section("gate 2：模式 B —— SRS 裸 sys 走离线 hive")
    hive_before = open(fixtures["hive"], "rb").read()
    report_b = os.path.join(root, "modeB.json")
    code, output = run([ospt, "--inject-drivers", "--image", fixtures["image"],
                        "--srs", fixtures["srs"], "--dry-run", "--report", report_b])
    check("退出码 0", code == 0, output.strip().splitlines()[-1] if output else "")
    check("打印了每个注册表项的键路径与值",
          'HKLM\\OSPT_SRS\\ControlSet001\\Services\\' in output and '"ClassGUID"' in output
          and '"Service"' in output)

    plan_b = json.load(open(report_b, encoding="utf-8-sig"))
    check("报告 mode=B", plan_b.get("mode") == "B")
    check("默认挂载键是 OSPT_SRS", plan_b.get("mountKey") == "OSPT_SRS", str(plan_b.get("mountKey")))
    check("hive 路径正确",
          plan_b.get("hivePath", "").lower().endswith("windows\\system32\\config\\system"),
          plan_b.get("hivePath"))
    check("五步都在预告里",
          any("RegLoadKey" in s for s in plan_b.get("steps", [])) and
          any("RegUnLoadKey" in s for s in plan_b.get("steps", [])) and
          any("RegSetValueEx" in s for s in plan_b.get("steps", [])))

    services = plan_b.get("serviceOps", [])
    check("两个包各一条 Services", len(services) == 2, str(len(services)))
    for op in services:
        key = op["keyPath"]
        names = {v["name"] for v in op["values"]}
        check(f"Services 键路径正确: {op['subject']}",
              key.startswith("HKLM\\OSPT_SRS\\ControlSet001\\Services\\"), key)
        check(f"写入了 Type/Start/ImagePath: {op['subject']}",
              {"Type", "Start", "ImagePath"}.issubset(names), ", ".join(sorted(names)))
        check(f"Type 是内核驱动(1): {op['subject']}",
              any(v["name"] == "Type" and v["display"].startswith("0x00000001") for v in op["values"]))
        check(f"ImagePath 指向 system32\\drivers: {op['subject']}",
              any(v["name"] == "ImagePath" and "system32\\drivers\\" in v["display"]
                  for v in op["values"]))

    cdd = plan_b.get("cddOps", [])
    check("两条 CDD 条目", len(cdd) == 2, str(len(cdd)))
    for op in cdd:
        names = {v["name"] for v in op["values"]}
        check(f"CDD 写入了 ClassGUID 与 Service: {op['subject']}",
              {"ClassGUID", "Service"}.issubset(names), ", ".join(sorted(names)))
        check(f"CDD 键路径正确: {op['subject']}",
              "CriticalDeviceDatabase" in op["keyPath"], op["keyPath"])
        check(f"ClassGUID 是存储控制器类: {op['subject']}",
              any(v["name"] == "ClassGUID" and "4D36E97B" in v["display"] for v in op["values"]))

    copies = plan_b.get("copyOps", [])
    check("两个 .sys 待复制", len(copies) == 2, str(len(copies)))
    for copy in copies:
        check("复制目标在 Windows\\System32\\drivers 下",
              copy["to"].lower().endswith(".sys") and
              "windows\\system32\\drivers" in copy["to"].lower(), copy["to"])

    # ---- dry-run 不得有任何副作用 ----
    section("gate 2 的副作用检查：dry-run 必须什么都没做")
    check("目标 hive 字节未变", open(fixtures["hive"], "rb").read() == hive_before)
    drivers_dir = os.path.join(fixtures["image"], "Windows", "System32", "drivers")
    check("没有 .sys 被复制进映像", os.listdir(drivers_dir) == [],
          ", ".join(os.listdir(drivers_dir)))

    # ---- sysprep ----
    section("gate 3：sysprep 编排")
    report_c = os.path.join(root, "sysprep.json")
    code, output = run([ospt, "--sysprep", "--dry-run", "--image", fixtures["image"],
                        "--unattend", os.path.join(root, "unattend.xml"),
                        "--after", "shutdown", "--report", report_c])
    check("退出码 0", code == 0, output.strip().splitlines()[-1] if output else "")

    plan_c = json.load(open(report_c, encoding="utf-8-sig"))
    line = plan_c.get("commandLine", "")
    check("命令行含 /generalize", "/generalize" in line, line)
    check("命令行含 /oobe", "/oobe" in line, line)
    check("命令行含 /shutdown", "/shutdown" in line, line)
    check("命令行含 /quiet", "/quiet" in line, line)
    check("命令行含 /unattend:", "/unattend:" in line, line)
    check("程序是 sysprep.exe", plan_c.get("program", "").lower().endswith("sysprep.exe"),
          plan_c.get("program"))
    check("参数逐条列出",
          {"/generalize", "/oobe", "/shutdown", "/quiet"}.issubset(set(plan_c.get("parameters", []))),
          str(plan_c.get("parameters")))

    sha2 = plan_c.get("sha2", {})
    check("离线判定了目标系统版本", sha2.get("checked") is True, str(sha2))
    check("目标被识别为 Win7 系", sha2.get("targetIsWindows7") is True, str(sha2))
    check("版本读到 6.1", sha2.get("targetOsVersion") == "6.1", str(sha2.get("targetOsVersion")))
    check("KB4474419 被找到", sha2.get("packageFound") is True, str(sha2.get("evidence")))
    check("结论是可以安装 SHA-2 签名的驱动", sha2.get("ok") is True, str(sha2.get("note")))

    # ---- KB4474419 缺失时必须报阻塞 ----
    section("KB4474419 缺失时的行为")
    os.remove(fixtures["mum"])
    report_d = os.path.join(root, "sysprep-nopatch.json")
    code, output = run([ospt, "--sysprep", "--dry-run", "--image", fixtures["image"],
                        "--report", report_d])
    check("dry-run 仍然退出码 0（只预告）", code == 0, str(code))
    plan_d = json.load(open(report_d, encoding="utf-8-sig"))
    check("判定为不具备 SHA-2 支持", plan_d["sha2"]["ok"] is False)
    check("给出了阻塞项", len(plan_d.get("blockers", [])) >= 1, str(plan_d.get("blockers")))
    check("阻塞项点名 KB4474419", any("KB4474419" in b for b in plan_d.get("blockers", [])))

    code, output = run([ospt, "--sysprep", "--execute", "--image", fixtures["image"]])
    if os.path.isfile(plan_c.get("program", "")):
        check("真实执行因阻塞项被拒（不会真去封装）", code == 1, str(code))
        check("拒绝理由里有阻塞提示", "阻塞" in output, output.strip()[-160:])
    else:
        check("本机没有 sysprep.exe，执行被拒", code == 1, str(code))

    # ---- 约束：不链接 DismApi.dll、不打包驱动 ----
    section("约束核对")
    dlls = [d.lower() for d in imported_dlls(ospt)]
    check("ospt.exe 的导入表里没有 dismapi.dll", "dismapi.dll" not in dlls, ", ".join(dlls))
    for dll in FORBIDDEN_IN_OSPT:
        check(f"ospt.exe 不依赖 {dll}", dll not in dlls)

    bundled: list[str] = []
    for folder in ("core", "host", "deploy", "third_party", "docs"):
        base = os.path.join(REPO, folder)
        if not os.path.isdir(base):
            continue
        for dirpath, _dirnames, filenames in os.walk(base):
            for name in filenames:
                if name.lower().endswith((".sys", ".inf", ".cat", ".pesrs")):
                    bundled.append(os.path.relpath(os.path.join(dirpath, name), REPO))
    check("仓库里没有内嵌任何驱动文件（.sys/.inf/.cat/.pesrs）", not bundled, ", ".join(bundled))

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
        sys.exit(main(sys.argv))
    except Exception:  # noqa: BLE001
        # 未预期的异常：把 traceback 打到 stdout 而不是 stderr。
        # CI 步骤即使只 Tee stdout，也能看到真正的原因，而不是一个无从定位的退出码。
        import traceback

        traceback.print_exc(file=sys.stdout)
        sys.exit(3)

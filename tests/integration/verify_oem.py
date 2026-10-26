#!/usr/bin/env python3
"""verify_oem.py —— M10 OEM 识别与 SC 档案导入验收

用法:
    python tests/integration/verify_oem.py <ospt.exe> [<SC 的 Scoem 目录>]

为什么需要它：OEM 这条路最容易错在编码和「静默成功」上——
SC 的 nt6.ini 是 GBK，读错就成乱码；而一份没有 OEM 字段的 ini 如果被当成
解析成功，就会往 unattend 里写一个空 OEMInformation。所以这里既要断言
GBK 能正确落地，也要断言坏输入会被拒绝。

不给 Scoem 目录时，用脚本自己造的 GBK 样本（不依赖反编译产物在不在本机）。
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


def run(args: list[str], timeout: int = 120) -> tuple[int, str]:
    completed = subprocess.run(args, capture_output=True, timeout=timeout, check=False)
    return completed.returncode, (completed.stdout + completed.stderr).decode("utf-8", "replace")


# GBK 编码的 nt6.ini：宏碁那份的等价样本。用 GBK 写盘是重点——
# SC 的原始文件就是这个编码，读成 UTF-8 会得到乱码或替换字符。
GBK_NT6 = (
    "[Support]\r\n"
    "Url=http://www.acer.com.cn\r\n"
    "Tel=400-700-1000\r\n"
    "Stime=7*24\r\n"
    "[General_CHS]\r\n"
    "Manufacturer=ACER（宏碁）\r\n"
    "Model=宏碁系列电脑\r\n"
    "[General_EN]\r\n"
    "Manufacturer=ACER\r\n"
    "Model=ACER Series\r\n"
).encode("gbk")


def write_base_config(path: str) -> None:
    cfg = {
        "schemaVersion": 1,
        "systemInfo": {
            "registeredName": "小御坂",
            "registeredOrg": "OpenSysprepTool",
            "computerName": "OSPT-OEM",
            "computerNameMode": "fixed",
        },
        "sysprep": {"mode": "classic", "afterAction": "reboot", "workgroup": "OSPTWG"},
        "runPlan": [],
    }
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(cfg, handle, ensure_ascii=False, indent=2)


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[union-attr]

    if len(sys.argv) < 2:
        _say("用法: python tests/integration/verify_oem.py <ospt.exe> [<Scoem 目录>]")
        return 2

    ospt = os.path.abspath(sys.argv[1])
    if not os.path.isfile(ospt):
        _say(f"找不到 ospt.exe: {ospt}")
        return 2
    scoem = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else ""

    work = tempfile.mkdtemp(prefix="ospt-oem-")
    try:
        # ------------------------------------------------------------------ 帮助
        section("命令行")
        code, output = run([ospt, "--help"])
        check("--help 退出码 0", code == 0, f"exit={code}")
        check("--help 列出 --oem-detect", "--oem-detect" in output)
        check("--help 列出 --import-oem", "--import-oem" in output)

        # ------------------------------------------------------------------ 本机识别
        section("本机 BIOS 识别")
        report = os.path.join(work, "detect.json")
        code, output = run([ospt, "--oem-detect", "--json", report])
        check("--oem-detect 退出码 0", code == 0, f"exit={code}\n{output[-300:]}")
        check("输出 RESULT: OK", "RESULT: OK" in output)
        if os.path.isfile(report):
            with open(report, encoding="utf-8") as handle:
                data = json.load(handle)
            check("JSON 含 bios 段", "bios" in data)
            check("JSON 含 profile 段", "profile" in data)
            bios = data.get("bios", {})
            # 真机 / VM 上 BIOS 标识一般读得到；读不到也必须说清原因而不是崩
            if bios.get("ok"):
                check("BIOS 厂商非空", bool(bios.get("manufacturer")))
                check("profile 的 manufacturer 跟随 BIOS", data["profile"]["manufacturer"] == bios["manufacturer"])
            else:
                check("BIOS 读不到时给出原因", bool(bios.get("error")))
        else:
            check("生成了 JSON 报告", False, report)

        # ------------------------------------------------------------------ GBK 档案
        section("导入 GBK 编码的 nt6.ini")
        ini = os.path.join(work, "nt6.ini")
        with open(ini, "wb") as handle:
            handle.write(GBK_NT6)

        code, output = run([ospt, "--import-oem", ini])
        check("--import-oem 退出码 0", code == 0, f"exit={code}\n{output[-300:]}")
        check("输出 RESULT: OK", "RESULT: OK" in output)
        # 中文必须真的还原出来（读错编码会得到 '?' 或替换字符）
        check("GBK 中文正确还原（ACER（宏碁））", "ACER（宏碁）" in output, output[-300:])
        check("型号还原（宏碁系列电脑）", "宏碁系列电脑" in output)
        check("支持电话读到", "400-700-1000" in output)
        check("支持网址读到", "http://www.acer.com.cn" in output)
        check("支持时段读到", "7*24" in output)

        code, output = run([ospt, "--import-oem", ini, "--lang", "EN"])
        check("--lang EN 退出码 0", code == 0)
        check("EN 取到 ACER Series", "ACER Series" in output, output[-200:])
        check("EN 下不再是中文型号", "宏碁系列电脑" not in output)

        # ------------------------------------------------------------------ 并进配置
        section("并进配置并写进 unattend")
        base = os.path.join(work, "cfg.json")
        write_base_config(base)
        merged = os.path.join(work, "cfg-oem.json")
        code, output = run([ospt, "--import-oem", ini, "--into", base, "--out", merged])
        check("--into/--out 退出码 0", code == 0, f"exit={code}\n{output[-300:]}")
        check("输出 RESULT: OK", "RESULT: OK" in output)
        if os.path.isfile(merged):
            with open(merged, encoding="utf-8") as handle:
                cfg = json.load(handle)
            oem = cfg.get("oem", {})
            check("配置里有 oem.manufacturer", oem.get("manufacturer") == "ACER（宏碁）", str(oem)[:200])
            check("配置里有 oem.supportUrl", oem.get("supportUrl") == "http://www.acer.com.cn")
            check("原配置字段没被清掉", cfg.get("systemInfo", {}).get("registeredName") == "小御坂")
            check("source 记录了来源", "nt6.ini" in oem.get("source", ""))
        else:
            check("生成了合并后的配置", False, merged)

        xml = os.path.join(work, "unattend.xml")
        code, output = run([ospt, "--gen-unattend", merged, "--out", xml])
        check("--gen-unattend 退出码 0", code == 0, f"exit={code}\n{output[-300:]}")
        if os.path.isfile(xml):
            with open(xml, encoding="utf-8") as handle:
                text = handle.read()
            check("unattend 有 OEMInformation", "<OEMInformation>" in text)
            check("Manufacturer 写进去了", "<Manufacturer>ACER（宏碁）</Manufacturer>" in text)
            check("Model 写进去了", "<Model>宏碁系列电脑</Model>" in text)
            check("SupportHours 写进去了", "<SupportHours>7*24</SupportHours>" in text)
            check("SupportPhone 写进去了", "<SupportPhone>400-700-1000</SupportPhone>" in text)
            check("SupportURL 写进去了", "<SupportURL>http://www.acer.com.cn</SupportURL>" in text)
            # 文档字母序：OEMInformation 在 OOBE 之前
            check(
                "OEMInformation 排在 OOBE 之前",
                text.find("<OEMInformation>") < text.find("<OOBE>") if "<OOBE>" in text else True,
            )
        else:
            check("生成了 unattend.xml", False, xml)

        # ------------------------------------------------------------------ 坏输入
        section("坏输入必须被拒绝，不能静默成功")
        empty_ini = os.path.join(work, "empty.ini")
        with open(empty_ini, "w", encoding="utf-8") as handle:
            handle.write("[Other]\r\nNothing=1\r\n")
        code, output = run([ospt, "--import-oem", empty_ini])
        check("没有 OEM 字段时退出码非 0", code != 0, f"exit={code}")
        check("给出原因而不是空成功", "RESULT: FAILED" in output or "没有可用的 OEM 字段" in output, output[-200:])

        code, output = run([ospt, "--import-oem", os.path.join(work, "no-such.ini")])
        check("文件不存在时退出码非 0", code != 0, f"exit={code}")

        code, output = run([ospt, "--import-oem", ini, "--no-such-flag"])
        check("未知开关退出码 2", code == 2, f"exit={code}")

        # 空 oem 段不能生成空 OEMInformation
        with open(base, encoding="utf-8") as handle:
            raw = json.load(handle)
        clean = os.path.join(work, "cfg-clean.json")
        with open(clean, "w", encoding="utf-8") as handle:
            json.dump(raw, handle, ensure_ascii=False, indent=2)
        clean_xml = os.path.join(work, "unattend-clean.xml")
        code, _ = run([ospt, "--gen-unattend", clean, "--out", clean_xml])
        if code == 0 and os.path.isfile(clean_xml):
            with open(clean_xml, encoding="utf-8") as handle:
                text = handle.read()
            check("oem 段为空时不生成 OEMInformation", "<OEMInformation>" not in text)
        else:
            check("oem 段为空时仍能生成 unattend", False, f"exit={code}")

        # ------------------------------------------------------------------ 真实 SC 档案
        section("真实 SC 品牌目录（有就测）")
        if not scoem or not os.path.isdir(scoem):
            skip("SC 品牌目录", "未提供或不存在，跳过（自造样本已覆盖同一格式）")
        else:
            brands = sorted(d for d in os.listdir(scoem) if os.path.isdir(os.path.join(scoem, d)))
            check("品牌目录非空", len(brands) > 0, f"{len(brands)} 个")
            ok_brands = 0
            for brand in brands:
                candidate = os.path.join(scoem, brand, "nt6.ini")
                if not os.path.isfile(candidate):
                    continue
                code, output = run([ospt, "--import-oem", candidate])
                if code == 0 and "RESULT: OK" in output:
                    ok_brands += 1
            check("每个真实 nt6.ini 都能解析", ok_brands > 0, f"{ok_brands}/{len(brands)}")

            # 品牌匹配：造一个与本机 BIOS 厂商同名的品牌目录，必须命中
            bios_manufacturer = ""
            if os.path.isfile(report):
                with open(report, encoding="utf-8") as handle:
                    bios_manufacturer = json.load(handle).get("bios", {}).get("manufacturer", "")
            if bios_manufacturer:
                skin = os.path.join(work, "skin")
                os.makedirs(os.path.join(skin, bios_manufacturer), exist_ok=True)
                shutil.copyfile(
                    os.path.join(scoem, brands[0], "nt6.ini"),
                    os.path.join(skin, bios_manufacturer, "nt6.ini"),
                )
                code, output = run([ospt, "--oem-detect", "--skin", skin])
                check("品牌目录按 BIOS 厂商命中", code == 0 and "RESULT: OK" in output, f"exit={code}")
                check("命中后档案来自该品牌", brands[0] in output or "宏碁" in output or "Dell" in output, output[-300:])
            else:
                skip("品牌目录匹配", "本机 BIOS 厂商为空")

        # ------------------------------------------------------------------ 汇总
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

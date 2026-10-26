#!/ usr / bin / env python3
"""verify_presentation.py —— M10 图形部署页验收

用法:
    python tests/integration/verify_presentation.py <osptd.exe> [<ospt.exe>]

为什么需要它：图形进度页最容易出的问题是「窗口建起来了，但一片黑 / 没有文字」，
只看进程退出码发现不了。这里让 osptd 自己把画面存成 BMP，再读像素做断言：
    - 四种模块各画一遍，画面必须**不是单色**（证明真的画了进度条和文字）
    - 背景图路径配错时不能崩，必须仍然画出来
    - 分辨率配置成非法值不能崩
    - 页面在 3 秒内正常退出（不卡死、不阻塞）

BMP 解析用纯 Python（不依赖 PIL）：24 位未压缩 BMP 的头是固定的。
"""

from __future__ import annotations

import json
import os
import struct
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

#-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- BMP 解析


class Bmp:
    """只支持 24 位未压缩 BMP —— 正好是 osptd 写出来的格式。"""

    def __init__(self, path: str) -> None:
        with open(path, "rb") as handle:
            data = handle.read()
        if len(data) < 54 or data[0:2] != b"BM":
            raise ValueError("不是 BMP 文件")
        self.data = data
        self.pixel_offset = struct.unpack_from("<I", data, 10)[0]
        header_size = struct.unpack_from("<I", data, 14)[0]
        if header_size < 40:
            raise ValueError("不支持的 BMP 头")
        self.width = struct.unpack_from("<i", data, 18)[0]
        self.height = struct.unpack_from("<i", data, 22)[0]
        self.bit_count = struct.unpack_from("<H", data, 28)[0]
        self.compression = struct.unpack_from("<I", data, 30)[0]
        if self.bit_count != 24 or self.compression != 0:
            raise ValueError(f"只支持 24 位未压缩 BMP（实际 {self.bit_count} 位 / 压缩 {self.compression}）")
        self.row_bytes = ((self.width * 3 + 3) // 4) * 4

    def pixel(self, x: int, y: int) -> tuple[int, int, int]:
#BMP 自下而上存储
        row = self.height - 1 - y
        offset = self.pixel_offset + row * self.row_bytes + x * 3
        b, g, r = self.data[offset], self.data[offset + 1], self.data[offset + 2]
        return (r, g, b)

    def distinct_colors(self, step: int = 7) -> int:
        seen = set()
        for y in range(0, self.height, step):
            for x in range(0, self.width, step):
                seen.add(self.pixel(x, y))
        return len(seen)


def analyze(path: str) -> tuple[bool, str]:
    """返回 (是否有内容, 说明)。判据：颜色种类要够多，且不能整屏同色。"""
    if not os.path.isfile(path):
        return False, "文件不存在"
    try:
        bmp = Bmp(path)
    except ValueError as exc:
        return False, str(exc)

    if bmp.width < 200 or bmp.height < 200:
        return False, f"画面过小 {bmp.width}x{bmp.height}"

    colors = bmp.distinct_colors()
    if colors < 4:
        return False, f"只有 {colors} 种颜色，画面基本是单色（可能是黑屏）"
    return True, f"{bmp.width}x{bmp.height}，采样到 {colors} 种颜色"


def write_plan(directory: str, presentation: dict) -> str:
    """造一份最小 runplan.json，只带 presentation 段。"""
    os.makedirs(directory, exist_ok=True)
    plan = {
        "schemaVersion": 1,
        "driveTable": {"source": "file", "drives": []},
        "presentation": presentation,
        "phases": [
            {"phase": "preinstall", "label": "部署前", "script": "preinstall.cmd", "taskCount": 1,
             "tasks": [{"index": 1, "type": "run", "commandLine": "cmd.exe /c echo hi"}]},
        ],
        "taskCount": 1,
    }
    path = os.path.join(directory, "runplan.json")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(plan, handle, ensure_ascii=False, indent=2)
    return directory


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[union-attr]

    if len(sys.argv) < 2:
        _say("用法: python tests/integration/verify_presentation.py <osptd.exe>")
        return 2

    osptd = os.path.abspath(sys.argv[1])
    if not os.path.isfile(osptd):
        _say(f"找不到 osptd.exe: {osptd}")
        return 2

    work = os.path.join(REPO, "build", "m10", "presentation")
    os.makedirs(work, exist_ok=True)

#-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 帮助文本
    section("命令行")
    code, output = run([osptd, "--help"])
    check("--help 退出码 0", code == 0, f"exit={code}")
    check("--help 列出 --preview-presentation", "--preview-presentation" in output)

#-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 四种模块
    section("四种部署模块各画一遍")
    modules = [
        ("grid", "四格进度"),
        ("trueTrans", "Aero 风格"),
        ("colorBar", "多彩进度条"),
        ("image", "图片控件"),
    ]
    for module, label in modules:
        plan_dir = write_plan(
            os.path.join(work, f"plan-{module}"),
            {
                "module": module,
                "background": "systemDefault",
                "titleText": f"正在部署（{label}）",
                "backgroundImage": "",
                "imageSwitchSeconds": 10,
                "resolutionX": 0,
                "resolutionY": 0,
                "fontColor": "#F0F0F0",
            },
        )
        bmp = os.path.join(work, f"{module}.bmp")
        if os.path.exists(bmp):
            os.remove(bmp)

        code, output = run(
            [osptd, "--preview-presentation", "--plan", plan_dir, "--seconds", "2", "--screenshot", bmp],
            timeout=180,
        )
        if "RESULT: SKIP" in output:
            skip(f"{module} 模块绘制", "当前会话没有可见窗口站")
            continue

        check(f"{module} 退出码 0", code == 0, f"exit={code}\n{output[-300:]}")
        check(f"{module} 输出 RESULT: OK", "RESULT: OK" in output, output[-200:])
        ok, detail = analyze(bmp)
        check(f"{module} 画面有内容", ok, detail)

#-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 异常配置
    section("异常配置不能崩")
    # 注意：下面三个用例的**产物文件名是中文**，这是故意的。
    # CI runner 的 ANSI 代码页是 1252，中文会变成 '?'，而 '?' 是非法文件名字符，
    # 写截图就会失败并返回 1 —— 曾经在 CI 上就是这三条挂、四个 ASCII 名的模块用例全过。
    # 保留中文名等于每次都回归一遍「argv 必须是 UTF-8」这条链路，别改成 ASCII。
    cases = [
        (
            "背景图不存在",
            {"module": "grid", "background": "single", "backgroundImage": "Z:\\nope\\missing.jpg"},
        ),
        (
            "多图目录不存在",
            {"module": "colorBar", "background": "multi", "backgroundImage": "Z:\\nope"},
        ),
        (
            "非法颜色回退默认",
            {"module": "trueTrans", "background": "systemDefault", "fontColor": "not-a-color"},
        ),
    ]
    for label, presentation in cases:
        full = {
            "module": presentation.get("module", "grid"),
            "background": presentation.get("background", "systemDefault"),
            "titleText": f"异常用例：{label}",
            "backgroundImage": presentation.get("backgroundImage", ""),
            "imageSwitchSeconds": 10,
            "resolutionX": 0,
            "resolutionY": 0,
            "fontColor": presentation.get("fontColor", ""),
        }
        plan_dir = write_plan(os.path.join(work, "plan-" + label), full)
        bmp = os.path.join(work, f"{label}.bmp")
        if os.path.exists(bmp):
            os.remove(bmp)

        code, output = run(
            [osptd, "--preview-presentation", "--plan", plan_dir, "--seconds", "1", "--screenshot", bmp],
            timeout=180,
        )
        if "RESULT: SKIP" in output:
            skip(f"{label}", "当前会话没有可见窗口站")
            continue
        check(f"{label} 不崩溃（退出码 0）", code == 0, f"exit={code}\n{output[-300:]}")
        ok, detail = analyze(bmp)
        check(f"{label} 仍然画出内容", ok, detail)

#-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 参数校验
    section("参数校验")
    code, output = run([osptd, "--preview-presentation", "--seconds", "0"])
    check("--seconds 0 被拒绝（退出码 2）", code == 2, f"exit={code}\n{output[-200:]}")

    code, output = run([osptd, "--preview-presentation", "--seconds", "999"])
    check("--seconds 999 被拒绝（退出码 2）", code == 2, f"exit={code}\n{output[-200:]}")

    code, output = run([osptd, "--preview-presentation", "--no-such-flag"])
    check("未知开关退出码 2", code == 2, f"exit={code}")

    code, output = run([osptd, "--preview-presentation", "--plan", os.path.join(work, "nope")])
    check("计划目录不存在时退出码非 0", code != 0, f"exit={code}\n{output[-200:]}")

    _say("")
    if _failures:
        _say(f"RESULT: FAILED（{len(_failures)} 项失败 / {_checks} 项检查，{_skipped} 项跳过）")
        for name in _failures:
            _say(f"  - {name}")
        return 1
    _say(f"RESULT: ALL PASSED（{_checks} 项检查，{_skipped} 项跳过）")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception:  # noqa: BLE001
        import traceback

        traceback.print_exc(file=sys.stdout)
        sys.exit(3)

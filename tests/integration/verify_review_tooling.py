#!/usr/bin/env python3
"""verify_review_tooling.py —— 验收「独立验收器」自己（规则书改了必须有配套验收）

宪章 §9.8 那条硬规则说「改闸门要有配套验收」。这一次改的正是闸门本身：
`tools/independent_review.py` 与 `.github/workflows/review.yml`。所以必须有这个脚本，
否则改规则书的人和被规则约束的人是同一个，等于没有规则。

三组断言，全部不调模型（可离线跑）：

1. **确定性闸门的判决边界**
   - 新增 `continue-on-error: true` / `--no-tests=ignore` / `|| true` → reject
   - 只在注释里提到这些字眼 → 不 reject（注释不是开关）
   - 只改 `.md` 且 CI 绿 → 确定性层放行（交给模型）
   - CI 结论是 failure → reject；结论缺失 → uncertain（不是 reject）
2. **规则书改动必须被标注**
   - diff 里出现 `tools/independent_review.py` 等 → notes 里必须带「规则书本身」
   - 且判决交给模型，不再硬拦 —— 硬拦会让规则书永远改不动
3. **模型输出的解析与预算**
   - 合法 JSON / ``` 围栏 / 前后有废话 → 都能解析
   - 被 max_tokens 截断的半截 JSON → 返回 None（判 uncertain，不是瞎猜）
   - `call_model` 的 `max_tokens` 必须 ≥ 3000：1200 时实测被截断过一次
   - `build_user_prompt` 必须带上「已合入提交」上下文，否则只改文档的 PR 会被误判

用法: python tests/integration/verify_review_tooling.py
退出码: 0 全部通过 / 1 有失败项 / 2 无法加载被测模块
"""

from __future__ import annotations

import importlib.util
import inspect
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
TOOL = os.path.join(REPO, "tools", "independent_review.py")
WORKFLOW = os.path.join(REPO, ".github", "workflows", "review.yml")

_checks = 0
_failures: list[str] = []


def _say(text: str = "") -> None:
    sys.stdout.write(text + "\n")


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


def load_tool():
    spec = importlib.util.spec_from_file_location("independent_review_under_test", TOOL)
    if spec is None or spec.loader is None:
        return None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def diff_of(path: str, added: list[str]) -> str:
    """拼一份最小的 unified diff，只含新增行"""
    return diff_of_many({path: added})


def diff_of_many(files: dict[str, list[str]]) -> str:
    """多文件版：{路径: 新增行}。有些规则要看「整份 diff 里有没有配测试」"""
    parts = []
    for path, added in files.items():
        body = "".join("+" + line + "\n" for line in added)
        parts.append(f"diff --git a/{path} b/{path}\n--- a/{path}\n+++ b/{path}\n@@ -1,1 +1,1 @@\n{body}")
    return "".join(parts)


def diff_with_removal(path: str, removed: list[str]) -> str:
    """只有删除行的 diff —— 规则书里的「松绑」基本都长这样"""
    body = "".join("-" + line + "\n" for line in removed)
    return f"diff --git a/{path} b/{path}\n--- a/{path}\n+++ b/{path}\n@@ -1,1 +1,1 @@\n{body}"


CI_OK = "success"
CI_FAIL = "failure"
CI_MISSING = "missing"


def run_tool_against_stub(model_content: str, tool, model: str = "tw-intl:stub-reviewer") -> tuple[int, str]:
    """起一个假的 OpenAI 兼容服务，把评审器当子进程跑，返回 (退出码, 细节)

    这一层验的是真实链路：HTTP 请求 → 响应解析 → 判决 → 退出码。
    只 grep 源码里有没有 "3000" 这种断言，评审模型在 PR #5 上直接指出过不算验证。
    """
    import http.server
    import json as _json
    import tempfile
    import threading

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_POST(self):  # noqa: N802 —— http.server 的固定接口名
            length = int(self.headers.get("Content-Length") or 0)
            raw = self.rfile.read(length)
            try:
                requested = _json.loads(raw.decode("utf-8", errors="replace")).get("model", "")
            except Exception:  # noqa: BLE001 —— 畸形请求当普通请求处理
                requested = ""
            # 名字里带 broken 就当「这个池子配额用尽」，用来验多候选降级
            if "broken" in requested:
                body = _json.dumps(
                    {
                        "error": {
                            "message": "all accounts unavailable (cooling/disabled)",
                            "code": "no_healthy_account",
                        }
                    }
                ).encode("utf-8")
                self.send_response(503)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            payload = {
                "choices": [{"message": {"role": "assistant", "content": model_content}}],
                "usage": {"prompt_tokens": 1, "completion_tokens": 1},
            }
            body = _json.dumps(payload, ensure_ascii=False).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, format=None, *args):  # noqa: A002 —— 覆盖基类接口，静音访问日志
            return

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    port = server.server_address[1]
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    work = tempfile.mkdtemp(prefix="ospt-review-e2e-")
    diff_path = os.path.join(work, "pr.diff")
    evidence_path = os.path.join(work, "evidence.txt")
    out_path = os.path.join(work, "review.json")
    with open(diff_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(diff_of("docs/note.md", ["补充一段说明"]))
    with open(evidence_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("RESULT: ALL PASSED (23 checks)\n")

    env = dict(os.environ)
    env["OSPT_REVIEW_API_KEY"] = "stub-key"
    try:
        result = __import__("subprocess").run(
            [
                sys.executable,
                TOOL,
                "--diff", diff_path,
                "--ci-conclusion", CI_OK,
                "--evidence", evidence_path,
                "--out", out_path,
                "--model", model,
                "--base-url", f"http://127.0.0.1:{port}/v1",
                "--timeout", "30",
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            env=env,
        )
        tail = (result.stdout + result.stderr).strip().splitlines()
        return result.returncode, (tail[-1] if tail else "")
    finally:
        server.shutdown()
        server.server_close()


def main(argv: list[str]) -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[union-attr]

    tool = load_tool()
    if tool is None:
        _say(f"无法加载 {TOOL}")
        return 2

    _say(f"verify_review_tooling: {os.path.relpath(TOOL, REPO)}")

    # ------------------------------------------------------------ 1. 判决边界
    section("确定性闸门：削弱闸门的写法必须被拦")
    cases = [
        (".github/workflows/ci.yml", ["continue-on-error: true"], "continue-on-error"),
        (".github/workflows/ci.yml", ["ctest --no-tests=ignore"], "--no-tests=ignore"),
        (".github/workflows/ci.yml", ["python x.py || true"], "|| true"),
        ("CMakeLists.txt", ["set(OSP_WERROR OFF)"], "关 /WX"),
        ("tests/integration/verify_ui_layout.py", ["pytest.mark.skip"], "skip 标记"),
    ]
    for path, added, label in cases:
        verdict, reasons, notes = tool.deterministic_checks(diff_of(path, added), CI_OK)
        check(f"「{label}」被判 reject", verdict == "reject", f"verdict={verdict}")

    section("确定性闸门：注释里提到不算削弱")
    verdict, reasons, notes = tool.deterministic_checks(
        diff_of(
            "tests/integration/verify_ui_layout.py",
            ["# 这里以前写过 continue-on-error: true，已删除"],
        ),
        CI_OK,
    )
    check("注释行不被误判成开关", verdict != "reject", f"reasons={reasons}")

    section("确定性闸门：CI 结论三档不能混")
    verdict, _r, _n = tool.deterministic_checks(diff_of("docs/X.md", ["新增一行"]), CI_FAIL)
    check("CI 红 → reject", verdict == "reject", f"verdict={verdict}")
    verdict, _r, _n = tool.deterministic_checks(diff_of("docs/X.md", ["新增一行"]), CI_MISSING)
    check("CI 结论缺失 → uncertain（不是 reject）", verdict == "uncertain", f"verdict={verdict}")
    verdict, _r, _n = tool.deterministic_checks(diff_of("docs/X.md", ["新增一行"]), CI_OK)
    check("只改文档 + CI 绿 → 放行给模型", verdict == "", f"verdict={verdict}")

    # ------------------------------------------------------------ 2. 规则书改动
    section("规则书本身被改动：必须交给模型并明确标注")
    verdict, reasons, notes = tool.deterministic_checks(
        diff_of_many(
            {
                "tools/independent_review.py": ["max_tokens = 3000"],
                "tests/integration/verify_review_tooling.py": ["check('x', True)"],
            }
        ),
        CI_OK,
    )
    check("不再硬拦（否则规则书永远改不动）", verdict == "", f"verdict={verdict} reasons={reasons}")
    joined = "\n".join(notes)
    check("notes 里点名「规则书」", "规则书" in joined, joined[:120])
    check("notes 里要求区分加严与松绑", "松绑" in joined, joined[:120])

    verdict, reasons, notes = tool.deterministic_checks(
        diff_of_many(
            {
                ".github/workflows/review.yml": ["--context merged-context.txt"],
                "tests/integration/verify_review_tooling.py": ["check('x', True)"],
            }
        ),
        CI_OK,
    )
    check("review.yml 也算规则书", any("review.yml" in n for n in notes), str(notes)[:120])

    section("规则书里的「松绑」= 删除断言或步骤，必须机械拦下")
    verdict, reasons, _n = tool.deterministic_checks(
        diff_with_removal("tools/independent_review.py", ["    check('闸门还在', verdict == 'reject')"]),
        CI_OK,
    )
    check("删掉一条 check() 被判 reject", verdict == "reject", f"verdict={verdict}")
    verdict, reasons, _n = tool.deterministic_checks(
        diff_with_removal(".github/workflows/review.yml", ["      - name: 独立验收（异模型）"]),
        CI_OK,
    )
    check("删掉一个工作流步骤被判 reject", verdict == "reject", f"verdict={verdict}")
    verdict, reasons, _n = tool.deterministic_checks(
        diff_with_removal("tools/independent_review.py", ["    # 一条注释，删掉不影响闸门"]),
        CI_OK,
    )
    check("只删注释不算松绑", verdict != "reject", f"reasons={reasons}")

    # ------------------------------------------------------------ 3. 解析与预算
    section("模型输出的解析")
    parse = tool.parse_verdict
    good = '{"verdict":"pass","summary":"ok","must_fix":[],"evidence_cited":[],"confidence":0.9}'
    check("裸 JSON 能解析", (parse(good) or {}).get("verdict") == "pass")
    fenced = "```json\n" + good + "\n```"
    check("``` 围栏能解析", (parse(fenced) or {}).get("verdict") == "pass")
    chatty = "判决如下：\n" + good + "\n以上。"
    check("前后有废话能解析", (parse(chatty) or {}).get("verdict") == "pass")
    truncated = '{"verdict":"reject","summary":"本次提交只改文档，却声称新增并已落地多项门禁'
    check("被截断的半截 JSON 返回 None（判 uncertain，不瞎猜）", parse(truncated) is None)
    check("非法 verdict 被拒绝", parse('{"verdict":"maybe"}') is None)

    section("模型预算与上下文")
    source = inspect.getsource(tool.call_model)
    check(
        "max_tokens ≥ 3000（1200 时实测被截断，判决读不出来）",
        "max_tokens" in source and "3000" in source,
        "调用参数里没看到 3000",
    )
    check(
        "build_user_prompt 接受 context（只改文档的 PR 靠它才判得对）",
        "context" in inspect.signature(tool.build_user_prompt).parameters,
    )

    section("工作流接线")
    with open(WORKFLOW, encoding="utf-8") as handle:
        workflow = handle.read()
    check("review.yml 把 merged-context.txt 传给评审器", "--context merged-context.txt" in workflow)
    check("review.yml 会生成 merged-context.txt", "merged-context.txt" in workflow)
    check(
        "review.yml 里没有 `|| true`（本仓库自己的确定性闸门禁止这种吞失败写法）",
        "|| true" not in workflow,
    )

    # ------------------------------------------------------------ 4. 端到端
    # 只断言源码字符串是不够的（评审模型在 PR #5 上就指出过这一点）。
    # 这里起一个假的 OpenAI 兼容服务，把 tools/independent_review.py 当子进程真跑一遍，
    # 验的是「模型回什么 → 退出码是什么」这条真实链路。
    section("端到端：假模型服务 → 退出码")
    for label, content, want in [
        ("pass → 0", '{"verdict":"pass","summary":"ok","must_fix":[],"evidence_cited":[],"confidence":0.9}', 0),
        ("reject → 1", '{"verdict":"reject","summary":"缺少证据","must_fix":["补日志"],"evidence_cited":[],"confidence":0.9}', 1),
        ("垃圾输出 → 2", "我看不懂，随便说两句", 2),
    ]:
        code, detail = run_tool_against_stub(content, tool)
        check(f"{label}", code == want, f"实际 exit={code}；{detail}")

    # 上游单池冷却时不能把 PR 永远卡死：第一个候选 503，第二个顶上就该判出来
    code, detail = run_tool_against_stub(
        '{"verdict":"pass","summary":"第二个池子接上了","must_fix":[],"evidence_cited":[],"confidence":0.8}',
        tool,
        model="tw-intl:broken-pool,wb-cn:glm-5.3",
    )
    check("第一个候选 503 时自动降级到下一个", code == 0, f"实际 exit={code}；{detail}")

    code, detail = run_tool_against_stub(
        '{"verdict":"pass","summary":"x","must_fix":[],"evidence_cited":[],"confidence":0.5}',
        tool,
        model="tw-intl:broken-a,tw-intl:broken-b",
    )
    check("所有候选都 503 → uncertain（exit 2），不是假 pass", code == 2, f"实际 exit={code}；{detail}")

    _say("")
    if _failures:
        _say(f"RESULT: FAILED  ({len(_failures)}/{_checks} checks failed)")
        for name in _failures:
            _say(f"  - {name}")
        return 1
    _say(f"RESULT: ALL PASSED  ({_checks} checks)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

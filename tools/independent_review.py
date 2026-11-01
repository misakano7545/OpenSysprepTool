#!/usr/bin/env python3
"""independent_review.py —— 独立 AI 验收（第二双眼睛）

为什么要有这一层
----------------
写代码的 AI 自己跑自己的验收脚本，然后把「全绿」写进记录 —— 这不是验收，是自证。
2026-10-28 那次「界面整体错乱」就是自证：冒烟 266 项全过，界面照样是乱的，因为
断言的期望值也来自那张错的表。

所以这里做两件事，顺序不能换：

1. **确定性闸门（先跑，不依赖模型）**
   把「能机械判定」的作弊与破口全部挡在前面。模型再懒，这些也拦得住：
   - CI 对这个 sha 必须已经全绿，否则直接 reject（不调模型，省一次调用）
   - diff 里不许出现削弱闸门的写法：`continue-on-error`、`--no-tests=ignore`、
     `|| true`、`if: false`、`OSP_WERROR=OFF`、`pytest.mark.skip` 等
   - 改了 `.github/workflows/**` 就必须在 diff 里同时改到验收脚本或说明，否则 reject
2. **异模型评审（后跑）**
   调一个**与写代码的模型不同族**的模型，只给它材料（diff / 规则 / CI 证据），
   要求输出严格 JSON。verdict ∈ pass / reject / uncertain。
   - pass：退出码 0
   - reject：退出码 1
   - uncertain（无法确认，含模型不可用/输出不合规）：退出码 2，交给人

用法:
    python tools/independent_review.py --diff pr.diff --ci-conclusion success \
        --out review.json --summary summary.md
退出码: 0 通过 / 1 退回修改 / 2 无法确认（交给人）
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import urllib.error
import urllib.request

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

DEFAULT_BASE_URL = os.environ.get("OSPT_REVIEW_BASE_URL", "https://ai.misakano.kdns.fr/v1")
DEFAULT_MODEL = os.environ.get("OSPT_REVIEW_MODEL", "tw-intl:gpt-5.4")

# 与写代码的模型必须不同族：宪章要求「另一个 AI，最好不同模型」。
# 这里把「本仓库的历史作者模型」写死，只是为了在选错模型时大声失败。
CODER_MODEL_HINTS = ("deepseek",)

# 削弱闸门的写法。命中即 reject —— 不给模型解释空间，这类改动一律要人来提。
GATE_WEAKENING = [
    (r"continue-on-error\s*:\s*true", "workflow 里出现 continue-on-error: true"),
    (r"--no-tests=ignore", "ctest 用了 --no-tests=ignore（零测试也算绿）"),
    (r"\|\|\s*true\s*$", "命令尾部加了 || true（失败被吞）"),
    (r"if\s*:\s*false", "步骤被 if: false 关掉"),
    (r"(?i)OSP_WERROR[\s=:]*OFF", "关掉了 /WX"),
    (r"(?i)pytest\.mark\.(skip|xfail)", "测试被 skip/xfail 标记"),
    (r"(?i)\bSkip-Test\b", "PowerShell 里跳过测试"),
    (r"@unittest\.skip", "单元测试被 skip 装饰器关掉"),
]

MAX_DIFF_CHARS = 24000  # 超过就截断，宁可少看也不能把上下文撑爆

# 裁判自己的规则书。这三个文件被改动时必须交给人：否则「改闸门」这件事可以由
# 写代码的那一手自己批准，等于没有闸门。
# 同时也解决了一个真实的自伤问题：本文件里就写着那些被禁写法的字面量（它们是
# 规则数据），自扫必然命中自己 —— 现在这条规则让自扫根本轮不到执行。
PROTECTED_TOOLING = (
    "tools/independent_review.py",
    "tools/collect_ci_evidence.py",
    ".github/workflows/review.yml",
)

SYSTEM_PROMPT = """你是一个独立的代码验收者。你不是写这段代码的人，也不认识他。

你的职责：只根据给到的材料判断这次改动能不能合入。你没有「鼓励开发者」的任务，
也没有「保持礼貌」的任务。默认怀疑，证据不足就说不确定。

判断规则（按优先级）：
1. 声称完成但拿不出可验证证据的，一律 reject。证据 = 命令输出、退出码、日志、断言。
2. 验收脚本自己给自己打分（期望值与被验对象同源）的，视为没有验证，reject。
3. 破坏了既有闸门（删断言、放宽容差、跳过测试、关掉警告）的，reject。
4. 只改了文档/注释却声称修好了行为的，reject。
5. 材料不足以判断时，输出 uncertain，不要猜。
6. 有些证据在**当前时点不可能存在**：例如这次改动本身就是在引入评审链路，它还没来得及跑过一次。
   这种情况判 uncertain 并写清「需要什么才能转成 pass」，不要因为「第一次跑不了」就 reject ——
   那道门会永远锁死，谁来都进不去。
7. 证据只给了一部分（有文件被截断）时：如果缺的部分不影响你判断的那几条，照常下判；
   如果影响，uncertain 并点名缺哪个文件。不要因为看不到就默认有罪。
8. **只改文档的提交**：diff 里只有 `.md` 时，它的作用是记录已经发生的事。如果材料里
   给出了「本 PR 之前已合入的提交」清单，且文档描述的能力对应这些提交，就不要因为
   「本次 diff 里没有代码」而 reject；只有当文档写了清单里根本不存在的东西时才 reject。

输出必须是严格 JSON，不要 markdown 代码块，不要任何解释性前后缀。
**务必短**：summary 一句话；must_fix 最多 3 条、每条不超过 60 字；evidence_cited 最多 3 条。
（上一版没限长度，输出被 max_tokens 截断，整段 JSON 变成不可解析 —— 判决再对也读不出来。）
{"verdict":"pass|reject|uncertain",
 "summary":"一句话中文结论",
 "must_fix":["最多 3 条，每条 ≤60 字"],
 "evidence_cited":["最多 3 条"],
 "confidence":0.0-1.0}"""


def _say(text: str = "") -> None:
    sys.stdout.write(text + "\n")


def section(title: str) -> None:
    _say("")
    _say(f"== {title} ==")


def load_text(path: str) -> str:
    with open(path, encoding="utf-8", errors="replace") as handle:
        return handle.read()


def changed_files(diff: str) -> list[str]:
    return re.findall(r"^\+\+\+ b/(.+)$", diff, re.M)


COMMENT_PREFIXES = ("#", "//", ";;", "--", "<!--", "/*", "*", "rem ", "REM ", "'")

# 只有这些文件里的新增行才可能是「削弱闸门」。文档（.md）里提到某个开关是在解释
# 规则，不是在关它 —— 第一版没做这个区分，直接把自己解释 /WX 的那段 CHANGELOG
# 判成了「关掉了 /WX」。闸门误报一次就会被关掉，所以这条边界必须精确。
GATE_BEARING_SUFFIXES = (
    ".py",
    ".ps1",
    ".sh",
    ".bat",
    ".cmd",
    ".yml",
    ".yaml",
    ".cmake",
)
GATE_BEARING_NAMES = ("CMakeLists.txt", "Makefile")

# 规则定义与规则测试这两种文件必然包含这些模式串（它们就是拿这些串做断言的），
# 扫它们等于每次提交都自我驳回。所以排除 —— 但排除名单本身在 PROTECTED_TOOLING
# 里，改它会被标成「改了规则书」，由评审模型逐条判断是加严还是松绑。
SELF_FILES = (
    "tools/independent_review.py",
    "tests/integration/verify_review_tooling.py",
)


def is_gate_bearing(path: str) -> bool:
    if path.startswith(".github/"):
        return True
    base = path.rsplit("/", 1)[-1]
    if base in GATE_BEARING_NAMES:
        return True
    return path.endswith(GATE_BEARING_SUFFIXES)


def iter_file_diffs(diff: str):
    """把 unified diff 切成 (文件路径, 新增代码行)"""
    path = ""
    lines: list[str] = []
    for line in diff.splitlines():
        if line.startswith("+++ "):
            if path:
                yield path, lines
            path = line[4:].strip()
            if path.startswith("b/"):
                path = path[2:]
            lines = []
            continue
        if not line.startswith("+") or line.startswith("+++"):
            continue
        body = line[1:].strip()
        if not body:
            continue
        if body.lower().startswith(COMMENT_PREFIXES):
            continue
        lines.append(re.sub(r"\s+#\s.*$", "", body))
    if path:
        yield path, lines


def iter_removed_code_lines(diff: str):
    """把 unified diff 切成 (文件路径, 被删除的代码行)

    「规则书被改」这件事有两种：加严和松绑。加严往往只是新增行，松绑则表现为
    **删除** —— 删掉一条 check()、删掉一个工作流步骤、删掉一项必检。
    只看新增行的话，删断言是看不见的。
    """
    path = ""
    for line in diff.splitlines():
        if line.startswith("+++ "):
            path = line[4:].strip()
            if path.startswith("b/"):
                path = path[2:]
            continue
        if not line.startswith("-") or line.startswith("---"):
            continue
        body = line[1:].strip()
        if not body:
            continue
        if body.lower().startswith(COMMENT_PREFIXES):
            continue
        yield path, body


def all_added_code_lines(diff: str) -> list[str]:
    result: list[str] = []
    for _path, lines in iter_file_diffs(diff):
        result.extend(lines)
    return result


def deterministic_checks(diff: str, ci_conclusion: str) -> tuple[str, list[str], list[str]]:
    """返回 (verdict, reasons, notes)。

    verdict 非空即直接下判，不调模型；notes 是给评审模型的额外提醒
    （例如「本次改动包含验收规则书本身」）。

    CI 结论分三档，不能混：
      success          → 继续往下查
      failure 等       → reject（脏的提交没有讨论余地）
      missing / error  → uncertain（拿不到证据不等于证据证明有问题，
                        这类要交给人，否则链路第一次上线就永远进不去）
    """
    reasons: list[str] = []
    notes: list[str] = []
    escaped: list[str] = []
    verdict_ci = ci_conclusion.strip().lower()

    if verdict_ci in ("failure", "failed", "red", "cancelled", "timed_out"):
        reasons.append(f"CI 对这个 sha 的结论是「{ci_conclusion}」，不是 success")
        return "reject", reasons, notes
    if verdict_ci not in ("success", "passed", "green"):
        reasons.append(
            f"拿不到可用的 CI 证据（结论「{ci_conclusion}」）：按「无法确认」处理，交给人"
        )
        return "uncertain", reasons, notes

    for path, lines in iter_file_diffs(diff):
        if path in PROTECTED_TOOLING:
            # 不硬拦 —— 硬拦会让「改进验收规则」这件事永远卡在门外（第一版就是这样，
            # 连给评审模型加个上下文都做不到）。但必须让模型知道它在看的是规则书本身，
            # 由它逐条判断是加严还是松绑，而这一条不给任何「自动放行」的余地。
            escaped.append(path)
            continue
        if not is_gate_bearing(path):
            continue
        if path in SELF_FILES:
            continue
        for pattern, why in GATE_WEAKENING:
            for line in lines:
                if re.search(pattern, line):
                    reasons.append(f"{why}（{path}）：`{line.strip()[:120]}`")
                    break

    files = changed_files(diff)
    if not files:
        reasons.append("diff 里没有任何文件改动")

    # 规则书里的「松绑」几乎都长成删除：删掉一条 check()、删掉一个工作流步骤。
    # 第一版对规则书是整份硬拦（一律 uncertain 交给人），结果连给评审器加个上下文
    # 都做不到 —— 规则书永远改不动。现在换成能机械判定的那条：**删断言/删步骤 = reject**，
    # 其余改动照常交给模型逐条判断加严还是松绑。
    for path, body in iter_removed_code_lines(diff):
        if path not in PROTECTED_TOOLING:
            continue
        if re.search(r"\bcheck\(|\bREQUIRE\(|\bassert\b|^\s*-\s*name\s*:|^\s*(run|uses|shell)\s*:", body):
            reasons.append(f"在规则书里删掉了断言或工作流步骤（{path}）：`{body[:120]}`")

    touch_ci = [name for name in files if name.startswith(".github/workflows/")]
    touch_tests = [
        name
        for name in files
        if name.startswith("tests/") or name.endswith("_test.py") or "/test_" in name
    ]
    if touch_ci and not touch_tests:
        # 只改工作流也算正当 —— 前提是它调用了一个真实存在的验收脚本：
        # 把闸门加严是好事，把闸门关小才是坏事。这里用「引用了哪个 verify_*.py
        # 且该文件真的在 tests/integration 下」来区分两者。
        referenced = set(re.findall(r"verify_[A-Za-z0-9_]+\.py", "\n".join(all_added_code_lines(diff))))
        existing = {
            name
            for name in referenced
            if os.path.isfile(os.path.join(REPO, "tests", "integration", name))
        }
        if not existing:
            reasons.append(
                "只改了 .github/workflows/，既没碰测试也没引用任何既有的 verify_*.py："
                "闸门被改动必须有验收配套"
            )

    if reasons:
        return "reject", reasons, notes
    if escaped:
        notes.append(
            "⚠ 本次改动包含**验收规则书本身**：" + "、".join(escaped) +
            "。逐条核对每一处改动是「加严」还是「松绑」：新增检查、收紧阈值 = 加严；"
            "删除断言、放宽容差、减少必检项、调高 token/超时上限以外的放水 = 松绑。"
            "任何一处松绑都必须 reject，并写明是哪一行。"
        )
    return "", [], notes


def diff_manifest(diff: str) -> tuple[str, dict[str, int]]:
    """按文件切分 diff，返回（清单文本，{文件: 行数}）"""
    chunks: dict[str, list[str]] = {}
    order: list[str] = []
    current = ""
    for line in diff.splitlines():
        if line.startswith("+++ b/"):
            current = line[6:]
            if current not in chunks:
                chunks[current] = []
                order.append(current)
            continue
        if current:
            chunks[current].append(line)

    lines = ["文件清单（新增/删除行数）："]
    counts: dict[str, int] = {}
    for name in order:
        body = chunks[name]
        added = sum(1 for line in body if line.startswith("+") and not line.startswith("+++"))
        removed = sum(1 for line in body if line.startswith("-") and not line.startswith("---"))
        counts[name] = len(body)
        lines.append(f"  {name}: +{added}/-{removed}")
    return "\n".join(lines), counts


def build_user_prompt(
    diff: str, ci_conclusion: str, files: list[str], evidence: str, context: str = ""
) -> str:
    # 第一版把整份 diff 截到前 24000 字符，评审模型当场驳回：「给的是截断的，
    # 无法检查后半段」。截断本身没错，错在没说清缺了什么。现在改成按文件分配预算：
    # 小文件给全文，大文件给前 N 行并**逐文件**标注还剩多少没给。
    manifest, counts = diff_manifest(diff)
    per_file_budget = max(1200, MAX_DIFF_CHARS // max(1, len(counts)))

    chunks: dict[str, list[str]] = {}
    order: list[str] = []
    current = ""
    for line in diff.splitlines():
        if line.startswith("+++ b/"):
            current = line[6:]
            if current not in chunks:
                chunks[current] = []
                order.append(current)
            continue
        if current:
            chunks[current].append(line)

    parts: list[str] = []
    omitted: list[str] = []
    for name in order:
        body = chunks[name]
        if len(body) <= per_file_budget:
            parts.append(f"### {name}\n" + "\n".join(body))
        else:
            parts.append(
                f"### {name}（前 {per_file_budget} 行，共 {len(body)} 行）\n"
                + "\n".join(body[:per_file_budget])
            )
            omitted.append(f"{name}（缺 {len(body) - per_file_budget} 行）")

    note = ""
    if omitted:
        note = (
            "\n\n⚠ 以下文件**只给了前半部分**，后半段没给：" + "、".join(omitted) +
            "\n如果你认为缺的那部分可能影响判决，输出 uncertain 并说明缺哪个文件的哪一段，"
            "不要因为看不到就默认它有罪，也不要默认它没问题。"
        )
    else:
        note = "\n\n（本次 diff 完整给出，没有截断）"

    rules = ""
    charter = os.path.join(REPO, "docs", "CHARTER.md")
    if os.path.isfile(charter):
        text = load_text(charter)
        # 只取红线一节：模型看全篇会跑题
        match = re.search(r"## 7\..*?(?=\n## 8\.)", text, re.S)
        if match:
            rules = "\n\n仓库硬规则（宪章第 7 节，任何实现都不得包含）：\n" + match.group(0).strip()

    return (
        f"CI 结论：{ci_conclusion}\n"
        f"改动文件（{len(files)} 个）：{', '.join(files) if files else '（无）'}\n"
        f"{context}"
        f"\n{manifest}"
        f"{rules}\n\n--- CI 运行证据（从本次流水线的真实日志里摘出来的原文）---\n"
        f"{evidence.strip()[:12000]}\n--- 证据结束 ---"
        f"\n\n--- diff 开始 ---\n" + "\n\n".join(parts) + f"\n--- diff 结束 ---{note}"
    )


def parse_verdict(raw: str) -> dict | None:
    """模型只该回 JSON；容忍它套了 ```json 围栏"""
    text = raw.strip()
    fence = re.search(r"```(?:json)?\s*(.*?)```", text, re.S)
    if fence:
        text = fence.group(1).strip()
    start = text.find("{")
    end = text.rfind("}")
    if start < 0 or end <= start:
        return None
    try:
        data = json.loads(text[start : end + 1])
    except json.JSONDecodeError:
        return None
    if not isinstance(data, dict):
        return None
    if data.get("verdict") not in ("pass", "reject", "uncertain"):
        return None
    data.setdefault("summary", "")
    data.setdefault("must_fix", [])
    data.setdefault("evidence_cited", [])
    data.setdefault("confidence", 0.0)
    return data


def call_model(prompt: str, base_url: str, api_key: str, model: str, timeout: int) -> tuple[dict | None, str]:
    body = json.dumps(
        {
            "model": model,
            "messages": [
                {"role": "system", "content": SYSTEM_PROMPT},
                {"role": "user", "content": prompt},
            ],
            "temperature": 0,
            "max_tokens": 3000,
        }
    ).encode("utf-8")

    request = urllib.request.Request(
        base_url.rstrip("/") + "/chat/completions",
        data=body,
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
            # 默认的 Python-urllib UA 会被前置的 Cloudflare 直接 1010 掉
            # （实测：同一把 key、同一个请求体，curl 200，urllib 403 error code 1010）。
            "User-Agent": "OpenSysprepTool-IndependentReview/1.0",
            "Accept": "application/json",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            payload = json.loads(response.read().decode("utf-8", errors="replace"))
    except urllib.error.HTTPError as exc:
        return None, f"HTTP {exc.code}: {exc.read()[:300].decode('utf-8', errors='replace')}"
    except Exception as exc:  # noqa: BLE001 —— 网络层任何异常都只能是「无法确认」
        return None, f"{type(exc).__name__}: {exc}"

    choices = payload.get("choices") or []
    if not choices:
        return None, f"空 choices：{json.dumps(payload)[:300]}"
    content = (choices[0].get("message") or {}).get("content") or ""
    return parse_verdict(content), content.strip()


def write_summary(path: str, verdict: dict, model: str, reasons: list[str]) -> None:
    lines = [
        "# 独立验收结论",
        "",
        f"- 判决：**{verdict['verdict']}**",
        f"- 评审模型：`{model}`",
        f"- 置信度：{verdict.get('confidence')}",
        f"- 结论：{verdict.get('summary') or '（无）'}",
        "",
    ]
    if reasons:
        lines += ["## 确定性闸门拦下的问题", ""] + [f"- {item}" for item in reasons] + [""]
    must_fix = verdict.get("must_fix") or []
    lines += ["## 必须修改", ""] + ([f"- {item}" for item in must_fix] or ["- （无）"]) + [""]
    cited = verdict.get("evidence_cited") or []
    lines += ["## 引用到的材料", ""] + ([f"- {item}" for item in cited] or ["- （无）"]) + [""]
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))


def main(argv: list[str]) -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[union-attr]

    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("--diff", required=True)
    parser.add_argument("--ci-conclusion", required=True)
    parser.add_argument("--evidence", default="", help="CI 真实日志摘要；不给就等于没有证据")
    parser.add_argument("--out", default="independent-review.json")
    parser.add_argument("--summary", default="")
    parser.add_argument("--model", default=DEFAULT_MODEL)
    parser.add_argument("--base-url", default=DEFAULT_BASE_URL)
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument(
        "--context",
        default="",
        help="本次改动之前已合入的提交清单（文件路径）。只改文档的 PR 靠它才能被判对",
    )
    parser.add_argument("--self-test", action="store_true", help="只跑确定性闸门，不调模型")
    try:
        args = parser.parse_args(argv[1:])
    except SystemExit:
        return 2

    if not os.path.isfile(args.diff):
        _say(f"找不到 diff 文件: {args.diff}")
        return 2

    diff = load_text(args.diff)
    files = changed_files(diff)
    evidence = ""
    if args.evidence and os.path.isfile(args.evidence):
        evidence = load_text(args.evidence)
    context = ""
    if args.context and os.path.isfile(args.context):
        context = load_text(args.context).strip()
        if context:
            context = "\n--- 本 PR 之前已合入 main 的提交（文档描述的能力通常在这里）---\n" + context[:4000] + "\n"

    _say(f"independent_review: diff {len(diff)} 字符 / {len(files)} 个文件")
    _say(f"CI 结论: {args.ci_conclusion}")
    _say(f"CI 证据: {'%d 字符' % len(evidence) if evidence else '（没有）'}")
    _say(f"上下文: {'有' if context else '（没有）'}")

    section("确定性闸门（不依赖模型）")
    verdict_code, reasons, notes = deterministic_checks(diff, args.ci_conclusion)
    for item in notes:
        _say(f"  NOTE  {item}")
    if not verdict_code and not evidence.strip():
        # 「声称跑过」不等于「跑过」。没有从真实流水线摘出来的日志，就不给合入。
        verdict_code = "reject"
        reasons = ["没有提供任何 CI 运行证据（--evidence）：只有结论没有日志，等于没验证"]
    if verdict_code:
        for item in reasons:
            _say(f"  {verdict_code.upper()}  {item}")
        result = {
            "verdict": verdict_code,
            # 结论直接用第一条原因，别写一句笼统的固定话术 ——
            # 「改动了规则书」和「没给证据」是两种完全不同的处理方式，
            # 混成一句话会让人照着错的去修。
            "summary": (reasons[0] if reasons else "确定性闸门拦下"),
            "must_fix": reasons,
            "evidence_cited": [],
            "confidence": 1.0,
            "model": "deterministic",
        }
        with open(args.out, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(result, handle, ensure_ascii=False, indent=2)
            handle.write("\n")
        if args.summary:
            write_summary(args.summary, result, "deterministic", reasons)
        _say(_as_result(result))
        return 1 if verdict_code == "reject" else 2
    _say("  PASS  没有削弱闸门的写法，CI 也是绿的")

    if args.self_test:
        _say("")
        _say("（--self-test：只跑确定性闸门，不调模型）")
        return 0

    section("异模型评审")
    for hint in CODER_MODEL_HINTS:
        if hint in args.model.lower():
            _say(f"  FAIL  评审模型 {args.model} 与写代码的模型同族（含 '{hint}'），不是独立验收")
            result = {
                "verdict": "uncertain",
                "summary": "评审模型与写代码的模型同族，不能算独立验收",
                "must_fix": [f"换一个不同族的模型：当前是 {args.model}"],
                "evidence_cited": [],
                "confidence": 1.0,
                "model": args.model,
            }
            with open(args.out, "w", encoding="utf-8", newline="\n") as handle:
                json.dump(result, handle, ensure_ascii=False, indent=2)
                handle.write("\n")
            if args.summary:
                write_summary(args.summary, result, args.model, [])
            _say(_as_result(result))
            return 2

    api_key = os.environ.get("OSPT_REVIEW_API_KEY", "").strip()
    if not api_key:
        _say("  UNCERTAIN  没有 OSPT_REVIEW_API_KEY，无法执行独立评审")
        result = {
            "verdict": "uncertain",
            "summary": "未配置评审模型的 API key，独立验收无法执行（交给人处理）",
            "must_fix": ["在仓库 Secrets 里配置 OSPT_REVIEW_API_KEY"],
            "evidence_cited": [],
            "confidence": 1.0,
            "model": args.model,
        }
        with open(args.out, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(result, handle, ensure_ascii=False, indent=2)
            handle.write("\n")
        if args.summary:
            write_summary(args.summary, result, args.model, [])
        _say(_as_result(result))
        return 2

    _say(f"  调用 {args.model} …")
    verdict, raw = call_model(
        build_user_prompt(diff, args.ci_conclusion, files, evidence, context),
        args.base_url,
        args.api_key if hasattr(args, "api_key") else api_key,
        args.model,
        args.timeout,
    )
    if verdict is None:
        _say(f"  UNCERTAIN  模型输出不可解析或调用失败：{raw[:300]}")
        result = {
            "verdict": "uncertain",
            "summary": "评审模型不可用或输出不合规，按「无法确认」交给人",
            "must_fix": [],
            "evidence_cited": [],
            "confidence": 0.0,
            "model": args.model,
            "raw": raw[:2000],
        }
        with open(args.out, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(result, handle, ensure_ascii=False, indent=2)
            handle.write("\n")
        if args.summary:
            write_summary(args.summary, result, args.model, [])
        _say(_as_result(result))
        return 2

    verdict["model"] = args.model
    with open(args.out, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(verdict, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    if args.summary:
        write_summary(args.summary, verdict, args.model, [])
    _say(_as_result(verdict))
    return {"pass": 0, "reject": 1, "uncertain": 2}[verdict["verdict"]]


def _as_result(verdict: dict) -> str:
    must_fix = verdict.get("must_fix") or []
    lines = [f"RESULT: {verdict['verdict'].upper()}  {verdict.get('summary', '')}"]
    for item in must_fix:
        lines.append(f"  - {item}")
    return "\n".join(lines)


if __name__ == "__main__":
    sys.exit(main(sys.argv))

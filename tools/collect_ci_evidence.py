#!/usr/bin/env python3
"""collect_ci_evidence.py —— 把某个 sha 的 CI 运行结果收成一份可验的证据

独立验收者（`tools/independent_review.py`）不接受「CI 结论: success」这种口头结论 ——
它要的是真实日志里的原文。这个脚本负责把那些原文摘出来：

- 等该 sha 的所有工作流跑完（默认最多等 25 分钟），跑不完就是 inconclusive
- 逐个作业记录 conclusion，任何一个不是 success 就把总结论记为 failure
- 从作业日志里摘出关键行：`warning C` / `error C` 计数、`RESULT:` 行、
  `tests passed`、`werror` 配置值、`SMOKE-OK`
- 同时把「有多少条 warning」算成数字写进证据首行 —— 宪章说「0 warning」，
  这句话必须能被一条 grep 证伪

用法:
    python tools/collect_ci_evidence.py --repo owner/name --sha <sha> \
        --out evidence.txt --conclusion conclusion.txt [--wait-minutes 25]
退出码: 0 收集完成（结论写在 conclusion 文件里）/ 2 用法或网络错误
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

API = os.environ.get("GITHUB_API_URL", "https://api.github.com")


def _say(text: str = "") -> None:
    sys.stdout.write(text + "\n")


class _StripAuthOnRedirect(urllib.request.HTTPRedirectHandler):
    """日志接口会 302 到 objects.githubusercontent.com 的签名地址。

    签名 URL 自带鉴权，再带一个 Authorization 头过去反而会被拒（实测 HTTPError）。
    所以跨域重定向时把这个头摘掉。
    """

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        new = super().redirect_request(req, fp, code, msg, headers, newurl)
        if new is not None:
            old_host = urllib.parse.urlsplit(req.full_url).netloc
            if urllib.parse.urlsplit(newurl).netloc != old_host:
                new.remove_header("Authorization")
        return new


_OPENER = urllib.request.build_opener(_StripAuthOnRedirect)


def request(path: str, token: str, accept: str = "application/vnd.github+json", raw: bool = False):
    url = path if path.startswith("http") else API + path
    req = urllib.request.Request(
        url,
        headers={
            "Authorization": f"Bearer {token}",
            "Accept": accept,
            "User-Agent": "OpenSysprepTool-CIEvidence/1.0",
        },
    )
    with _OPENER.open(req, timeout=120) as response:
        data = response.read()
    if raw:
        return data
    return json.loads(data.decode("utf-8", errors="replace"))


def runs_for_sha(repo: str, sha: str, token: str) -> list[dict]:
    payload = request(f"/repos/{repo}/actions/runs?head_sha={sha}&per_page=100", token)
    return payload.get("workflow_runs") or []


def filter_runs(runs: list[dict], exclude: set[str], only: set[str]) -> list[dict]:
    """按工作流名过滤。

    独立验收那个作业本身也在同一个 sha 上跑，它要是在这儿等「所有工作流都完成」，
    等到的永远是自己 —— 死锁。所以必须能把发起这次调用的工作流排除掉。
    实测：第一版没做这个过滤，评审模型一眼看出「会把自己算进待完成工作流并卡死」。
    """
    result = []
    for run in runs:
        name = (run.get("name") or "").strip()
        if name in exclude:
            continue
        if only and name not in only:
            continue
        result.append(run)
    return result


def wait_for_completion(
    repo: str,
    sha: str,
    token: str,
    wait_minutes: int,
    exclude: set[str] | None = None,
    only: set[str] | None = None,
) -> list[dict]:
    exclude = exclude or set()
    only = only or set()
    deadline = time.time() + wait_minutes * 60
    started = time.time()
    runs: list[dict] = []
    while True:
        runs = filter_runs(runs_for_sha(repo, sha, token), exclude, only)
        pending = [run for run in runs if run.get("status") != "completed"]
        if runs and not pending:
            return runs
        # 一个运行都没有：可能是流水线还没被触发。给 2 分钟宽限，之后判 missing，
        # 不要拿「还在排队」当借口空等到超时。
        if not runs and time.time() - started > 120:
            _say("2 分钟内没有出现任何运行记录，按 missing 处理")
            return []
        if time.time() > deadline:
            _say(f"等待超时：仍有 {len(pending)} 个工作流没跑完")
            return runs
        _say(f"  等待中：{len(pending)} 个未完成 / 共 {len(runs)} 个")
        time.sleep(20)


def jobs_for_run(repo: str, run_id: int, token: str) -> list[dict]:
    payload = request(f"/repos/{repo}/actions/runs/{run_id}/jobs?per_page=100", token)
    return payload.get("jobs") or []


KEY_LINE = re.compile(
    r"(RESULT:|SMOKE-OK|warning C|error C|tests passed|tests failed|werror\s*:|"
    r"Build succeeded|FAILED|::error::)"
)

# GitHub 会把 run 脚本**原文**先回显一遍，脚本里的 `Write-Host "::error::..."` 因此
# 会以普通日志行的形式出现。第一版只过滤「行首就是 Write-Host」的行，可实际回显是
# 带缩进的（`    Get-Content ... |`），于是大量伪 ::error:: 混进了「真实证据」——
# 独立评审模型直接指出来：证据已失真，不可作为发布矩阵的数据源。
SHELL_ECHO = re.compile(
    r"(\$_\b|\$\(|\$\{|\$LASTEXITCODE|\$code\b|\$log\b|\$xml\b|\$hits|\$work\b|"
    r"Select-String|ForEach-Object|Write-Host|Get-Content|Tee-Object|Join-Path|"
    r"^\s*(if|else|elseif|foreach|try|catch|\}|exit)\b|^\s*\)\s*\|)",
)
ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


def is_script_echo(line: str) -> bool:
    """这一行是不是 run 脚本的原文回显（而不是命令的真实输出）"""
    stripped = ANSI.sub("", line).strip()
    if not stripped:
        return True
    # 真实的 pwsh 输出不会带这些变量/管道原文
    if SHELL_ECHO.search(stripped):
        return True
    # 裸的花括号或管道收尾，多半是脚本的续行
    if stripped in ("}", ")", "|", "{"):
        return True
    # 脚本里的注释也会被回显（例如「# 结果是一条 ::error:: 注解都发不出来」）
    if stripped.startswith("#"):
        return True
    return False


def summarize_job_log(repo: str, job_id: int, token: str, limit: int = 60) -> list[str]:
    try:
        blob = request(f"/repos/{repo}/actions/jobs/{job_id}/logs", token, raw=True)
    except Exception as exc:  # noqa: BLE001
        return [f"（取日志失败：{type(exc).__name__}）"]
    text = blob.decode("utf-8", errors="replace")
    result: list[str] = []
    for raw_line in text.splitlines():
        line = re.sub(r"^\S+Z ", "", ANSI.sub("", raw_line)).strip()
        if not line or not KEY_LINE.search(line):
            continue
        if is_script_echo(line):
            continue
        result.append(line)
    # 去重保序
    seen: set[str] = set()
    unique = [line for line in result if not (line in seen or seen.add(line))]
    return unique[:limit]


def main(argv: list[str]) -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[union-attr]

    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--sha", required=True)
    parser.add_argument("--out", default="evidence.txt")
    parser.add_argument("--conclusion", default="conclusion.txt")
    parser.add_argument("--wait-minutes", type=int, default=25)
    parser.add_argument(
        "--exclude-workflow",
        action="append",
        default=[],
        help="不计入证据的工作流名（可重复）。调用方自己的工作流必须排除，否则会等自己",
    )
    parser.add_argument(
        "--only-workflow",
        action="append",
        default=[],
        help="只计入这些工作流（可重复）。给了它就忽略其它一切工作流",
    )
    parser.add_argument(
        "--json",
        default="",
        help="额外写出结构化 JSON（供 release 矩阵读取，如 docs/acceptance/ci-<sha>.json）",
    )
    try:
        args = parser.parse_args(argv[1:])
    except SystemExit:
        return 2

    token = os.environ.get("GITHUB_TOKEN", "").strip() or os.environ.get("GH_TOKEN", "").strip()
    if not token:
        _say("没有 GITHUB_TOKEN / GH_TOKEN，无法读取运行日志")
        return 2

    # 默认排除调用者自己的工作流：它跑在同一个 sha 上，等它完成等于等自己。
    exclude = set(args.exclude_workflow)
    caller = os.environ.get("GITHUB_WORKFLOW", "").strip()
    if caller:
        exclude.add(caller)
    only = set(args.only_workflow)

    _say(f"collect_ci_evidence: {args.repo}@{args.sha[:7]}")
    if exclude:
        _say(f"  排除工作流：{', '.join(sorted(exclude))}")
    if only:
        _say(f"  只看工作流：{', '.join(sorted(only))}")
    runs = wait_for_completion(args.repo, args.sha, token, args.wait_minutes, exclude, only)
    if not runs:
        with open(args.out, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(f"{args.sha} 上没有任何工作流运行记录\n")
        with open(args.conclusion, "w", encoding="utf-8", newline="\n") as handle:
            handle.write("missing")
        _say("RESULT: 没有运行记录 -> missing")
        return 0

    lines: list[str] = [f"sha {args.sha} 的工作流：{len(runs)} 个"]
    conclusion = "success"
    warning_count = 0
    error_count = 0
    step_evidence: list[str] = []
    jobs_summary: list[dict] = []
    passed_steps = 0

    for run in sorted(runs, key=lambda item: item.get("name", "")):
        status = run.get("conclusion") or run.get("status")
        lines.append(f"\n## {run.get('name')} #{run.get('run_number')} → {status}")
        if status != "success":
            conclusion = "failure"

        for job in jobs_for_run(args.repo, run["id"], token):
            job_status = job.get("conclusion") or job.get("status")
            lines.append(f"### 作业 {job.get('name')} → {job_status}")
            if job_status != "success":
                conclusion = "failure"
            jobs_summary.append(
                {
                    "workflow": run.get("name"),
                    "runNumber": run.get("run_number"),
                    "job": job.get("name"),
                    "conclusion": job_status,
                    "htmlUrl": run.get("html_url"),
                }
            )
            for step in job.get("steps") or []:
                if (step.get("conclusion") or "") != "success":
                    lines.append(f"  步骤失败：{step.get('name')}（{step.get('conclusion')}）")
                    conclusion = "failure"
                else:
                    passed_steps += 1
                    step_evidence.append(f"{run.get('name')} / {job.get('name')} / {step.get('name')}")

            if job.get("status") == "completed":
                snippet = summarize_job_log(args.repo, job["id"], token)
                warning_count += sum(1 for line in snippet if "warning C" in line)
                error_count += sum(1 for line in snippet if "error C" in line)
                lines.extend(f"  {line}" for line in snippet)

    header = [
        f"统计：warning C 行 {warning_count} 条，error C 行 {error_count} 条",
        f"总结论：{conclusion}",
    ]
    with open(args.out, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(header + lines) + "\n")
    with open(args.conclusion, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(conclusion)

    if args.json:
        payload = {
            "schemaVersion": 1,
            "sha": args.sha,
            "conclusion": conclusion,
            "warningCLines": warning_count,
            "errorCLines": error_count,
            "passedStepCount": passed_steps,
            "steps": step_evidence,
            "jobs": jobs_summary,
            "excludedWorkflows": sorted(exclude),
            "onlyWorkflows": sorted(only),
            "collector": "tools/collect_ci_evidence.py",
        }
        folder = os.path.dirname(os.path.abspath(args.json))
        if folder:
            os.makedirs(folder, exist_ok=True)
        with open(args.json, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(payload, handle, ensure_ascii=False, indent=2)
            handle.write("\n")
        _say(f"已写出结构化证据：{args.json}（{len(step_evidence)} 个通过步骤）")

    _say(f"RESULT: {conclusion}（warning C {warning_count} 条 / error C {error_count} 条）")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

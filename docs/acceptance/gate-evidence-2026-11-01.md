# 交付门禁实况：原始输出留档（2026-11-01）

宪章 §11「门禁实况」表里的每一句都可以在这里找到**输出原文**，不是结论。
每条都附复现命令；命令只用 `curl` + 公开 API，不需要 token 的部分已标注。

---

## 1. 分支保护：补之前是关的

```bash
curl -s https://api.github.com/repos/misakano7545/OpenSysprepTool/branches/main
```

补之前（`8bbf1a2` 时点）返回：

```json
"protection": {
  "enabled": false,
  "required_status_checks": { "enforcement_level": "off", "contexts": [], "checks": [] }
}
```

`enabled: false` 是「不合格代码无法合并」不成立的硬证据：CI 当时只是通知，不拦人。

## 2. 分支保护：补之后配了什么

```bash
curl -s https://api.github.com/repos/misakano7545/OpenSysprepTool/branches/main/protection
```

```json
"required_status_checks": {
  "strict": true,
  "contexts": ["build (x64, x64)", "build (x86, Win32)", "review", "clang-format"]
},
"enforce_admins": { "enabled": true },
"required_linear_history": { "enabled": true },
"allow_force_pushes": { "enabled": false }
```

## 3. 直推 main 被拒的原文

造一个空提交、直接推 main：

```bash
git commit --allow-empty -m "chore: 分支保护负例"
git push origin HEAD:main
```

真实输出（`20.205.243.166`，2026-11-01）：

```
remote: error: GH006: Protected branch update failed for refs/heads/main.
remote:
remote: - Changes must be made through a pull request.
remote:
remote: - 4 of 4 required status checks are expected.
To https://github.com/misakano7545/OpenSysprepTool.git
 ! [remote rejected] HEAD -> main (protected branch hook declined)
error: failed to push some refs to 'https://github.com/misakano7545/OpenSysprepTool.git'
```

这是「改走 PR + 必须绿」这条唯一的硬证据。之前同一个动作是**成功**的：

```
remote: Bypassed rule violations for refs/heads/main:
remote: - Changes must be made through a pull request.
```

差别只在 `enforce_admins`：`false` 时仓库 admin 可以绕过，`true` 时绕不过。

## 4. required check 名字写错的后果：死锁

第一次配保护时写的是工作流名 `lint`。合并时报：

```
Required status check "lint" is expected.
```

而 `lint.yml` 里真实存在的 job 叫 `clang-format`：

```bash
curl -s "https://api.github.com/repos/misakano7545/OpenSysprepTool/commits/<sha>/check-runs"
```

```json
[["build (x64, x64)", "success"],
 ["build (x86, Win32)", "success"],
 ["clang-format", "success"],
 ["review", "success"]]
```

`lint` 永远等不到 → 合并按钮永久灰。改成 `clang-format` 后合并成功（`1f16038`）。
**教训**：配 required checks 前先读 `commits/<sha>/check-runs`，别照工作流名猜。

## 5. 独立验收：四个连续 run 的判决原文

```bash
curl -s "https://api.github.com/repos/misakano7545/OpenSysprepTool/issues/1/comments"
curl -s "https://api.github.com/repos/misakano7545/OpenSysprepTool/issues/3/comments"
```

| run | sha | 判决 | 评论里的结论句 |
|---|---|---|---|
| #2 | `1c96208` | reject | 「声称新增并已落地多项门禁，但本 sha 上没有任何 review 链路运行证据」 |
| #3 | `5711bed` | pass | 「本次仅增强集成校验与负例闸门，且有当前流水线证据表明新增『来源头部一致性』检查已实际执行并通过」 |
| #5 | `b49460c` | reject | 「这次改动放宽了既有评审闸门，且缺少本 sha 上 review 链路真实运行证据」 |
| #6 | `f9fe37f` | pass | 「可合入：本次改动有已跑通的 CI 证据，且新增评审器自检已覆盖所宣称修复点」 |

判决模型是 `tw-intl:gpt-5.4`，与实现本仓库的模型不同族；`independent_review.py`
里有一段 `CODER_MODEL_HINTS = ("deepseek",)`，评审模型同族时直接判 `uncertain`。

**#5 那条最值得留档**：它指出的是本仓库自己把规则书放宽了 —— 上一版把「改规则书
一律交给人」换成了提示里的一句话。写代码的一方看不见自己的松绑，这就是第二个模型
存在的意义。改法是把它换成能机械判定的规则（见第 6 节）。

## 6. 规则书被改时，判据是「删没删断言」

`tools/independent_review.py` 的 `deterministic_checks()`：

```python
for path, body in iter_removed_code_lines(diff):
    if path not in PROTECTED_TOOLING:
        continue
    if re.search(r"\bcheck\(|\bREQUIRE\(|\bassert\b|^\s*-\s*name\s*:|^\s*(run|uses|shell)\s*:", body):
        reasons.append(f"在规则书里删掉了断言或工作流步骤（{path}）：`{body[:120]}`")
```

`PROTECTED_TOOLING` = `tools/independent_review.py`、`tools/collect_ci_evidence.py`、
`.github/workflows/review.yml`。三条负例在
`tests/integration/verify_review_tooling.py` 的「规则书里的松绑」一节。

## 7. 「0 warning」从声明变成约束

补之前，`build (x64)` 与 `build (x86)` 的日志里各有 13 条 C4xxx：

```
core\config\ini.cpp(142,34): warning C4305: 'argument': truncation from 'int' to 'const _Elem'
core\config\ini.cpp(142,34): warning C4309: 'argument': truncation of constant value
core\prepare\prepare.cpp(2532,31): warning C4456: declaration of 'defaultHive' hides previous local declaration
deploy\main.cpp(400,59): warning C4100: 'okCount': unreferenced parameter
host\cli_oem.cpp(26,78): warning C4100: 'indent': unreferenced parameter
host\ui\ui_frame.cpp(2285,30): warning C4456: declaration of 'rect' hides previous local declaration
host\ui\tmffun.cpp(699,46): warning C4100: 'wnd': unreferenced parameter
tests\unit\test_prepare.cpp(523,46): warning C4129: 'D': unrecognized character escape sequence
（C4129 共 6 处：523/526/538/544/552/610）
```

`CMakeLists.txt` 当时只有 `/W4` 没有 `/WX`，所以「0 error 0 warning」只是一句话。
现在 `OSP_WERROR` 默认 ON，CI 的 `collect_ci_evidence.py` 每次统计 `warning C` 行数并写进
证据首行。

## 8. 体检验收脚本真的进流水线了

补之前：

```bash
grep -c "verify_checkup.py" .github/workflows/ci.yml   # → 0
ls tests/integration/verify_checkup.py                # → 存在
```

存在但没人跑 = 没有。现在 CI 有「系统体检验收」步骤，先 `ospt --checkup --json`，
再用 `cmd /c ver` 与 `shutil.disk_usage` 当独立来源对照 OS 版本、sysprep 路径、分区容量（±1%）。

## 9. 矩阵记录：三个版本同一尺寸 = 常量

```bash
curl -s "https://api.github.com/repos/misakano7545/OpenSysprepTool/releases" \
  | python -c "import json,sys; [print(a['name'], a['size']) for r in json.load(sys.stdin) for a in r['assets'] if 'matrix' in a['name']]"
```

```
acceptance-matrix-v0.1.0.json 9015
acceptance-matrix-v0.1.1.json 9015
acceptance-matrix-v0.1.2.json 9015
```

三版字节数完全相同 —— 它读的是 `MATRIX_OSES` 这个字面量常量，不是执行结果。
现在改读 `docs/acceptance/real-machine.json` 与 `docs/acceptance/ci-<sha>.json`，
本地实测 `executedCells=0/6`。

## 10. 仍然做不到的：六格真机

Win7 SP1 / Win10 22H2 / Win11 23H2 × x86/x64 一次都没跑过。托管 runner 只有
Windows Server 2022，没有嵌套虚拟化。必须有人开 VM 并把结果写进
`docs/acceptance/real-machine.json`（`tools/acceptance_record.py`）。
在那之前矩阵会一直诚实地显示 `0/6`，这是设计意图，不是 bug。

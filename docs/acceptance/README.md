# 验收记录（不是文档，是数据）

这个目录里的文件**不是**手写的说明文档，而是自动化读的验收数据。判断一个格子「跑没跑过」，
看这里，不看任何一段文字。

| 文件 | 谁写 | 谁读 | 作用 |
|---|---|---|---|
| `real-machine.json` | `tools/acceptance_record.py` | `tests/integration/verify_release.py --emit-matrix` | 六格真机验收记录（系统 × 架构） |
| `ci-<sha>.json` | `tools/collect_ci_evidence.py --json` | 同上 | 该提交的 CI 真实执行证据（通过了哪些步骤） |

## 为什么要有这一层

2026-11-01 复核时发现：矩阵记录是 `verify_release.py` 里写死的一张常量表，
六格永远写「真机项待人工」，v0.1.0 / v0.1.1 / v0.1.2 三个 json 全是 **9,015 字节**——
一模一样，因为它根本不读任何执行结果。那不是验收记录，是免责声明。

现在的规则很简单：

- **没有记录 = 没跑过。** 矩阵会照实写「未执行（无真机记录）」。
- **记录必须带证据。** `--evidence` 指向的文件必须真实存在且非空，`--machine` 必须能认出是哪台机器。
- **记录必须绑定提交。** 自动带上当时 HEAD 的 sha；发版时校验它是不是 tag 的祖先，
  不是的话这条记录作废（防止拿旧版本的测试结果给新版本背书）。
- **同一格只保留最新一条**，被替换的进 `history`，不删不丢。

## 怎么记录一格

```bash
# 1. 在真机/VM 上把这一格的流程跑完，日志留好
# 2. 记下来
python tools/acceptance_record.py \
    --os "Windows 7 SP1" --arch x64 --result pass \
    --machine "VMware 17 / Win7SP1-x64" --operator 你的名字 \
    --evidence evidence/win7-x64-deploy.log \
    --notes "五阶段全跑完，驱动注入后能进桌面"

# 3. 看现在覆盖了几格
python tools/acceptance_record.py --list
```

## 当前状态

见 `python tools/acceptance_record.py --list` 的输出。
本文件不写「已完成几格」这种数字 —— 数字会过期，改成看命令。

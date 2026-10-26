# 提交信息

本仓提交历史的约定。scope 取本仓模块名，别照抄其他仓库的 `panel`、`server`。

## 格式

`type(scope): 描述`

- type 小写：`feat`、`fix`、`chore`、`ci`、`refactor`、`docs`、`test`、`style`、`ui`、`revert`
- scope 取模块目录名：`config`、`log`、`unattend`、`runplan`、`sysinfo`、`regio`、`dism`、`oem`、
  `host`、`ui`、`deploy`、`tests`、`ci`、`docs`
- 跨模块或全局改动不写 scope，别硬凑
- 描述用中文，不以句号结尾
- 破坏性改动在 type 后加 `!`：
  `feat(config)!: 配置结构改为按模块分组——旧 JSON 不再兼容`

## 标题写什么

说清改了什么，不写「优化」「调整」「修复问题」这类看不出内容的词。

- 用 `——` 引出原因或后果：
  `fix(dism): 改为 CreateProcess 调命令行——DismApi 跨版本 ABI 不稳`
- 用 `（）` 标注来源：
  `（宪章 §9.4）`、`（issue #12）`
- 用 `+` 并列多件事：
  `feat(config): 配置模型 + JSON 往返序列化`
- 引用宪章条目时写节号，不写「按文档要求」

## 正文

写「为什么」和根因，不重复标题已经说过的内容。

- 多件事用 `- ` 列点
- 硬换行，行宽控制在 70 列左右
- 不用 `Co-Authored-By`，也不用 `BREAKING CHANGE` footer，破坏性改动用 `!`

## 反例

- `Merge pull request #44 from ...` —— 合并提交不进本仓历史
- `修正提示文字` —— 没有 type
- `docs+fix: 快速开始重排 + 修陷阱` —— 复合 type，拆成两条
- `fix: save config on reboot` —— 本仓提交用中文

# 开工前必读

- 项目宪章在 `docs/CHARTER.md`，是唯一入口；技术决策已定死，不要重新讨论
- 需求基线在另一个目录 `D:\dev\scpt\out\15_spec\`，只读，不要提交进本仓
- 反编译产物**只能读，不能抄**。看懂功能在做什么，然后自己写实现；逐行翻译等于演绎作品
- 宪章第 7 节的 8 项流氓行为任何实现都不得包含，也不要为它们预留接口
- 改代码前先跑 `ctest`，提交前必须全绿；`ctest` 零用例返回 exit 8，不是通过
- 引入任何第三方库前先查许可，只能 MIT / BSL-1.0 / MS-PL 一类，并登记到 `third_party/LICENSES.md`

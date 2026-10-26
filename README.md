# OpenSysprepTool

[![CI](https://github.com/misakano7545/OpenSysprepTool/actions/workflows/ci.yml/badge.svg)](https://github.com/misakano7545/OpenSysprepTool/actions/workflows/ci.yml)
[![Lint](https://github.com/misakano7545/OpenSysprepTool/actions/workflows/lint.yml/badge.svg)](https://github.com/misakano7545/OpenSysprepTool/actions/workflows/lint.yml)
[![Release](https://github.com/misakano7545/OpenSysprepTool/actions/workflows/release.yml/badge.svg)](https://github.com/misakano7545/OpenSysprepTool/actions/workflows/release.yml)
[![最新发行版](https://img.shields.io/github/v/release/misakano7545/OpenSysprepTool)](https://github.com/misakano7545/OpenSysprepTool/releases/latest)

用 C++20 重写的 Windows 系统封装工具，功能对标商业封装工具的完整流程，
**不含任何捆绑、改主页、装扩展、装推广软件等行为**。

> 项目宪章：[`docs/CHARTER.md`](docs/CHARTER.md) —— 任何接手本项目的会话先读这一页。

## 状态

M1–M8 全部完成，当前版本 **0.1.1**。八个里程碑：工程骨架与核心库、系统体检、
unattend.xml 生成、运行计划调度器、部署运行时与 Windows 7 SP1 兼容、驱动注入与
sysprep 编排、主程序配置界面、集成测试矩阵与发版。详见 [`CHANGELOG.md`](CHANGELOG.md)。

M1 起每个里程碑都有**两条并列的验收线，缺一不可**：本地跑通不算完成，CI 不绿也不算完成。

## 下载与安装

从 [Releases](https://github.com/misakano7545/OpenSysprepTool/releases/latest) 取最新版本。
每个版本同时提供四份二进制、一份源码包和一份集成测试矩阵验收记录：

| 文件 | 架构 | 说明 |
|---|---|---|
| `ospt-x64.exe` | x64 | 主程序：配置界面、系统体检、生成 unattend.xml 与运行计划、驱动注入、sysprep 编排 |
| `osptd-x64.exe` | x64 | 部署运行时：在目标机上按运行计划执行（含 WinPE） |
| `ospt-x86.exe` | x86 | 主程序 32 位版 |
| `osptd-x86.exe` | x86 | 部署运行时 32 位版 |
| `OpenSysprepTool-v<版本>-src.zip` | — | 与这四份二进制同源同提交的完整源码（GPL-3.0 要求） |
| `acceptance-matrix-v<版本>.json` / `.md` | — | 集成测试矩阵逐格验收记录（含需人工真机验证的清单） |

**绿色程序，不需要安装**，也没有服务与计划任务。全部产物静态链接（`/MT`），
目标机**不需要装 VC 运行库**；子系统版本固定为 6.1，Windows 7 SP1 及以上可用。
`ospt.exe` 与 `osptd.exe` 在需要管理员的操作前自动请求提权（代码里 `runas`，清单为 `asInvoker`），
其中 `ospt.exe` 正常启动只出现界面窗口，不附带命令行窗口。

### 前置条件

| 目标系统 | 前置 |
|---|---|
| Windows 7 SP1 | **必须已装 SP1**（未装 SP1 时产物起不来）。用 `--inject-drivers` 装驱动前**必须先装 KB4474419**（SHA-2 代码签名支持），否则驱动装不上 |
| Windows 10 22H2 / Windows 11 23H2 | KB4474419 一般随系统自带，仍建议核对一遍 |

驱动二进制**不在本仓库**，另见 `OpenSysprepDriver`（`srs/` 与 `vmd/`）。
本仓库任何地方都不含驱动文件，验收脚本会断言这一点。

## 构建

需要 CMake 3.25+ 与 MSVC 2022（含 Windows SDK；配置界面需要 ATL 组件
`Microsoft.VisualStudio.Component.VC.ATL`，WTL 头文件已随仓库提供）。

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64        # 或 -A Win32
cmake --build build --config Release --parallel
```

产物：

| 文件 | 说明 |
|---|---|
| `build/bin/Release/ospt.exe` | 主程序 |
| `build/bin/Release/osptd.exe` | 部署运行时 |
| `build/bin/Release/osp_unit_tests.exe` | 单元测试 |

## 命令行

```
ospt                                                    打开配置界面（等同 --ui）
ospt --ui [--config <cfg.json>]                         打开配置界面，可带一个初始配置
ospt --selftest                                        配置 JSON 往返自检
ospt --checkup [--json <path>]                          系统体检
ospt --gen-unattend <cfg.json> --out <unattend.xml>     生成应答文件
ospt --gen-runplan <plan.json> --out <run/> [--drives <drives.json>]   生成运行计划
ospt --inject-drivers --image <挂载点> --driver <目录> --dry-run      模式 A 标准驱动包
ospt --inject-drivers --image <挂载点> --srs <目录> --dry-run         模式 B SRS 注册表绑定
ospt --sysprep --dry-run [--image <挂载点>] [--unattend <xml>]        封装编排
ospt --ui-smoke-test [--visible] [--report <json>]      界面冒烟测试
ospt --version                                          版本与源码地址
ospt --help                                             用法

osptd --dry-run --plan <run/>                           逐阶段打印将要执行的动作，不启动进程
osptd --plan <run/> [--log <log.txt>] [--report <report.json>]        真正执行运行计划
osptd --plan <run/> --continue-on-error                 失败不中止，继续后续任务
osptd --win7-compat [--json]                            Windows 7 兼容自述（供闸门脚本比对）
osptd --version                                         版本与源码地址
```

改名、删文件、装驱动、跑 sysprep 这类动作**默认只预览**：界面按钮只改配置，
命令行必须显式加 `--execute` 才会真正落地。`--ui-smoke-test` 在无头环境下会打印
`SMOKE-SKIP` 与原因并返回 0，绝不静默略过。

### 配置界面

**双击 `ospt.exe`（或 `ospt` / `ospt --ui`）就打开配置界面，关掉窗口才退出，且不会附带命令行窗口** ——
`ospt.exe` 是 GUI 子系统（`/SUBSYSTEM:WINDOWS,6.01`），装载器不为它分配控制台。7 个页签：
母盘系统 / 目标系统 / 计划任务 / 部署过程 / 封装过程 / 运行 / 完成。

需要排查输出时显式使用 **`ospt.exe --console`**：保留已有控制台；没有控制台时新建一个，
显示用法并等待回车。`ospt.exe --console --ui` 可同时保留控制台与配置界面。
日常双击或无参数启动始终只有 UI，不需要加这个开关。

#### 需要管理员权限

`ospt.exe` 与 `osptd.exe` 需要**管理员权限**（装驱动、写离线注册表、跑 sysprep 都要管理员）：

- **界面（无参数或 `--ui`）和真正需要管理员的操作**（`--inject-drivers` / `--sysprep` /
  `--execute` / `--plan` 不带 `--dry-run`）启动时自动提权：未提权的会话里启动它，会用
  `runas` 重新启动自己（UAC 可能弹一次），新进程以管理员身份运行，原进程等待其退出并
  返回其退出码。
- **只读开关不自动提权**：`--version` / `--help` / `--selftest` / `--checkup` /
  `--gen-unattend` / `--gen-runplan` / `--ui-smoke-test` / `--win7-compat` / `--dry-run`
  这些不需要管理员，直接跑（CI 也要能跑）。
- 清单保持 `asInvoker`：如果写成 `requireAdministrator`，`CreateProcess` 会在未提权时
  直接拒绝（740），自动提权逻辑就没机会跑。提权是在 `main()` 开头做的。
- 单元测试产物 `osp_unit_tests.exe` 保持 `asInvoker` 且**不自动提权**，任何会话都能直接跑。
- 本机跑验收请用 `tests/tools/run_acceptance_elevated.ps1`：它自己拉一个提权会话把全部闸门
  跑一遍，只需提权一次。

界面本身：

- **文件 → 打开配置… / 另存为配置…**：读写 `cfg.json`（就是 `--gen-unattend` /
  `--gen-runplan` 吃的那种配置文件，同一份模型）。标题栏显示当前文件名，改过没存会带 `*`，
  关窗时会问你要不要先存。载入失败（非法 JSON / 空文件 / schemaVersion 不符）会弹出原因，
  不会崩，也不会悄悄用默认值顶替。
- **工具 → 立即封装体检**：重跑一遍 M2 的采集（等同 `--checkup`）。
- 界面上的按钮**只改配置**，不会真的装驱动、跑 sysprep 或关机；要真正执行仍走命令行
  的 `--execute`。这是刻意的安全边界。
- 界面是原生 Win32 标准控件（页签、编辑框、复选框、下拉框、进度条、列表视图），
  视觉完全自研，不做换肤，不引入任何图片或字体资源。

## 测试与验收

### 本地验收

需要真实执行的部署/驱动操作要求管理员权限；`--dry-run` 与只读生成/校验不需要。
`verify_ui.py` 的常驻关窗检查在提权会话里跑，避免未提权进程向提权窗口发消息时被 UIPI 拒绝。
本机统一入口会自己拉一个提权会话：

```powershell
powershell -ExecutionPolicy Bypass -File tests/tools/run_acceptance_elevated.ps1
powershell -ExecutionPolicy Bypass -File tests/tools/run_acceptance_elevated.ps1 -BuildDir build-x86
```

下面是展开后的命令，供在**已提权的终端**里逐条手跑：

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure

build/bin/Release/ospt.exe --selftest
build/bin/Release/ospt.exe --ui-smoke-test
build/bin/Release/ospt.exe --ui          # 打开配置界面，关窗即退出

build/bin/Release/ospt.exe --checkup --json out.json
python tests/integration/verify_checkup.py out.json

build/bin/Release/ospt.exe --gen-unattend tests/integration/fixtures/m3_cfg.json --out build/m3/unattend.xml
python tests/integration/verify_unattend.py build/m3/unattend.xml tests/integration/fixtures/m3_cfg.json

build/bin/Release/ospt.exe --gen-runplan tests/integration/fixtures/m4_plan.json --out build/m4/run
python tests/integration/verify_runplan.py tests/integration/fixtures/m4_plan.json build/m4/run

python tests/integration/verify_deploy_exec.py build/bin/Release/ospt.exe build/bin/Release/osptd.exe
python tests/integration/verify_win7_compat.py build/bin/Release/ospt.exe build/bin/Release/osptd.exe
python tests/integration/verify_drivers.py build/bin/Release/ospt.exe
python tests/integration/verify_ui.py build/bin/Release/ospt.exe
python tests/integration/verify_elevation.py build/bin/Release/ospt.exe build/bin/Release/osptd.exe build/bin/Release/osp_unit_tests.exe

python tests/integration/verify_release.py --check-tag v0.1.1
python tests/integration/verify_release.py --check-workflow .github/workflows/release.yml
```

| 命令 | 通过标准 |
|---|---|
| `cmake --build build --config Release` | 0 error 0 warning（x64 与 x86 两个架构都要） |
| `ctest --test-dir build -C Release` | 全绿。**零用例时 ctest 返回 exit code 8，视为失败**，不用 `--no-tests=ignore` 掩盖 |
| `ospt.exe --selftest` | 打印 `config roundtrip OK` |
| `ospt.exe --ui-smoke-test` | 退出码 0；7 个配置页全部打开、控件全部响应、菜单与配置读写都能跑通、退出无崩溃 |
| `ospt.exe --ui` | 开出常驻窗口（类名 `OSPTMainFrame`），关窗后退出码 0；`verify_ui.py` 会启动它、等窗口出现、确认 2 秒后仍在运行、再发 `WM_CLOSE` 核对退出 |
| `verify_elevation.py` | 两个产品清单声明 `asInvoker`（由代码按需自动提权）；`ospt.exe` 是 WINDOWS 子系统（不分配控制台）、`osptd.exe` 是 CONSOLE；三者子系统版本仍 6.1；未提权启动 UI 后能返回提权副本的真实结果 |
| `verify_checkup.py` | 全绿；含「OS 版本与 `cmd /c ver` 一致」「sysprep.exe 真实存在」「分区容量偏差 ≤1%」三条硬证据 |
| `verify_unattend.py` | 全绿；含命名空间、元素顺序、父子嵌套、pass 合法性、计算机名规则、AutoLogon 次数、登录阶段落点 |
| `verify_runplan.py` | 全绿；覆盖 5 阶段 × 4 类型 × 4 运行方式 × 3 等待策略 × 4 路径类型 |
| `verify_deploy_exec.py` | 全绿；dry-run 无副作用、等待/超时/中止与回滚都真实执行 |
| `verify_win7_compat.py` | 子系统版本 6.1 且类型与 target 相符（ospt=WINDOWS）、导入表无 Win8+ API、无动态 VC 运行库；外附 4 组篡改负例 |
| `verify_drivers.py` | 全绿；断言仓库里没有任何驱动二进制、没有链接 DismApi.dll、dry-run 零副作用 |
| `verify_ui.py` | 全绿；逐页核对打开情况、统计六类控件响应证据、断言红线控件名不出现 |
| `verify_release.py --check-tag` | tag 去掉 `v` 后与 `core/include/core/version.hpp` 的三段版本号完全一致，否则失败 |

### CI 验收

推送后三个工作流都必须在 `windows-2022` runner 上全绿：

[ci.yml](https://github.com/misakano7545/OpenSysprepTool/actions/workflows/ci.yml)（`x64` 与 `x86` 双架构矩阵）：
Configure → Build → Test → Selftest → Version → Unattend 校验 → Runplan 校验 →
部署运行时验收 → Win7 兼容闸门 → UI 冒烟测试 → M6 驱动注入验收 →
Artifact 内容断言（打印实际字节数与 SHA256，缺失或 0 字节直接失败）→ 上传 `ospt-x64` / `ospt-x86`。

[lint.yml](https://github.com/misakano7545/OpenSysprepTool/actions/workflows/lint.yml)：
固定版本 clang-format（19.1.5），**只检查本次改动涉及的源文件**，不要求历史文件重排。

[release.yml](https://github.com/misakano7545/OpenSysprepTool/actions/workflows/release.yml)（tag 触发）：

| 步骤 | 通过标准 |
|---|---|
| 版本一致性校验 | tag 去掉 `v` 后必须与 `version.hpp` 完全一致，**不一致就整条流水线失败，绝不产出 Release** |
| 发版契约自检 | 四产物、源码包、CHANGELOG 摘要、GPL 说明、人工清单齐备 |
| 双架构构建 | x64 与 x86 各自构建 + 跑关键闸门 |
| 收口本架构两份二进制 | 按发布资产名改名，并核对机器码（0x8664 / 0x14C）与子系统 6.1；四份齐全由发版 job 统一核对 |
| 源码包 | `git archive <tag>`，并核对包内含 `LICENSE`、`third_party/LICENSES.md`、`third_party/WTL/MS-PL.txt`、`version.hpp` |
| 集成测试矩阵 | 3 系统 × 2 架构 = 6 格，每格分开「CI 已证」与「需人工真机验证」，不可跑的部分**显式打印跳过原因**并列出人工清单 |
| 创建 Release | 四份二进制 + 源码包 + 矩阵记录一起挂上去，发布后**回读 assets 核对**，缺一即失败 |

三个工作流都对同一分支启用了 `concurrency` 组，新推送会取消该分支上仍在跑的旧运行。

### 集成测试矩阵

目标系统 × 架构共 6 格。**CI 能证明的只有代码层**（PE 子系统 6.1、无 Win8+ API、
无动态 VC 运行库、单元测试与各里程碑验收脚本全绿）；**真机行为必须人工验证**，
托管 runner 上没有 Win7 SP1 / Win10 22H2 / Win11 23H2，这部分在流水线里逐格打印跳过原因，
不会静默略过。每格的记录见 Release 附带的 `acceptance-matrix-v<版本>.md`。

### 代码风格

```bash
clang-format -i <files>                                 格式化
python tests/tools/check_whitespace_only.py <base> HEAD 证明某提交只有空白/换行变化
```

`.clang-format` 的 `ColumnLimit` 是 110，`SortIncludes` / `ReflowComments` /
`AllowShort*` / `BinPack*` 全部关闭，避免重排 include、重排注释、把多行压成一行、
行内补对齐空格。纯格式化提交用 `check_whitespace_only.py` 校验，
**不要用 `git diff -w`**：clang-format 会合并或拆分手工折行的表达式，
行数一变 diff 无法对齐，`-w` 会报出不存在的内容差异。

## 目录

```
core/            静态库 osp_core，两个 exe 共用
  include/core/  公共头（version.hpp 的三段版本号是发版 tag 的判据）
  config/        配置模型 + JSON 序列化
  log/           文件日志
  sysinfo/       系统体检（M2）
  unattend/      unattend.xml 生成（M3）
  runplan/       运行计划（M4）
  deploy/        计划执行（M5）
  win7/          Windows 7 兼容（M5）
  dism/          DISM 封装（M6）
  srsinject/     SRS 离线注册表注入（M6）
  sysprep/       sysprep 编排（M6）
host/            主程序 ospt.exe（含 ui/ 配置界面，M7）
deploy/          部署运行时 osptd.exe（M5）
tests/unit/      单元测试（doctest）
tests/integration/  各里程碑的验收校验脚本
tests/tools/     开发工具脚本
third_party/     第三方依赖，许可见 third_party/LICENSES.md
docs/            项目宪章等文档
```

## 许可

GPL-3.0，见 [`LICENSE`](LICENSE)。**分发二进制须同时提供源码**：
本仓库即完整源码（<https://github.com/misakano7545/OpenSysprepTool>），
每个 Release 另附一份与二进制同源的源码包；`ospt.exe --version` 与
`osptd.exe --version` 都会打印仓库地址，产物本身就能指出源码在哪。
第三方组件与许可证见 [`third_party/LICENSES.md`](third_party/LICENSES.md)。

## 明确不做

静默安装浏览器扩展、改写主页/默认搜索、破解 Edge Secure Preferences、预置收藏夹、
静默安装推广软件、检测规避安全软件、装内核驱动阻止删除、装机联盟分成。
完整清单与证据位置见宪章第 7 节。

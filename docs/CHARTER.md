# OpenSysprepTool 项目宪章

> 本文件是**新会话的唯一入口**。任何接手本项目的会话（人或 Agent）先读这一页，
> 不要依赖对话历史，历史会丢，磁盘不会。

---

## 0. 项目标识

| 项目 | 值 |
|---|---|
| 名称 | **OpenSysprepTool** |
| 仓库 | https://github.com/misakano7545/OpenSysprepTool.git |
| 本地路径 | `D:\dev\OpenSysprepTool` |
| 分支 | `main` |
| 许可 | **GPL-3.0**（见第 6 节，对依赖有约束） |
| 定位 | 用 C++20 重写一个系统封装工具，功能对标 SC 封装工具 3.0.0.178，**去掉它的全部流氓行为** |

**需求来源在另一个目录**：`D:\迅雷下载\scpt\out\15_spec\`（反编译分析产物）。
那个目录只读，不在本仓库内，也不要提交进本仓库。

---

## 1. 已经定死的技术决策

以下决策已完成论证，**不要重新讨论**，除非发现硬性技术障碍。

| 项目 | 决策 | 依据 |
|---|---|---|
| 语言 | **C++20** | WinPE 里 .NET 是可选组件、WPF 明确不支持 |
| UI | **Win32 + WTL 10 + 原生标准控件**（Direct2D 自绘后置） | 用户 2026-10-24 决策：先用原生控件把 demo 跑起来，初期见效快。SC 是全自绘换肤，那个观感不是现阶段目标 |
| 构建 | **CMake 3.25+ / MSVC 2022** | |
| 运行库 | **静态 /MT** | PE 里不能依赖 VC 运行库。**且这是硬要求**：v14 Redistributable 已明确只支持 Win10/11，Win7 上装不了 |
| 最低目标系统 | **Windows 7 SP1**（x64 + x86），M5 起即要求 | 用户 2026-10-25 决策：不推到 M8。MSVC 官方支持 targeting Win7 SP1；但须 `_WIN32_WINNT=0x0601` 且不得静态导入 Win8+ API |
| 产物 | 主程序 `ospt.exe`（x64+x86）、部署运行时 `osptd.exe`（x64+x86） | 目标系统可能是 x86 Win7；名字避开 SC 缩写 |
| 配置格式 | **JSON**（`nlohmann/json` 单头文件） | 运行计划是嵌套结构，INI 撑不住 |
| 兼容 | 支持**导入** SC 的 `.ini` 配置，不做导出 | 只服务老用户迁移 |
| DISM | **命令行 `CreateProcess`**，不链 `DismApi.dll` | DismApi 跨版本 ABI 不稳 |
| XML | `pugixml` 或手写模板 | unattend.xml 结构固定 |

**原始产品的技术栈**（供参考，不是我们要复刻的）：Delphi XE2+ / VCL / 32 位 / ASPack 2.12 加壳。

---

## 2. 仓库结构（与磁盘一致，改动需同步本节）

```
D:\dev\OpenSysprepTool\
├─ CMakeLists.txt              顶层：C++20、/MT、/W4 /permissive- /utf-8
├─ LICENSE                     GPL-3.0
├─ .gitignore                  build/ build-*/ out/ cmake-build-*/ .vs/ ...
├─ .github/workflows/          CI 与发版流水线（见第 9 节，M1 起必须有）
├─ CHANGELOG.md                版本变更记录（M1 起）
├─ README.md                   项目说明 + CI 徽章（M1 起）
├─ core/                       静态库 osp_core —— ospt 与 osptd 共用
│   ├─ CMakeLists.txt
│   ├─ include/core/           公共头（version.hpp 已就位）
│   ├─ src/                    实现（version.cpp 已就位）
│   ├─ config/                 配置模型 + JSON 序列化
│   ├─ unattend/               unattend.xml 生成
│   ├─ runplan/                任务模型 + 脚本生成
│   ├─ sysinfo/                系统查询
│   ├─ regio/                  注册表 RAII（M9）
│   ├─ prepare/                封装前设置：动作计划 / 执行 / 回滚（M9）
│   ├─ dism/                   DISM 封装（模式 A：.inf 驱动包）
│   ├─ srsinject/              SRS 离线 hive 注入（模式 B：裸 .sys + CDD/Services）
│   ├─ oem/                    OEM 信息
│   └─ log/                    文件日志
├─ host/                       主程序 → ospt.exe
│   ├─ CMakeLists.txt
│   ├─ main.cpp
│   └─ ui/                     WTL + 原生控件
├─ deploy/                     部署运行时 → osptd.exe
│   ├─ CMakeLists.txt
│   └─ main.cpp
├─ tests/
│   ├─ CMakeLists.txt
│   ├─ unit/                   Catch2 或 doctest 单头文件
│   └─ integration/            VM 矩阵（手工/脚本）
├─ third_party/                第三方依赖放这里（见第 6 节许可约束）
└─ docs/
    ├─ CHARTER.md              本宪章的仓库副本（与权威版逐字一致）
    └─ 驱动集说明.md           驱动集的调研与版权说明（见第 6.1 节）
```

**本仓库不含驱动二进制。** 驱动集在独立仓库
**`https://github.com/misakano7545/OpenSysprepDriver`**（本地 `D:\dev\OpenSysprepDriver`），
布局与用法见第 6.1 节与 §8 M6。

**约定**：`core/` 下每个子目录先有 `.gitkeep` 占位，实际实现时再补 CMake 源文件列表。

---

## 3. 构建与产物

```bash
cmake -S . -B build                    # 禁止在源码目录内构建（顶层已强制检查）
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

| 项目 | 值 |
|---|---|
| 产物目录 | `build/bin/`（可执行）、`build/lib/`（静态库） |
| VS 多配置生成器 | 实际在 `build/bin/Release/` |
| Ninja 单配置 | 实际在 `build/bin/` |
| 目标名 | `osp_core`（静态库）、`ospt`、`osptd` |
| 别名 | `osp::core` |
| C++ 命名空间 | `osp::` |

**验收命令里写路径时两个都算对**：`build/bin/Release/ospt.exe`（VS）或 `build/bin/ospt.exe`（Ninja）。

---

## 4. 命名与编码约定

- **C++20**，不用异常做控制流；错误用 `std::expected` 风格或返回码
- 命名：`PascalCase` 类型，`camelCase` 函数，`m_` 前缀成员，`k` 前缀常量
- 命名空间统一 `osp::`，子模块 `osp::config`、`osp::unattend` …
- 所有 Win32 句柄用 RAII 包装（`wil` 或自己写），禁止裸 `HANDLE`
- 所有可能失败的 API 调用必须检查返回值并记日志
- **禁止 `system()`**，一律 `CreateProcess` + 显式参数数组
- 单元测试用 Catch2 或 doctest（单头文件，放 `third_party/`）
- 提交前必须 `ctest` 全绿
- 提交信息用中文或英文均可，但一个提交只做一件事

---

## 5. 需求来源（不要重新逆向）

所有需求已经从反编译产物提取完毕，位于 `D:\迅雷下载\scpt\out\15_spec\`：

| 文件 | 内容 | 用途 |
|---|---|---|
| `可行性论证报告.md` | 论证 + 工作量 + 风险 | 先读这个 |
| `功能规格.md` | 功能清单（按模块） | 需求基线 |
| `SCCHS_full.txt` | SC 全部 UI 字符串（源 28,796 字节 UTF-16LE → 14,398 字符 → 22,508 字节 UTF-8） | 细节查证 |
| `项目宪章.md` | 本文件 | 项目锚点 |

配套证据库（同在 `D:\迅雷下载\scpt\out\`）：

| 路径 | 内容 |
|---|---|
| `02_forms\pascal\*.pas` | 7 个主窗体的组件树（UI 布局图纸） |
| `02_forms\dfm\*.dfm` | 7 个主窗体的 DFM 文本（控件树与坐标） |
| `work\tree\` 下 `kind==dfm` 的 29 个条目 | 全部模块窗体资源（13 个不同类，跨模块有重复；UI 用不到，需要时再查） |
| `06_report\form_events.csv` | 350 条事件绑定 → 代码地址 |
| `14_full_decompiled\` | 291,982 个函数的反编译 C（**只读，不可抄**） |
| `10_最终结论_全量分析.md` | 全量逆向总报告（背景） |

**红线**：反编译产物**只能读，不能抄**。可以看它理解"这个功能在做什么"，然后自己写实现。
逐行翻译反编译 C 代码 = 演绎作品 = 侵权。

---

## 6. 依赖与许可（GPL-3.0 约束）

本项目是 **GPL-3.0**，这意味着：

1. **分发二进制必须同时提供源码**（或书面要约）。`--version` 里应打印仓库地址。
2. **不能链接 GPL 不兼容的库**。静态链接尤其严格。
3. Windows SDK / MSVC 运行库适用"系统库例外"，不受影响。

允许使用的依赖（已确认 GPL-3.0 兼容）：

| 库 | 用途 | 许可 | 状态 |
|---|---|---|---|
| `nlohmann/json` | 配置 JSON | MIT | ✅ 单头文件 |
| `pugixml` | unattend.xml | MIT | ✅ |
| WTL 10 | UI 框架 | MS-PL | ✅ 头文件库 |
| Catch2 或 doctest | 单元测试 | BSL-1.0 / MIT | ✅ 单头文件 |
| `wil` | Win32 RAII | MIT | ✅ 可选 |

### 6.1 外部驱动仓库（不进本仓库）

驱动集放在**独立仓库 `OpenSysprepDriver`**，**本仓库不含任何驱动二进制**。

- 远端：`https://github.com/misakano7545/OpenSysprepDriver`
- 本地：`D:\dev\OpenSysprepDriver`（与主仓库并列）

理由：那批驱动的著作权归 Intel / AMD / NVIDIA / Marvell / VIA / SiS / JMicron / ITE / ALi /
Microsoft 等各权利人，**授权状态无法逐一确认**。放进 GPL-3.0 仓库会把许可讲混
（GPL-3.0 只覆盖本项目代码，无权覆盖第三方驱动），且一旦收到 takedown 会牵连整个项目。
独立仓库把风险隔离，本仓库保持纯 GPL-3.0。

- 驱动仓库内容与版权声明：见 `docs/驱动集说明.md`
- 本仓库**不链接、不内嵌**任何驱动文件；运行时只按路径读取用户提供的驱动目录
- `--srs <dir>` / `--driver <dir>` 的默认值**不得**指向仓库内的任何路径

**禁止引入**：任何 GPL 不兼容的库（专有 SDK、AGPL 混用需单独评估、带广告条款的库）。

所有第三方代码放 `third_party/`，并在 `third_party/LICENSES.md` 记录来源与许可。

---

## 7. 绝对不做的事（产品红线）

这 8 项是原软件被判为流氓的全部原因。任何实现都不得包含：

| 禁止项 | 原实现位置（仅作识别参考） |
|---|---|
| 静默安装浏览器扩展 | `bener.exe` |
| 改写浏览器主页 / 默认搜索 | `bcfgs` |
| 破解 Edge Secure Preferences | `EdgeCipher` |
| 预置浏览器收藏夹 | `DATA_133` |
| 静默安装第三方推广软件 | `PUBCE` / `FUNR` / `FUNS` |
| 检测/规避安全软件 | `0x7E75A0` |
| 装内核驱动阻止用户删除 | `ScProtect.sys` / `Scufd.sys` |
| 装机联盟分成 / 渠道号 | `lm.sysceo.cn/apps/getaid` |

**"防流氓封装方式"不实现。** 它的真实语义是"防止别的装机员动我的推广包"，没有推广就没有需求。
如果确实需要防篡改，用**用户态 MD5 校验**（原软件也有：`Page_Tampering` / `Items_FileMd5`）。

---

## 8. 里程碑与验收标准

每个里程碑必须**可自动验证**，否则不算完成。
下面命令里的 `build/bin/ospt.exe` 在 VS 多配置下是 `build/bin/Release/ospt.exe`。

**两条并列的验收线，缺一不可**：
1. **本地验收** —— 下面各里程碑列出的命令
2. **CI 验收** —— 推送后 GitHub Actions 必须全绿（见第 9 节）

CI 不绿不算完成，即使本地跑通了也不算。反过来，本机环境不可用时以 CI 为准。

### M1 — 骨架与核心库（1 周）

**交付**
- CMake 工程 configure + build 通过，产出空壳 `ospt.exe` / `osptd.exe`
- `core/config`：配置模型 + JSON 往返序列化
- `core/log`：文件日志
- `ospt --selftest`：跑一遍 JSON 往返并打印结果
- **`.github/workflows/ci.yml`：push 即跑，绿勾，artifact 能下到 `ospt.exe`**
- `README.md`（含 CI 徽章）+ `CHANGELOG.md`（0.1.0 条目）

**验收**
```
cmake --build build --config Release        → 0 error
ctest --test-dir build -C Release           → 全绿
build/bin/ospt.exe --selftest               → 输出 "config roundtrip OK"
```

**CI 验收（与本地并列，不能只看本地）**
```
推送到 origin/main 后，GitHub Actions 的 CI 工作流必须全绿
下载 artifact "ospt-x64"，解压后有 ospt.exe 与 osptd.exe
ospt.exe --selftest 在 CI 里也跑过且通过
```

> **M1 的历史备注**：M1 开工时本机缺 Windows SDK（见第 10 节），当时
> `windows-2022` runner 是唯一可信的构建验证手段，所以先上 CI 再修本地环境。
> 该路径已被验证有效，本地环境随后补齐（MSVC 14.44 + SDK 10.0.26100）。
> 后续里程碑两条线并行即可，不再有主次之分。

### M2 — 系统体检（1 周）

**交付**：`core/sysinfo` + 体检报告数据结构
检查项：系统版本/位宽、sysprep 可用性、分区容量、必备文件、克隆版检测、杀软状态、启动项

**验收**
```
build/bin/ospt.exe --checkup --json out.json
python tests/integration/verify_checkup.py out.json
```
校验脚本要能证明：本机 OS 版本与 `winver` 一致、sysprep 路径真实存在、分区容量与资源管理器一致（±1%）。

### M3 — unattend.xml 生成（1 周）

**交付**：`core/unattend`，覆盖 20+ 配置项

**验收**
```
build/bin/ospt.exe --gen-unattend cfg.json --out unattend.xml
python tests/integration/verify_unattend.py unattend.xml
```
校验脚本按微软 schema 校验命名空间与元素顺序，并断言：计算机名规则、AutoLogon 次数、FirstLogonCommands 条数与输入一致。

### M4 — 运行计划调度器（1.5 周）

**交付**：`core/runplan` — 5 阶段 × 4 任务类型 × 4 运行方式 × 3 等待策略 × 4 路径类型

**验收**
```
build/bin/ospt.exe --gen-runplan plan.json --out run/
python tests/integration/verify_runplan.py plan.json run/
```
断言：`RunSynchronous` 命令条数与阶段匹配、路径解析在模拟驱动器表上正确、等待策略映射到正确的
**执行标志 + 等待方式组合**。

> **口径修正（2026-10-24）**：原文写"等待策略映射到正确的 `CreateProcess` 标志"，这句在 Win32 层面
> 不成立 —— `CreateProcess` 的 `dwCreationFlags` 里**没有**"等待"这个语义，等待由调用方
> `WaitForSingleObject(hProcess, …)` 决定。三种策略的正确映射是：
>
> | 等待策略 | `dwCreationFlags` | 等待方式 |
> |---|---|---|
> | 不等待 | `CREATE_NEW_PROCESS_GROUP`（便于后续可选的整组终止） | 立即 `CloseHandle`，不 `Wait` |
> | 等待退出 | `0` | `WaitForSingleObject(hProcess, INFINITE)` + `GetExitCodeProcess` |
> | 等待超时 | `0` | `WaitForSingleObject(hProcess, timeoutMs)`，超时按配置决定终止或继续，两种结果都要写进日志 |
>
> 验收脚本断言的是**这张表的两列**，不是单个标志。`verify_runplan.py` 校验生成的计划文件，
> 运行时实际传的标志由 `tests/unit/test_runplan.cpp` 覆盖。

**已实现并验收（2026-10-25，`9505fdc`）**

验收命令逐条实测通过：

```
ospt --gen-runplan build/m4/plan.json --out build/m4/run/
  → exit 0；产出 runplan.json + 5 个阶段脚本（preinstall/install/postinstall/logon/desktop），
    共 17 个任务
python tests/integration/verify_runplan.py build/m4/plan.json build/m4/run/
  → RESULT: ALL PASSED (274 checks)
ctest --test-dir build -C Release
  → 1/1 passed（doctest 37 cases / 25,529 assertions，含
    TEST_CASE("every one of the 960 combinations resolves to the documented mapping")）
```

两列表在实现与产物里都真实落地（不是只写在注释里）：

- `runplan.hpp` 定 `WaitMapping{creationFlags, creationFlagsName, waitMode, timeoutMs}`，
  `waitMapping()` 返回映射；`runplan.cpp` 见 `mapping.creationFlags = CREATE_NEW_PROCESS_GROUP`
- 序列化进 `runplan.json`：`wait='noWait'` → `creationFlags=512`（`CREATE_NEW_PROCESS_GROUP`）、
  `creationFlagsName='CREATE_NEW_PROCESS_GROUP'`；`wait` / `custom` → `creationFlags=0`
- `custom` 附带 `timeoutMs=30000` 与 `timeoutAction='continue'`（超时后终止还是继续两种都在）
- 运行方式 → `showWindow`：`hidden=0 (SW_HIDE)`、`normal=1 (SW_SHOWNORMAL)`、
  `minimized=2 (SW_SHOWMINIMIZED)`、`maximized=3 (SW_SHOWMAXIMIZED)`

**路径解析不依赖本机盘符 —— 已用反例证明**（本机只有 C: 与 D:）：

| 驱动器表 | `taskCount` | 产物里出现的盘符 |
|---|---|---|
| fixture 的 5 盘（C…G） | 17 | C, D, E, F, G |
| 自造假表 3 盘（Z/Y/X） | 12 | Z, Y, X |
| 自造假表但卷标不匹配 | — | **报错退出**：`路径类型 usbDrive 在驱动器表里找不到候选（卷标要求: OSPT_USB）` |

第三行是关键反证：卷标对不上时**大声失败**，而不是回退到本机真实盘符。
`allDrives` 按表展开（5 盘 → 5 条、3 盘 → 3 条），任务总数因此随表变化。

`searchScope: systemPath` 是**绝对路径直通**：目标按原样使用、不查驱动器表，
产物里以 `resolvedFromDriveTable=false` 显式标记。名字容易误读成"解析到系统盘"，
实际语义是"已是绝对路径"。

### M5 — 部署运行时（2 周）

**交付**：`osptd.exe` — 单页进度 UI + 阶段执行 + 失败回滚，**在 Windows 7 SP1 x64 与 x86 上可运行**

**验收**
```
build/bin/osptd.exe --dry-run --plan run/   → 每阶段打印将要做什么，退出码 0
python tests/integration/verify_win7_compat.py build/bin/Release/osptd.exe
                                             → RESULT: ALL PASSED
```
真机验证：VM 里跑一遍，进度条走完、日志完整、失败时能回滚。

#### M5.1 Windows 7 支持（用户 2026-10-25 决策，不推迟到 M8）

**为什么放在 M5**：`osptd.exe` 是唯一在**目标系统上运行**的产物。目标系统可能是 32 位
Win7 SP1，所以 Win7 兼容性对它是硬需求，不是锦上添花。主程序 `ospt.exe` 只在操作员机器上跑，
可以后置，但同一份 `core/` 静态库要两个目标都能编，所以一起管。

**三条硬要求**

| # | 要求 | 做法 |
|---|---|---|
| 1 | 编译期拦住 Win8+ API | `WINVER=0x0601`、`_WIN32_WINNT=0x0601`、`NTDDI_VERSION=0x06010000`，在 `sdkddkver.h` 之前定义。头文件会直接编译报错，这是第一道闸 |
| 2 | 不得静态导入 Win8+ API | 若确需某个 Win8+ API，只能 `GetProcAddress` 动态取 + 有 Win7 回退路径，并登记到白名单 |
| 3 | 静态 `/MT` 不得破 | 产物不得出现 `VCRUNTIME140.dll` / `MSVCP140.dll` / `ucrtbase.dll` / `api-ms-win-*` 的动态导入 |

要求 3 是硬性的原因：微软已明确 **v14 Redistributable 只支持 Win10/11**，
Win7 上根本装不上。所以一旦 `/MT` 被破坏，Win7 直接失效。

**自动化闸门 `verify_win7_compat.py`**（M5 必交付，进 CI）

不需要 Win7 虚拟机，纯静态分析 PE 即可，因此能在 `windows-2022` runner 上跑：

1. 解析 `OPTIONAL_HEADER`，断言 `MajorSubsystemVersion.Minor == 6.1`
   （当前实测是 **6.0**，因为 `_WIN32_WINNT` 未设置，属待修项）
2. 解析导入表，逐个函数对照 Win7 SP1 白名单；不在白名单且不在"动态解析白名单"里的**判失败**
3. 断言导入表里没有 `VCRUNTIME140.dll` / `MSVCP140.dll` / `ucrtbase.dll` / `api-ms-win-*`
4. x64 与 x86 **两个架构都要查**

**2026-10-25 对现有产物的实测基线**（`ospt.exe` / `osptd.exe` / `osp_unit_tests.exe`）：

| 检查项 | 实测 |
|---|---|
| 导入表里的 Win8+ API | **0 个** ✓ |
| `WaitOnAddress` / `WakeByAddressSingle` / `WakeByAddressAll`（VS2022 STL 对 Win7 的最大风险点） | **0 处** ✓ |
| `VCRUNTIME140` / `MSVCP140` / `ucrtbase` / `api-ms-win-*` | **0 处** ✓（`/MT` 已生效，`osptd.exe` 只导入 KERNEL32） |
| `GetSystemTimePreciseAsFileTime`（Win8+） | 字符串出现 1 次但**不在导入表** → `GetProcAddress` 带回退，**安全** |
| `GetFileInformationByHandleEx` | Vista+，**安全** |
| `_WIN32_WINNT` | **未设置** ❌ 头文件不拦 Win8+ API |
| Subsystem 版本 | **6.0（Vista）** ❌ 应为 6.1 |

**所以现状是"碰巧能跑"，不是"保证能跑"** —— 第一道闸（要求 1）还没装。
M5 的任务就是把它变成有保证、且能被 CI 持续验证。

**Win7 端的前提补丁**（写进 README 与发布说明，M5 文档化、M8 实测）

| 补丁 | 何时需要 |
|---|---|
| **SP1** | 无条件必需。MSVC 只支持 targeting Win7 **SP1**，RTM 不在支持范围 |
| KB4474419（SHA-2 代码签名） | 若要**安装驱动**（M6）则必需；2019 年后 Win7 必须装它才认 SHA-2 签名的驱动 |
| KB2533623 | 仅当代码使用 `SetDefaultDllDirectories` / `AddDllDirectory` / `LoadLibraryEx` 的 `LOAD_LIBRARY_SEARCH_*` 标志时。当前代码未用；若将来为防 DLL 劫持而采用，则必须检查此补丁并在缺失时降级 |
| KB2999226（通用 C 运行库） | **不需要** —— 这是 `/MD` 才需要的；本项目静态 `/MT`，天然规避 |

**Win7 上不要碰的 API 与写法**

| 避免 | 改用 |
|---|---|
| `WaitOnAddress` / `WakeByAddressSingle` / `WakeByAddressAll` | Win8+。STL 在 `_WIN32_WINNT=0x0601` 下会走条件变量回退路径 |
| `CreateFile2` | `CreateFileW` |
| `GetSystemTimePreciseAsFileTime` | `GetSystemTimeAsFileTime`（或 `GetProcAddress` + 回退） |
| `PathCch*` 系列 | shlwapi 的 `Path*` 系列（`PathCombine` 等），或自己拼 |
| `SetThreadDescription` | 无替代，仅调试用，靠 `GetProcAddress` 可选调用 |
| `CopyFile2` | `CopyFileW` |
| `GetThreadInformation` | 避免 |
| `SHGetKnownFolderPath` | Vista+，**可用** |
| `RegDeleteTree` / `RegLoadKey` | **可用**（M6 要用 `RegLoadKey`） |

#### M5.2 已实现并验收（2026-10-25，`1178515` + `3e6357c` + `30b630b`）

**x86 矩阵立刻抓到一个只有 32 位进程才会出现的真 bug —— WOW64 文件系统重定向**

32 位进程在 64 位 Windows 上访问 `%SystemRoot%\System32` 会被**静默重定向到 `SysWOW64`**，
而 `sysprep.exe` / `dism.exe` / `bcdedit.exe` 都是 **64 位专有**工具，在 `SysWOW64` 里根本不存在。
于是「其实有」被报成「不存在」：

```
x86 上失败的断言：CHECK( report.sysprep.available == !report.sysprep.exePath.empty() )
实测：SysWOW64 下确实没有 sysprep.exe 与 bcdedit.exe
```

修法（`core/win7`）：

- `ScopedWow64FsRedirection` —— RAII，进作用域关掉重定向、离开自动还原
- `probeFileNative()` —— 探测这类路径前先关重定向，`sysinfo` 的三处探测全部改走它
- **`Wow64DisableWow64FsRedirection` / `Wow64RevertWow64FsRedirection` 只能动态取** ——
  它们只存在于 **64 位** Windows 的 kernel32 里，在 32 位系统上静态导入会让进程**直接加载失败**；
  取不到恰好说明本系统没有重定向，什么都不用做

这条 bug 在主程序上只会表现为「体检说 sysprep 不可用」，用户看不出是重定向问题 ——
**没有 x86 矩阵就不可能发现**。这是 §12「x86 分阶段」决策的直接回报。

**第一次 CI（run #19）两个架构各挂一处、且是两处不同的 bug**

| 架构 | 挂在哪一步 | 真因 |
|---|---|---|
| x64 | 部署运行时验收 | 新增验收脚本漏了 `sys.stdout.reconfigure(encoding="utf-8")`，runner 上 Python stdout 是 cp1252，打印中文抛 `UnicodeEncodeError` |
| x86 | Test（Build 后立刻） | WOW64 重定向（上一条） |

x64 那处的**次生伤害**值得记：Traceback 走 stderr 而 CI 步骤只 Tee 了 stdout，
日志里只剩一句 `Process completed with exit code 1`，全部 `::error::` 都没打出来 —— 排查时信息量为零。

**逐条实测验收**（本地，两个架构各跑一遍）

```
x64  configure exit 0 / build 0 error 0 warning
x86  configure exit 0 / build 0 error 0 warning

PE 实测      build/bin/Release/osptd.exe      machine=0x8664  subsystem=6.1
             build-x86/bin/Release/osptd.exe   machine=0x014c  subsystem=6.1
             （Subsystem 原为 6.0，/SUBSYSTEM:CONSOLE,6.01 钉死后正确）

Win7 闸门     x64  RESULT: ALL PASSED (23 checks，1 项跳过)
             x86  RESULT: ALL PASSED (23 checks，1 项跳过)
             跳过的 1 项 = ospt.exe 的自述交叉比对（ospt.exe 不提供 --win7-compat，
             只有 osptd.exe 提供），带了明确原因，不是静默略过
             动态解析白名单 2 条，均写明起始系统与 Win7 回退路径：
               kernel32.dll!GetSystemTimePreciseAsFileTime
               kernel32.dll!GetTempPath2W

Win7 闸门负例  RESULT: ALL PASSED (4 组篡改全部被闸门拦下)
             子系统 6.1→6.0 / KERNEL32→ucrtbase / ADVAPI32→api-ms-win-x /
             WriteConsoleW→WaitOnAddress(Win8+)，每处都被拦下且报错信息可定位

部署运行时    ospt --gen-runplan → exit 0
             osptd --dry-run --plan → exit 0，逐任务打印
               命令 / 等待策略（含自定义 30000ms 与「动作：继续」）/
               标志（CREATE_NEW_PROCESS_GROUP (0x00000200)）/ 窗口（SW_* = 0/1/2/3）/
               路径解析来源（经驱动器表解析，命中 E:\）
             verify_deploy_exec.py → ALL PASSED (48 checks)
               覆盖失败场景：中止后续、留反序清理清单、记录中止位置与退出码

WOW64 修复验证  x64 --checkup 与 x86 --checkup 的 JSON 逐字段比对：
               33 个字段，差异 0 个
               （未修前 x86 会把 sysprep 报成不存在，两者必然不一致）

ctest / 单测    x64  1/1 passed；53 cases / 25,996 assertions
               x86  1/1 passed；53 cases / 25,996 assertions（与 x64 完全相同）

全链路回归      M2 verify_checkup      85 checks  ALL PASSED
               M3 verify_unattend    121 checks  ALL PASSED
               M4 verify_runplan     274 checks  ALL PASSED
               M5 verify_deploy_exec  48 checks  ALL PASSED

CI  run #19（1178515）failure → #20（3e6357c）success → #21（30b630b）success
    Lint #10 / #11 / #12 均 success
    artifact：ospt-x64 546,432 B / ospt-x86 486,552 B
```

**两个已知的不对称（不阻塞，留作 M6/M7 参考）**

1. **`ospt.exe` 没有 `--win7-compat` 自述** —— 只有 `osptd.exe` 有。`ospt.exe` 的 Win7 合规
   仍由 PE 级三项检查覆盖（Subsystem / 导入表 / 无动态 VC 运行库），所以不是漏洞，
   但少了一层「编译期意图 vs 实测值」的交叉比对。M6 顺手补上最省事。
2. **`/SUBSYSTEM:CONSOLE,6.01` 在顶层写死** —— 对所有 exe 生效。M7 若把 `ospt.exe` 改成
   GUI 子系统（`/SUBSYSTEM:WINDOWS`），会与这条冲突，届时需要按 target 区分。

### M6 — 驱动注入与 sysprep 编排（1.5 周）

**交付**：`core/dism` + `core/srsinject` + `core/sysprep`

**两种注入模式**（来自 SC 驱动集的实测，见 `out/15_spec/驱动集说明.md`）：

| 模式 | 输入形态 | 实现路径 |
|---|---|---|
| A 标准驱动包 | `.inf` + `.sys` + `.cat` | `dism /image:<挂载点> /add-driver:<inf> /recurse` |
| B SRS 注册表绑定 | 裸 `.sys` + `Drv.reg` / `Drv.pesrs`（**无 .inf/.cat**） | 离线 hive 注入 |

模式 B 是 SC 驱动集的实际形态，**DISM 用不上**。步骤：

```
1. RegLoadKey        挂载 <目标>\Windows\System32\config\SYSTEM 到临时键
2. 复制              <name>.sys → <目标>\Windows\System32\drivers\
3. RegSetValueEx     ControlSet00x\Services\<name>
                       Type=1(Kernel Driver) / Start=0(Boot) 或 1(System) / ImagePath
4. RegSetValueEx     ControlSet00x\Control\CriticalDeviceDatabase\<硬件ID>
                       ClassGUID（存储控制器类 {4D36E97B-...}）+ Service
5. RegUnLoadKey      卸载
```

**验收**
```
build/bin/ospt.exe --inject-drivers --image W:\ --driver D:\drivers --dry-run
build/bin/ospt.exe --inject-drivers --image W:\ --srs D:\srs --dry-run
build/bin/ospt.exe --sysprep --dry-run
```
`--dry-run` 打印完整命令行与将写入的注册表项，与微软文档一致。
模式 B 的 `--dry-run` 要打印出每个 `RegSetValueEx` 的键路径与值。

### M7 — 主程序 UI（3 周）

**交付**：多标签配置界面，**原生 Win32 标准控件**（WTL 薄封装），先出可用 demo

用户 2026-10-24 决策：先用原生控件，初期做 demo 更快。Direct2D 自绘与换肤**后置**为独立里程碑
（M7 之后，视需要再定），不进 M7 的交付范围。

**验收**：手工走查清单（对照 DFM 图纸核对字段与交互，不比对视觉），加上
```
build/bin/ospt.exe --ui-smoke-test   → 所有配置页能打开、控件能响应、退出无崩溃
```

### M8 — 集成测试矩阵（2 周）

**交付**：Win7 SP1 / Win10 22H2 / Win11 23H2 × x86 / x64，各跑一遍完整封装流程。

M5 已经用静态闸门守住 Win7 的**代码层**兼容（Subsystem 6.1 + 导入表无 Win8+ API + 无动态 VC 运行库），
M8 负责**真机层**验证：在真实 Win7 SP1（x86 与 x64）上确认能起来、能执行计划、能装驱动。

Win7 真机验证前先确认前提补丁（见 §8 M5.1）：**SP1 必需**；装驱动另需 **KB4474419**。

验收记录要包含每台 VM 的：系统版本与 SP、产物架构、计划执行日志、失败回滚是否生效。

### M9 — 封装前设置应用（1 周）

**交付**：配置界面里那些**只被 JSON 存下来、没有执行者**的字段，落成显式动作。

问题来源：M7 把配置页按 DFM 图纸铺完了，但 `sysprep.cleanTemp`、`optimization.closeNumLock`、
`deploy.enableAdministrator` 这些开关只进了 `cfg.json`，没有任何代码读它们去改系统。
对标产品（SC / ES5P）在封装前会真做这些事，否则这些开关等于装饰。

**模型**（`core/prepare`）：

| 动作类型 | 落地方式 | 可回滚 |
|---|---|---|
| `registrySet` / `registryDelete` | `core/regio` 的 RAII 封装，HKLM 固定 64 位视图 | ✅ 记旧值 |
| `runCommand` | `CreateProcess`（复用 `dism::runCommand`），**不用 `system()`** | ❌ |
| `copyTree` / `moveTree` | `std::filesystem`，目标已存在时跳过并告警 | ✅ 记路径 |
| `makeDirectory` / `deleteTree` | 同上；`deleteTree` 只清空内容，保留目录本身 | 建目录 ✅ / 清空 ❌ |

**三道安全边界**：

1. `--dry-run` 只读配置与环境变量，**不碰注册表、不碰文件**；`--prepare` 不带开关只打印计划并返回 2
2. 真实执行必须显式 `--execute`，且只有这时才自动提权（`--dry-run` 与 `--prepare-selftest` 不提权）
3. 可回滚动作写进 journal（默认 `ospt-prepare-journal.json`），`--rollback` 逆序还原；
   不可回滚的动作在计划里逐条标注

**本里程碑落地 11 个动作**：关闭小键盘（`HKU\.DEFAULT` 的 `InitialKeyboardIndicators=2`）、
开启休眠（`powercfg /hibernate on`）、关闭关机事件跟踪（`ShutdownReasonOn` / `ShutdownReasonUI`）、
虚拟内存换盘（`PagingFiles` 写成 `<盘>:\pagefile.sys 0 0`）、同步 IE 收藏夹到默认用户、
启用 Administrator、转移桌面文件到公共桌面、清空用户与 Windows 临时目录、`sfc /purgecache`。

**明确推迟的 12 项**（`--prepare` 的 deferred 段逐条写明原因，不静默略过）：
我的文档转移、IE 主页同步（两项都要离线加载 `Users\Default\NTUSER.DAT`，归 M10）、
自动 IP（配置模型缺 IP/掩码/网关/DNS 字段）、清理 SRS、卸载本机驱动（归驱动模块）、
任务栏固定、智能分辨率、`rebootAfterRun`（要改 unattend 生成器）、
文件防篡改与 MD5 清单（M10）、禁止虚拟机部署（由 `osptd` 在目标机判定）、
`loadSrsDrivers`（M6 的 `--inject-drivers --srs` 已覆盖）、`perServerLicensing`（unattend 未写 Licensing 组件）。

**验收**：

```
python tests/integration/verify_prepare.py build/bin/Release/ospt.exe
    → dry-run 零副作用、11 个动作与配置逐条对应、推迟项都有原因、坏配置退出码 1
    → --prepare-selftest 写入三种值类型并回滚（不需要管理员）
    → 提权会话：--prepare --execute 写 closeNumLock，--rollback --execute 还原
```

`--prepare-selftest` 只碰 `HKCU\Software\OpenSysprepTool\SelfTest`，所以 CI 的非管理员
步骤也能证明执行器与回滚链路真的能跑，而不是只打印一段文本。

---

## 9. 持续集成与发版

**从 M1 起就必须有 CI，不能等做完再补。**

### 9.1 为什么 M1 就要上

1. **CI 是构建的权威判据。** M1 开工时本机缺 Windows SDK，`windows-2022` runner
   预装 VS 2022 + Windows SDK + CMake，push 上去就能编译——CI 当时是唯一能验证构建的路径。
   本地环境后来补齐了，但「CI 绿勾才算完成」这条规则保留：本机可能装错、可能污染缓存，
   runner 是干净环境，更可信。
2. **越晚接入成本越高。** M1 接入只是一个 yml；M7 接入要同时处理 WTL 头文件、资源编译、
   图标、可能的代码签名，改动面大得多。
3. **绿勾是合并前提。** 没有 CI 就没有"这个提交没破坏东西"的证据，
   后面 8 个里程碑的回归全靠人肉盯。

### 9.2 Runner 与工具链

| 项目 | 值 |
|---|---|
| Runner | `windows-2022`（预装 VS 2022、Windows SDK、CMake、Ninja） |
| 架构矩阵 | M1–M2 只 `x64`；M3 起加 `x86`；M8 做 Win7 SP1 兼容验证 |
| 生成器 | `Visual Studio 17 2022`（与本地一致，避免两套构建行为） |
| CMake | 用 runner 自带的，不额外下载 |

### 9.3 工作流划分

| 文件 | 触发 | 做什么 |
|---|---|---|
| `.github/workflows/ci.yml` | push 任意分支、PR | configure → build → ctest → selftest → 上传 artifact |
| `.github/workflows/release.yml` | 推 `v*` tag | 校验版本号 → 构建 → 打包 → 建 GitHub Release |
| `.github/workflows/lint.yml` | push、PR | clang-format 检查（M2 起启用） |

### 9.4 `ci.yml` 骨架（M1 实现目标）

```yaml
name: CI
on: [push, pull_request]

jobs:
  build:
    runs-on: windows-2022
    strategy:
      fail-fast: false
      matrix:
        arch: [x64]                 # M3 起加 x86
    steps:
      - uses: actions/checkout@v4

      - name: Configure
        run: cmake -S . -B build -G "Visual Studio 17 2022" -A ${{ matrix.arch }}

      - name: Build
        run: cmake --build build --config Release --parallel

      - name: Test
        run: ctest --test-dir build -C Release --output-on-failure

      - name: Selftest
        run: .\build\bin\Release\ospt.exe --selftest

      - uses: actions/upload-artifact@v4
        with:
          name: ospt-${{ matrix.arch }}
          path: build/bin/Release/*.exe
          if-no-files-found: error
```

**硬性要求**
- `ctest` 失败必须让 job 失败，**禁止** `continue-on-error`
- `if-no-files-found: error` —— 产物缺失要报错，不能静默通过
- 构建日志里 `/W4` 的警告必须可见，不许 `2>nul` 吞掉
- 不用 `actions/cache` 缓存 `build/`（CMake 缓存跨 runner 复用会出诡异问题），
  只在构建明显变慢后再考虑 ccache

**⚠ 已知陷阱：`ctest` 零用例会返回 8**

`ctest` 在找不到任何测试时，默认行为是报错退出（exit code 8），
不是静默通过。当前 `tests/CMakeLists.txt` 只有注释、没有 `add_test`，
所以 **M1 一上来 Test 步骤就会红**。

两种处理，二选一，不要用第三种：
- **推荐**：M1 就写第一个单元测试（例如 `version.hpp` 的版本字符串断言），
  让 `ctest` 有真用例可跑。M1 交付里本来就要求 JSON 往返，顺手加个测试。
- 过渡：`ctest --test-dir build -C Release --output-on-failure --no-tests=ignore`，
  但**必须在 M1 结束前去掉 `--no-tests=ignore`**，否则整个项目会带着
  "零测试也算绿"的坏习惯往下走。

### 9.5 发版流程

| 步骤 | 动作 |
|---|---|
| 1 | 改 `core/include/core/version.hpp` 的 `kVersionMajor/Minor/Patch` |
| 2 | 更新 `CHANGELOG.md`，加版本小节 |
| 3 | 提交并推 `main`，确认 CI 绿 |
| 4 | `git tag -a v0.1.0 -m "v0.1.0"` |
| 5 | `git push origin v0.1.0` → 触发 `release.yml` |
| 6 | Release 自动附 `ospt.exe`、`osptd.exe`、CHANGELOG 摘要、源码包 |

**版本号约定**：0.x 阶段用 `0.MINOR.PATCH`，接口稳定后再进 `1.0.0`。

**`release.yml` 必须校验**：tag 名去掉 `v` 后与 `version.hpp` 的三段版本号完全一致，
不一致就让 job 失败。这一步防"tag 打的是 v0.2.0，二进制里还是 0.1.0"。

### 9.6 GPL-3.0 对发版的硬要求

GPL-3.0 规定分发二进制必须同时提供对应源码。所以：

- GitHub Release **必须**同时提供源码（GitHub 自动生成的
  `Source code (zip)` / `Source code (tar.gz)` 即满足）
- Release 说明里要写明源码获取方式，**不能只挂 exe**
- `ospt.exe --version` 必须打印仓库地址（第 6 节已要求）
- 将来若做安装包，安装目录里要带 `LICENSE` 与源码获取指引

### 9.7 里程碑与 CI 增量

| 里程碑 | CI 增量 |
|---|---|
| M1 | `ci.yml` 跑通：x64 构建 + ctest + selftest + artifact ✅ |
| M2 | `--checkup` 体检校验脚本进 CI ✅；`concurrency` 并发控制 ✅；Test 失败打 `::error::` annotation ✅；`lint.yml`（clang-format 增量检查）✅ **已于 M2 期间完成，未推迟** |
| M3 | 「Unattend 校验」步骤进 CI：fixture → cfg.json → unattend.xml → 断言 ✅（现 121 项，见下）；「Artifact 内容断言」步骤 ✅。矩阵仍 x64 |
| M4 | 「Runplan 校验」步骤进 CI ✅ —— 覆盖**两条路径**：不带 `--drives`（表内嵌在计划文件）与带 `--drives` 显式覆盖；均断言不依赖 runner 实际盘符 |
| M5 | **加 x86 到矩阵** ✅ **已落地，且立刻抓到一个真 bug（见下）**；`osptd.exe` 纳入 artifact 断言 ✅（`ospt-x64` / `ospt-x86` 双 artifact）；**加「Win7 兼容闸门」步骤** ✅ —— 正例查三项（Subsystem 6.1 + 导入表白名单 + 无动态 VC 运行库）+ 自述交叉比对，**并跑 `verify_win7_compat_negative.py` 负例**（篡改产物后闸门必须失败，否则闸门就是橡皮章） |
| M6 | DISM 相关只能 `--dry-run`，CI 里跑 dry-run 并断言命令行文本；**模式 B 的 SRS 注入断言每个 `RegSetValueEx` 的键路径与值，不碰真实 hive** |
| M7 | UI smoke test 在无头环境可能跳过，**必须显式打印 skip 原因**，不许静默略过；主程序加 x86 |
| M8 | `release.yml` 首次实跑，产出 v0.1.0 |
| M9 | 「封装前设置验收」步骤进 CI ✅ —— `verify_prepare.py` 断言 dry-run 零副作用、动作与配置逐条对应、推迟项都有原因、坏配置/坏日志退出码、`--prepare-selftest` 写入并回滚；runner 是管理员，所以提权分支真实执行 `closeNumLock` 再用 `--rollback` 还原 |

**`lint.yml` 的实际做法**（比原计划更好，记录备查）：

- **固定 clang-format 版本 `19.1.5`**，走 PyPI 安装（`clang-format==19.1.5`）而不是用 runner 自带的
  VS LLVM —— 后者会随镜像更新漂移，导致同一份代码在本地与 CI 判定不同
- 装完**校验实际版本号**，不符直接失败
- **只查增量**：`git diff --name-only --diff-filter=ACMR <base> HEAD -- '*.cpp' '*.hpp' '*.h' '*.cc' '*.cxx'`
- 本次改动不含源文件时**打印原因后跳过**，不静默略过
- **不用** `clang-format --dry-run --Werror ... 2>&1 | Tee-Object`（正是 §10 记的那个 PowerShell 坑）。
  改为让 clang-format 把结果写临时文件，再与原文件比 SHA-256

**原计划的「先提一个纯格式化提交」已不需要** —— 增量检查不要求历史文件重排，只在文件被改到时才校验。

**实测（2026-10-24，clang-format 19.1.5）**：`git ls-files` 里非 `third_party/` 的
**17 个源文件全部合规，0 个需格式化**。既有代码已符合 `.clang-format`，
所以 M4 开工**不需要任何前置格式化提交**，直接叠功能即可。

```bash
# 本机复现方式（clang-format 不在 PATH，先装固定版本）
python -m pip install "clang-format==19.1.5"
for f in $(git ls-files '*.cpp' '*.hpp' '*.h' '*.cc' '*.cxx' | grep -v '^third_party/'); do
  clang-format "$f" | cmp -s - "$f" || echo "需格式化: $f"
done
```

---

## 10. 已知的坑（来自逆向分析）

| 坑 | 说明 |
|---|---|
| WinPE 无 .NET | 部署运行时必须原生 |
| **WinPE 无 x86 可选组件** | Win11 ADK 只给 Amd64/Arm64；x86 目标要用 x86 的 boot.wim |
| **v14 Redist 不支持 Win7** | 微软已明确最新的 v14 Redistributable **只支持 Win10/11**。所以 `/MT` 静态链接不是优化而是硬要求 —— 一旦产物动态依赖 `VCRUNTIME140.dll`/`MSVCP140.dll`/`ucrtbase.dll`，Win7 上直接跑不起来。CI 有专门步骤禁止这种依赖 |
| **`_WIN32_WINNT` 不设就等于不设防** | 不定义时头文件会把全部 Win8+/Win10+ API 都暴露出来，误用会**照常编译通过**并在导入表里出现。必须 `_WIN32_WINNT=0x0601`，让头文件在编译期报错。实测 2026-10-25：本项目当时未设置，Subsystem 版本停在 6.0（Vista） |
| **STL 的 `WaitOnAddress`** | VS2022 STL 在同步原语上可能用到 `WaitOnAddress`/`WakeByAddress*`，那是 **Win8+**。好在 `_WIN32_WINNT=0x0601` 下会走条件变量回退路径。实测本项目三个产物的导入表与字符串里都是 **0 处**。加兼容闸门后这条会被持续守住 |
| **Win7 必须先装 SP1** | MSVC 官方只支持 targeting Windows 7 **SP1**，RTM 不在支持范围 |
| **Win7 装驱动需 KB4474419** | 2019 年后 Win7 必须装 SHA-2 代码签名支持补丁，否则不认 SHA-2 签名的驱动。M6 装驱动前要检查 |
| **WOW64 文件系统重定向** | 32 位进程在 64 位 Windows 上访问 `%SystemRoot%\System32` 会被**静默重定向到 `SysWOW64`**。而 `sysprep.exe`/`dism.exe`/`bcdedit.exe` 都是 **64 位专有**工具，在 `SysWOW64` 里不存在 —— 不关重定向就探测，报出的"文件不存在"是**假的**。`ScopedWow64FsRedirection`（RAII）关掉再探。注意 `Wow64DisableWow64FsRedirection`/`Wow64RevertWow64FsRedirection` **只存在于 64 位** kernel32，在 32 位系统上静态导入会让进程**直接加载失败**，只能动态取；取不到恰好说明没有重定向，无需处理 |
| **Python 脚本在 runner 上打印中文会崩** | Windows runner 的 Python stdout 是 **cp1252**，`print` 中文抛 `UnicodeEncodeError: 'charmap' codec`。每个验收脚本都要有 `sys.stdout.reconfigure(encoding="utf-8")`。**次生伤害更麻烦**：Traceback 走 stderr，而 CI 步骤只 Tee 了 stdout，日志里只剩 `Process completed with exit code 1`，所有 `::error::` 都打不出来，排查信息量为零 |
| **PowerShell 函数里用 `Tee-Object` 会吞输出** | 把命令包在 `function` 里再 `\| Tee-Object`：**Tee 的输出会变成函数的返回值**，于是一方面闸门自己的输出从控制台消失，另一方面退出码比较被搅成数组比较。**不要为了复用而包函数**，直接在步骤体里顺序写。这是 §10 那条 PowerShell 坑的另一个变体 |
| sysprep 不可逆 | 必须先提示用户备份，且要能检测"已封装过"（`HKLM\SYSTEM\Setup\Status\SysprepStatus`） |
| 驱动库版权 | SC 那 596 个 SRS 驱动属各厂商，不能分发。让用户自备或只做注入框架 |
| DISM 需要匹配架构 | x86 镜像要用 x86 的 DISM |
| unattend.xml 元素顺序 | 子元素顺序按 schema 声明输出（ADK 离线参考 `Unattend.chm` 各组件的 `Child Elements` 列表；静态 `unattend.xsd` 已不随包分发）。**顺序错的后果不是统一的**：2026-10-24 实测只有 `Microsoft-Windows-TCPIP \| Interfaces \| Interface` 一处会静默忽略整段，其余组件 Windows 安装程序容忍乱序。本工具不生成该组件，但仍按声明顺序输出 |
| 中文路径 | 内部统一 UTF-8 / `std::filesystem::path`，转 Win32 时显式 `MultiByteToWideChar` |
| GPL-3.0 传染性 | 引入依赖前先查许可，见第 6 节 |
| **本机构建环境** | 曾缺 Windows SDK 导致 `could not find any instance of Visual Studio`，2026-10-24 已补齐。换机器时先跑下方"环境检查" |
| **unattend 的登录/桌面落点** | 原先 `logon` 与 `desktop` 都塞进 `FirstLogonCommands`，与 ADK 参考不符。正确落点是：**`FirstLogonCommands` → `SynchronousCommand`（桌面出现前同步执行）对应「登录时」；`LogonCommands` → `AsynchronousCommand`（不等待、不推迟桌面出现）对应「进桌面」**。两者合法 pass 都是 `oobeSystem`。`1453063` 已修。注意 `AsynchronousCommand` 的子元素只有 `CommandLine` / `Description` / `Order`，同页的 `RequiresUserInput` 标注为 "This setting is not used."，不要生成 |
| **`searchScope: systemPath` 的语义** | 是**绝对路径直通**（目标按原样使用、不查驱动器表），不是"解析到系统盘"。产物里以 `resolvedFromDriveTable=false` 标记。名字容易误读 |
| **PowerShell 吞退出码** | CI 里调原生命令（python / ospt）时，`2>&1 \| Tee-Object` 会把 stderr 变成 `ErrorRecord`，配合 `$ErrorActionPreference='Stop'` 可能直接终止或吞掉 `$LASTEXITCODE`，**校验脚本明明失败却显示 success**。改法：不加 `2>&1`，单独一行取 `$LASTEXITCODE` 显式判 `-ne 0` 再 `exit`。本机只有 PS 5.1、CI 用 pwsh 7，两个版本行为不同，改动必须在 5.1 上跑正反两例 |
| **SRS 驱动不是标准驱动包** | SC 的 `FUNB` 里**没有 `.inf` / `.cat`**，只有裸 `.sys` + `Drv.reg` / `Drv.pesrs`（同内容，只差 hive 前缀 `HKLM\SYSTEM\ControlSet001` vs `HK_USERS\DCSRS\ControlSet001`）。`dism /add-driver` 对它完全无效，必须离线挂载 SYSTEM hive 写 `CriticalDeviceDatabase` + `Services`。详见 §8 M6 与 `out/15_spec/驱动集说明.md` |
| **`git push` 需要走代理** | 直连 github.com **间歇性失败**（`Recv failure: Connection was reset` / `Failed to connect to github.com:443`），可靠路径是 `git -c http.proxy=socks5h://127.0.0.1:10808 push …`。`git ls-remote` 走同一路径，**验证时也要加代理**，否则会误判成"远端没更新"。2026-10-25 实测：同一分钟内直连两次失败、代理两次成功 |
| **GCM 两个账号会让 push 静默挂死** | 本机 Git Credential Manager 为 github.com 存了**两个账号**（`misakano7545` 与陈旧的 `x-access-token`），每次 push 都要弹窗让选一个，无人应答就**无任何输出地挂住 400 秒以上**。这才是挂死的真因（不是代理）。**不要**用 `GCM_INTERACTIVE=never` 规避 —— 它会把选择提示变成硬失败 `Cannot prompt because user interactivity has been disabled`。正解是只设 `GIT_TERMINAL_PROMPT=0`，GCM 约 5 秒后自动采用 `misakano7545`。诊断用 `GCM_TRACE=1`；根治办法是删掉那个陈旧账号 |

### 环境检查（每次新会话开工前跑一次）

```bash
# 1. MSVC 工具集
ls "/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC"
# 2. Windows SDK（没有它连 stdio.h 都没有）
ls "/c/Program Files (x86)/Windows Kits/10/Include"
# 3. CMake
cmake --version   # 或便携版 C:/Tools/cmake-*/bin/cmake.exe
```

2026-10-24 实测（M1 完成后）：

| 组件 | 状态 |
|---|---|
| MSVC | ✅ 14.44.35207（`VC/Tools/MSVC`） |
| Windows SDK | ✅ 10.0.26100.0（`Windows Kits/10/Include`） |
| CMake | ✅ 4.4.4 便携版 `C:/Tools/cmake-4.4.4-windows-x86_64/` |
| VS BuildTools | ✅ `isComplete=1` |

历史坑（已解决，留作排错参考）：SDK 未装时 `cmake -S . -B build -G "Visual Studio 17 2022"`
报 `could not find any instance of Visual Studio`。修法是 VS Installer 补装
「使用 C++ 的桌面开发」+「Windows 11 SDK」，装完 `isComplete` 变 1。

**注意**：本机 `ctest` / `cmake` 不在 PATH，要用全路径
`C:/Tools/cmake-4.4.4-windows-x86_64/bin/ctest.exe`。
CI 里 runner 自带 PATH，无此问题。

---

## 11. 当前状态

| 阶段 | 状态 |
|---|---|
| 需求提取 | ✅ 完成 |
| 技术选型 | ✅ 完成 |
| 仓库初始化 | ✅ 完成 |
| 产物改名 | ✅ 完成（`ospt` / `osptd`） |
| 构建环境 | ✅ 完成（MSVC 14.44 / SDK 10.0.26100 / CMake 4.4.4 / `isComplete=1`） |
| **M1 骨架与核心库** | ✅ **完成**（`c4090c1`） |
| **M1 加固** | ✅ 完成（`7acdb35`：concurrency + artifact 内容断言 + README 验收节） |
| **M2 系统体检** | ✅ **完成**（`1a33770` + `0cc9938`） |
| **M3 unattend.xml 生成** | ✅ **完成**（`90ca731` + `a5f0a48`；语义修正 `1453063`） |
| **M4 运行计划调度器** | ✅ **完成**（`9505fdc`，CHANGELOG `df0819c`） |
| **M5 部署运行时 + Win7 SP1** | ✅ **完成**（`1178515` + `3e6357c` + `30b630b`） |
| **M6 驱动注入 + sysprep 编排** | ✅ **完成**（`fd90cfe`，CI 增 M6 步骤） |
| **M7 主程序 UI** | ✅ **完成**（`34b149e` M7a + `705ca2e` M7b + `287deda` 可用化） |
| **M8 集成测试矩阵 + 发版** | ✅ **完成**（`a604e24`；v0.1.0 已发） |
| 发版 0.1.1 | ✅ 完成（`5e06d46`；GUI 子系统 + 自动提权在 0.1.1 之后） |
| **M9 封装前设置应用** | ✅ **完成**（未发版，见下「M9 验收记录」） |

**M1–M5 验收记录**（2026-10-25，末次实测 sha `30b630b`）

```
M1  本地 configure/build 0 error
    ctest --test-dir build -C Release        → 1/1 passed（doctest 7 cases / 26 assertions）
    ospt --selftest                          → config roundtrip OK，日志落盘 1985 bytes
    ospt --version                           → 打印仓库地址 + GPL-3.0

M2  ospt --checkup --json out.json           → exit 0
    python tests/integration/verify_checkup.py out.json
                                             → RESULT: ALL PASSED (85 checks)

M3  ospt --gen-unattend cfg.json --out unattend.xml
                                             → exit 0
    python tests/integration/verify_unattend.py unattend.xml
                                             → RESULT: ALL PASSED (121 checks, 0 skipped)
                                               （原 116；1453063 修正 LogonCommands 落点后 +5）

M4  ospt --gen-runplan plan.json --out run/   → exit 0
    产物 runplan.json + 5 个阶段脚本，共 17 个任务
    python tests/integration/verify_runplan.py plan.json run/
                                             → RESULT: ALL PASSED (274 checks)
    ctest --test-dir build -C Release        → 1/1 passed
                                               （doctest 37 cases / 25,529 assertions）

本地总览（M4 时点，1453063）
    ospt.exe 612,864 B / osptd.exe 141,312 B / osp_unit_tests.exe 970,240 B

M5  x64 与 x86 各跑一遍（详见 §8 M5.2）
    x64  build/bin/Release/osptd.exe      machine=0x8664  subsystem=6.1
    x86  build-x86/bin/Release/osptd.exe  machine=0x014c  subsystem=6.1
    Win7 兼容闸门  x64/x86 均 ALL PASSED (23 checks，1 项跳过，带原因)
    Win7 闸门负例  ALL PASSED（4 组篡改全部被拦下）
    osptd --dry-run --plan run/          → exit 0
    verify_deploy_exec.py                → ALL PASSED (48 checks)
    verify_win7_compat_negative.py       → ALL PASSED
    ctest / 单测   两架构均 1/1 passed；53 cases / 25,996 assertions
    WOW64 修复验证 x64 与 x86 的 --checkup JSON 33 个字段差异 0 个

全链路回归（M5 动了 CMakeLists 与 core/sysinfo，所以 M2–M4 全部重跑）
    M2 verify_checkup      85 checks  ALL PASSED
    M3 verify_unattend    121 checks  ALL PASSED
    M4 verify_runplan     274 checks  ALL PASSED
    M5 verify_deploy_exec  48 checks  ALL PASSED

CI  run #19（1178515）failure → #20（3e6357c）success → #21（30b630b）success
    Lint #10 / #11 / #12 均 success
    artifact：ospt-x64 546,432 B / ospt-x86 486,552 B
```

**M3 的两条实测结论**（已回写第 10 节）：

1. 元素顺序违规**不是**统一被静默忽略，只有 `Microsoft-Windows-TCPIP | Interfaces | Interface`
   一处如此。原第 10 节的表述过宽，已修正。
2. CI 里用 PowerShell 调原生命令时，`2>&1 | Tee-Object` 配合 `$ErrorActionPreference='Stop'`
   会吞掉 `$LASTEXITCODE`，**校验失败却显示 success**。已改成显式判退出码。

**M9 验收记录**（2026-10-26，本机 x64 + x86）

```
ctest --test-dir build     -C Release  → 1/1 passed（doctest 104 cases / 27,068 assertions）
ctest --test-dir build-x86 -C Release  → 1/1 passed（同上，两架构一致）
  其中 M9 新增 12 cases / 136 assertions（regio 3 + prepare 9）

ospt --prepare-selftest                → prepare selftest OK（3 个动作已写入并回滚，值已还原）

python tests/integration/verify_prepare.py build/bin/Release/ospt.exe
                                       → RESULT: ALL PASSED（36 项检查，1 项跳过）
python tests/integration/verify_prepare.py build-x86/bin/Release/ospt.exe
                                       → RESULT: ALL PASSED（36 项检查，1 项跳过）
   跳过项 = 未提权会话里的真实执行；提权会话单独跑过：
   → RESULT: ALL PASSED（46 项检查，0 项跳过）
     含 execute 写 NumLock=2 → rollback dry-run → rollback execute → 值还原逐项核对

--prepare --dry-run（11 个动作 / 12 项推迟）实测输出节选
   RegSetValueEx  HKU\.DEFAULT\Control Panel\Keyboard  "InitialKeyboardIndicators" (sz) = 2
   执行  C:\Windows\System32\powercfg.exe /hibernate on
   RegSetValueEx  HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management
                  "PagingFiles" (multiSz) = D:\pagefile.sys 0 0
   清空目录  C:\Users\Admin\AppData\Local\Temp（不可回滚）

tests/tools/run_acceptance_elevated.ps1 全 15 步 exit=0（新增「封装前设置」一步）
```

**下一步**：M10 —— 离线加载 `Users\Default\NTUSER.DAT` 把「我的文档转移 / IE 主页同步」
补上，以及文件防篡改清单（用户态 MD5 校验）与驱动卸载。这三项是 M9 deferred 段里
最常被配置到的，其余推迟项各有归属模块。

**推送状态（2026-10-26）**：M9 提交后主仓库远端 = 本地（CI 与 Lint 结果见 Actions）。
驱动仓库远端 = 本地 = `cb4fc12`，远端递归树 98 文件 / 12.705 MB。

**正确的推送姿势**（本阶段踩了坑，见 §10）：

```bash
GIT_TERMINAL_PROMPT=0 git -c http.proxy=socks5h://127.0.0.1:10808 push origin main
```

要同时满足两点：**加代理**（直连间歇性失败）、**不要设 `GCM_INTERACTIVE=never`**（会让 GCM 的双账号
选择提示变成硬失败）。验证远端时 `git ls-remote` 也要加同一个代理。

---

## 12. 决策记录

**当前无未决项。** 本节记录已定死的决策与理由，不再有"待确认"。

**已决策（2026-10-24）**：产物改名 —— 主程序 → **`ospt`**、部署运行时 → **`osptd`**，
避开与 SC（SysCeo）缩写的商标混淆。代码与本文档已同步改完，无残留引用。

**已决策（2026-10-25，用户拍板）—— Windows 7 支持从 M5 开始，不推迟到 M8**：

> 「我觉得在 M5 的话应该要加上 Windows 7 的支持」

- **范围**：M5 起 `osptd.exe` 必须能在 **Win7 SP1 x64 与 x86** 上运行；主程序 `ospt.exe`
  因共用 `core/` 静态库，一并按同样标准管
- **三道闸**（详见 §8 M5.1）：编译期 `_WIN32_WINNT=0x0601` 拦 Win8+ API；
  运行时不静态导入 Win8+ API；产物不得动态依赖 VC 运行库
- **自动化**：新增 `tests/integration/verify_win7_compat.py`，纯静态分析 PE，
  在 `windows-2022` runner 上即可跑，x64 与 x86 都查
- **已实测基线**：当前三个产物的导入表里 Win8+ API 与 `WaitOnAddress` 均为 0 处，
  但 `_WIN32_WINNT` 未设置、Subsystem 停在 6.0，属"碰巧能跑"，M5 要变成有保证
- **与 §12 x86 决策的关系**：x86 原本定在 M5 进矩阵，现在与 Win7 支持是同一件事的两面
- **已知不需要的**：KB2999226（UCRT）—— 那是 `/MD` 才需要的，静态 `/MT` 天然规避

**已决策（2026-10-24，随 M2/M3 落地）**：x86 分阶段 —— **M2–M4 只做 x64，M5 起加 x86，
M7 主程序也出 x86**。`.github/workflows/ci.yml` 的矩阵注释已按此写死，M2/M3 已按 x64-only 交付。
**M5 开工时必须把 x86 加进矩阵**，否则部署运行时无法在 32 位目标系统上启动。

**已决策（2026-10-24，用户拍板）—— UI 用原生控件**：

> 「UI 我想先用原生？这样初期做 demo 好一些」

M7 用 **原生 Win32 标准控件**（WTL 薄封装），先出可用 demo，初期见效快。
**Direct2D 自绘与换肤后置**为 M7 之后的独立里程碑，不进 M7 交付范围。
「不复刻 SC 观感」这条一并确定：只借鉴交互逻辑（阶段划分、进度语义），视觉完全自研。
已同步 §1 技术决策表与 §2 目录树。

**已决策（2026-10-24，用户拍板）—— 驱动用 SC 的，放 GitHub**：

> 「驱动用原来 scpt 的，放到 github」

调研后确认可执行，但**不能按字面整个搬**。实测结论（详见 `out/15_spec/驱动集说明.md`）：

| 组 | 文件 | 大小 | 处置 |
|---|---|---|---|
| `FUND`/`FUNE` —— Windows 自带 `System32\{drivers, DriverStore, catroot}` 的 x64/x86 副本 | 466 | 39.6 MB | **丢弃**（目标机本来就有） |
| `FUNB/MS/` 里 37 个署名为 Microsoft 的 `.sys` | 37 | 1.0 MB | **丢弃**（微软 in-box） |
| `FUNB` 剩余（10 个厂商的 SRS 包） | 83 | 5.5 MB | 入库 |
| `FUNF`（Intel RST VMD 标准包） | 10 | 7.2 MB | 入库 |
| | **93** | **12.7 MB** | |

**关键发现**：`FUNB` 下**没有任何 `.inf` / `.cat`** —— 只有裸 `.sys` +
`Drv.reg` / `Drv.pesrs`（两份同内容、只差 hive 前缀：`HKLM\SYSTEM\ControlSet001` vs
`HK_USERS\DCSRS\ControlSet001`）。共 **1555 条 CriticalDeviceDatabase + 201 条 Services，
覆盖 81 个存储控制器服务**。这是**离线注册表注入**，不是驱动安装。

**因此 M6 必须实现两种注入模式**（已写入 §8 M6）：A 标准驱动包走 `dism /add-driver`；
B SRS 注册表绑定走离线 hive 注入。**只做 DISM 用不了 SC 这批驱动。**

**交付形态（已定死）**：驱动二进制入**独立仓库 `OpenSysprepDriver`**，不并入本 GPL-3.0
主仓库 —— 隔离 takedown 风险，也避免把许可讲混。仓库 README 写明版权归各权利人、本仓库不主张
权利、收到通知即删，`manifest.json` 带 SHA-256 证明文件未修改。

**三项决策已定死（2026-10-24，用户确认按建议执行）**：

| # | 决策项 | 结论 | 落地状态 |
|---|---|---|---|
| 1 | `FUNB/MS/` 那 18 个非微软署名的 in-box `.sys` 留不留 | **留**。`MS/` 整体保留（20 文件 / 18 sys），因为它含最大的 CDD 覆盖（679 条 CDD + 119 个 Services），剔除会显著缩小老机型覆盖。入库集合维持 **93 文件 / 12.7 MB** | ✅ 已入库 |
| 2 | 独立仓库还是并入主仓库 | **独立仓库 `OpenSysprepDriver`**（`https://github.com/misakano7545/OpenSysprepDriver`）。主仓库保持纯 GPL-3.0 代码，驱动作为独立聚合仓库发布；主仓库 README 与本文档都指向它 | ✅ 已推送 `75ad2a8`（远端 98 文件 / 12.71 MB） |
| 3 | 二进制入库，还是只放 manifest + 导入脚本 | **二进制入库**。风险靠 README 版权声明、SHA-256 清单、独立仓库三项缓解；代价是新用户克隆即用，不必自备驱动 | ✅ 已入库 |

三项已全部落地，**无需再议**。

**目录**：远端 `https://github.com/misakano7545/OpenSysprepDriver`，本地 `D:\dev\OpenSysprepDriver`（与主仓库并列），内容 98 文件 / 12.7 MB：
`srs/`（83 文件 / 5.5 MB，10 个厂商 + MS）、`vmd/`（10 文件 / 7.2 MB，Intel RST VMD 两个版本）、
`README.md`、`manifest.json`（93 条记录，全量 SHA-256 校验通过）、`LICENSE`、`.gitattributes`、`驱动集说明.md`。

**已推送并核实**（2026-10-25）：远端 HEAD `75ad2a8`，GitHub API 递归树报 **98 文件 / 12.705 MB**，
与本地一致；抽样 `srs/ALI(ULI)/drivers/aliide.sys` 7,311 B、`vmd/intel/rstvmd/20240722/iaStorVD.sys`
1,619,656 B 均相符。

**M6 开工时驱动目录指向**：`--srs <dir>` 指向该仓库的 `srs/`，`--driver <dir>` 指向 `vmd/`
（或用户自备的标准驱动包）。

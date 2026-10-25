# Changelog

本文件记录每个发布版本的变化。格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 `0.MINOR.PATCH`（接口稳定后进 1.0.0）。

## [Unreleased]

## [0.1.0] - 2026-10-25

M1–M8：工程骨架、系统体检、unattend.xml 生成、运行计划调度器、部署运行时与
Windows 7 SP1 兼容、驱动注入与 sysprep 编排、主程序配置界面、集成测试矩阵与发版。

### Added

**M1 —— 工程骨架与核心库**

- CMake 3.25+ / MSVC 2022 工程，C++20，静态运行库 `/MT`，产物 `ospt.exe` 与 `osptd.exe`
- `core/config`：配置模型（系统信息、首次进桌面、系统优化、运行计划、部署设置、封装设置）
  与 JSON 往返序列化，缺字段取默认值，解析失败返回错误而不抛异常
- `core/log`：带时间戳与级别前缀的文件日志
- `ospt --selftest`：JSON 往返自检，通过时打印 `config roundtrip OK`
- `ospt --version`：版本号与源码地址（GPL-3.0 要求）
- 单元测试（doctest v2.4.11），含配置往返与错误路径用例
- `.github/workflows/ci.yml`：`windows-2022` 上 configure → build → ctest → selftest，
  并上传 `ospt-x64` artifact

**M2 —— 系统体检**

- `core/sysinfo`：系统版本与位宽（`RtlGetVersion`）、sysprep 可用性与待部署状态、
  分区容量、必备文件、克隆版检测、杀软状态（WMI `root\SecurityCenter2`）、启动项
- `ospt --checkup [--json <path>]`：人类可读摘要 + JSON 报告；单项采集失败写入
  `warnings` 而不中断，退出码恒为 0
- 克隆版判定用 `ImageState` + `OOBEInProgress`。`GeneralizationState == 7` 是所有
  正常系统的默认值，`CleanupState` 取值语义跨版本不一致，两者都不作判据
- `tests/integration/verify_checkup.py`：81 项断言，含「OS 版本与 `cmd /c ver` 一致」
  「sysprep.exe 真实存在」「分区容量与 `shutil.disk_usage` 偏差 ≤1%」三条硬证据
- `tests/unit/test_sysinfo.cpp`：结构完整性、版本格式、分区自洽、JSON 键完整性

**M3 —— unattend.xml 生成**

- `core/unattend`：用 pugixml 生成应答文件，覆盖 23 个叶子配置项
  - `specialize`：Shell-Setup（ComputerName / ProductKey / RegisteredOwner /
    RegisteredOrganization）、Deployment（RunSynchronous，取 `postinstall` 阶段）、
    UnattendedJoin（JoinWorkgroup 与 JoinDomain 互斥，域优先）
  - `oobeSystem`：Shell-Setup（AutoLogon / Display / FirstLogonCommands / OOBE /
    UserAccounts）
- `ospt --gen-unattend <cfg.json> --out <unattend.xml>`
- 元素顺序取 ADK 离线参考 `Unattend.chm` 各组件页的 `Child Elements` 列表；
  ADK 已不再随包分发静态 `unattend.xsd`
- Win7 兼容：只生成 Win7–Win10 共同元素，不生成 Win8 起废弃的
  `SkipMachineOOBE` / `SkipUserOOBE`
- `prefixRandom` 的后缀由配置内容哈希导出，同样输入同样输出
- `tests/integration/verify_unattend.py`：116 项断言，覆盖命名空间、元素顺序、
  父子嵌套、pass 合法性、计算机名规则、AutoLogon 次数、FirstLogonCommands 条数
- `tests/unit/test_unattend.cpp`：根元素与命名空间、component 属性、顺序表比对、
  计算机名校验（超长 / 禁用字符 / 纯数字）、工作组与域互斥

**M4 —— 运行计划调度器**

- `core/runplan`：把 `RunPlanTask` 解析成可执行计划，
  5 阶段 × 4 任务类型 × 4 运行方式 × 3 等待策略 × 4 路径类型 = 960 种组合
- 等待策略按宪章 §8 M4 的两列表映射：不等待 → `CREATE_NEW_PROCESS_GROUP` +
  立即 `CloseHandle`；等待完成 → flags=0 + `WaitForSingleObject(INFINITE)`；
  自定义秒数 → flags=0 + `WaitForSingleObject(timeoutMs)`
- 运行方式 → `STARTUPINFO.wShowWindow`；任务类型 → 命令行形态
  （run 直执 / `reg.exe import` / `cmd /c del /f /q` / `cmd /c rd /s /q`）
- 路径解析不依赖本机盘符：驱动器表可由 `--drives` 传入，或内嵌在计划文件顶层的
  `drives` 数组里，两者都没有才枚举本机。U 盘 / 光盘先按卷标匹配，卷标为空
  退回按驱动器类型
- `allDrives` 展开为每个候选盘符一条命令；善后处理 = 删除文件 时在执行后追加一条
  隐藏 + 等待完成的删除命令
- `ospt --gen-runplan <plan.json> --out <run/> [--drives <drives.json>]`；
  产物为 `run/runplan.json` + 每阶段一个 `.cmd`
- `core/config` 的 `RunPlanTask` 新增 `expectedVolumeLabel` 与 `timeoutAction`
- `tests/unit/test_runplan.cpp`：遍历全部 960 种组合断言映射表，另覆盖路径解析、
  善后处理、驱动器表解析与全部错误分支
- `tests/integration/verify_runplan.py`：274 项断言，独立重算一遍解析结果再与产物比对

**M5 —— 部署运行时与 Windows 7 SP1 兼容**

- `core/deploy`：读 `runplan.json` 并用 `CreateProcess` 逐条执行，三种等待策略
  （等待完成 / 不等待 / 自定义秒数），超时后按 `timeoutAction` 终止进程或继续；
  中止时写回滚记录；产物 JSON 与可读报告
- `osptd --plan <runplan.json> [--dry-run] [--continue-on-error]`
- `deploy/progress`：单页控制台进度（Win32 控制台读写重绘，**不用 ANSI/VT**，
  因为 Win7 的 conhost 不支持）；输出被重定向时自动退化为逐行打印
- Win7 SP1 三道闸：`WINVER`/`_WIN32_WINNT`/`NTDDI_VERSION` = `0x0601`
  （让 SDK 头文件直接拒绝 Win8+ API）、`/SUBSYSTEM:CONSOLE,6.01`、
  `verify_win7_compat.py` 读 PE 断言无 Win8+ 导入、无动态 VC 运行库、无 `api-ms-win-*`
- `core/win7`：`OsVersion`/`osVersion()`（`RtlGetVersion`）、动态 API 白名单与
  `resolve()`；`GetSystemTimePreciseAsFileTime` 不在 Win7 上，按白名单动态解析并回退
- WOW64 文件系统重定向的 RAII 包装 + `probeFileNative()`：32 位进程探测
  `System32` 下的 64 位工具时必须先关重定向，否则会被重定向到 `SysWOW64`
- CI 矩阵加 x86；产物断言与 artifact 覆盖 `ospt.exe` 与 `osptd.exe` 两个架构
- `tests/integration/verify_deploy_exec.py`（48 项）：真跑一遍计划，逐条验证
  `exited`/`not-waited`/`timeout-killed`/`timeout-continued`、中止与回滚、文件系统确实变化
- `tests/integration/verify_win7_compat_negative.py`：4 组等长字节篡改，证明闸门不是空判

**M6 —— 驱动注入与 sysprep 编排**

- `core/dism`：模式 A 用 `CreateProcess` 调 `%SystemRoot%\System32\Dism.exe`
  `/image /add-driver /recurse`（**不链接 `DismApi.dll`**）；32 位进程先关重定向，
  否则会看到 `SysWOW64` 下那个管不了 64 位映像的 32 位 dism
- `core/srsinject`：模式 B 离线 hive 五步——`RegLoadKey` → 复制 `.sys` →
  写 `Services`（Type/Start/ImagePath）→ 写 `CriticalDeviceDatabase`（ClassGUID/Service）→
  `RegUnLoadKey`。`.reg` 解析支持 UTF-16LE/BE/UTF-8 自动识别与
  `REG_SZ`/`REG_EXPAND_SZ`(`hex(2)`)/`REG_DWORD`/`REG_QWORD`/`REG_MULTI_SZ`/`REG_BINARY`；
  `HKLM\SYSTEM` 与 `HK_USERS\DCSRS` 都映射到临时挂载键，外来 hive 显式列出并跳过；
  写注册表统一带 `KEY_WOW64_64KEY`
- `core/sysprep`：`/generalize /oobe /<quit|reboot|shutdown> /quiet [/unattend:<file>]`；
  KB4474419 离线判定（读 `<image>\Windows\System32\ntoskrnl.exe` 的 PE 版本判 Win7 家族，
  再找 `Package_for_KB4474419*`），缺补丁时记为阻塞项并**拒绝 `--execute`**
- `ospt --inject-drivers --image <挂载点> [--driver <目录>|--srs <目录>] --dry-run|--execute`
- `ospt --sysprep [--image <挂载点>] [--unattend <xml>] [--after <quit|reboot|shutdown>] --dry-run|--execute`
- 不内嵌任何驱动二进制：仓库里只有代码与夹具，驱动集在独立仓库 `OpenSysprepDriver`
- `tests/integration/verify_drivers.py`（62 项）：断言仓库里没有真实驱动二进制、
  导入表里没有 `dismapi.dll`、dry-run 前后目标 hive 字节完全一致（零副作用）
- `tests/unit/test_dism.cpp` / `test_srsinject.cpp` / `test_sysprep.cpp`

**M7 —— 主程序配置界面（M7a 框架 + M7b 其余配置页）**

- `host/ui`：原生 Win32 标准控件 + WTL 10 薄封装，数据驱动的渲染器
  （只读行 / 复选框 / 编辑框 / 数字框 / 下拉框 / 单选 / 按钮 / 进度条 / 列表共 9 类）；
  视觉完全自研，不做 SC 那种换肤，也没有引入任何图片或字体资源
- 交付 7 个配置页，页签标题与 DFM 的 `Caption` 逐字一致：
  母盘系统（`MlSkinTabSheet1`，字段来自 M2 体检）、目标系统（`MlSkinTabSheet2`）、
  计划任务（`MlSkinTabSheet3`）、部署过程（`MlSkinTabSheet4`）、
  封装过程（`MlSkinTabSheet5`）、运行（`MlSkinTabSheet6`）、完成（`MlSkinTabSheet7`）
- 页面与字段名一律沿用 DFM 控件名，走查时可与图纸一对一核对；每个字段的
  `binding` 指出落到 `core/config` 的哪个字段，`note` 记下取值依据
- `ospt --ui-smoke-test [--visible] [--report <json>]`：创建真实窗口、用真实页签控件
  逐页切换、用真实消息（`BM_CLICK` / `EN_CHANGE` / `CBN_SELCHANGE` / `BM_GETCHECK` /
  `PBM_*` / `LVM_GETITEMCOUNT`）驱动每一个控件、校验模型回写、销毁窗口
- 无头环境（没有窗口站）时**显式打印 `SMOKE-SKIP` 与原因**并返回 0，绝不静默略过
- `ospt.exe` 保持 `/SUBSYSTEM:CONSOLE,6.01`，窗口在运行期创建：既避开宪章 §10 记的
  「GUI 子系统与 6.01 冲突」，也保住 M5 的子系统版本闸门，冒烟输出还能打到 stdout
- 界面按钮只改配置或计数，**不执行**关机、重启、sysprep 与 dism；真正执行仍须
  `--execute`
- 宪章 §7 红线涉及的全部控件（系统总裁产品整组、装机联盟账户与计费 ID、
  同步封装前 IE 主页/收藏夹、固定任务栏文件、联盟/会员封装方式）与图纸/宪章都
  查不到语义的字段（一键修复、黑屏急救、部署模块列表、OEM 设置子对话框、
  错误报告、更新页）**一律不出现在界面上**，并登记在
  `ui::excludedControlIds()` 供验收脚本断言
- WTL 10（build 10320，MS-PL）按官方归档 vendor 到 `third_party/WTL/`：
  `Include/` 全部 20 个头文件 + 许可正文 + `MANIFEST.json`（逐文件 sha256）；
  根 `.gitattributes` 加 `third_party/WTL/** -text -eol` 保证签入内容与归档逐字节一致
- `tests/integration/verify_ui.py`（63 项）：逐页核对打开情况与字段/控件数、
  统计六类控件响应证据、断言冒烟记录里没有任何红线控件名、复核 Win7 闸门
- `tests/unit/test_ui.cpp`：页签清单与 DFM 页对象一一对应、走查名单、红线字段不出现、
  下拉取值来自 `core/runplan` 的四个维度、配置→页面→配置往返逐字一致

**M8 —— 集成测试矩阵与发版**

- `.github/workflows/release.yml`：tag（`v*`）触发。第一步就是**版本一致性硬校验**：
  tag 去掉前缀 `v` 后必须与 `core/include/core/version.hpp` 的三段版本号完全一致，
  不一致时整条流水线失败、**绝不产出 Release**
- 双架构构建（x64 与 x86）后按发布资产名收口四份二进制：
  `ospt-x64.exe`、`osptd-x64.exe`、`ospt-x86.exe`、`osptd-x86.exe`，
  并核对机器码（0x8664 / 0x14C）与子系统版本 6.1。
  每个架构的构建 job 只收口自己那两份（手里也只有那两份），四份齐全由发版 job 统一核对
- 源码包：`git archive <tag>` 生成 `OpenSysprepTool-<tag>-src.zip`，并核对包内含
  `LICENSE`、`third_party/LICENSES.md`、`third_party/WTL/MS-PL.txt` 与 `version.hpp`。
  GPL-3.0 要求二进制与源码同时提供，所以 Release 说明里写明三种源码获取方式
  （附带的源码包、仓库地址、`git clone` 与 `git checkout <tag>` 命令），不只挂 exe
- Release 说明 = CHANGELOG 摘要 + 产物清单 + GPL 源码声明 + 集成测试矩阵 + 人工验证清单；
  创建 Release 后**回读 assets** 逐个核对，缺一即失败
- `tests/integration/verify_release.py`：tag 一致性（`--check-tag`）、发版契约
  （`--check-workflow`）、四产物架构与子系统（`--check-artifacts`）、
  矩阵记录（`--emit-matrix`）、发布说明（`--emit-notes`）
- 集成测试矩阵 3 系统 × 2 架构 = 6 格（Win7 SP1 / Win10 22H2 / Win11 23H2 × x64/x86），
  每格分开「CI 已证（代码层）」与「需人工真机验证」两部分；托管 runner 上只有
  Windows Server 2022，没有这三套真机，流水线里逐格用 `::notice::` **显式打印跳过原因**
  并列出人工清单，不静默略过
- Win7 真机验证的前置补丁写进 README、CHANGELOG 与 Release 说明：**必须已装 SP1**；
  用 `--inject-drivers` 装驱动前**必须先装 KB4474419**（SHA-2 代码签名支持）

### Changed

- CI 的 Test 步骤在失败时把 ctest 输出里的断言行打成 `::error::` annotation，
  匿名访问仓库即可定位失败项（Actions 日志本身需要登录）
- CI 增加 `concurrency` 组，同分支新推送取消旧运行
- CI 增加 Unattend 校验步骤（M3 增量，宪章 §9.7）
- CI 增加 Runplan 校验步骤（M4 增量，宪章 §9.7），覆盖计划内嵌驱动器表与
  `--drives` 两条路径
- CI 增加部署运行时验收与 Win7 兼容闸门步骤（M5 增量，宪章 §9.7），
  后者同时跑 4 组篡改负例
- CI 增加 M6 驱动注入验收步骤（M6 增量，宪章 §9.7）
- CI 增加 UI 冒烟测试步骤（M7 增量，宪章 §9.7）；无头跳过时用 `::warning::`
  把 `SMOKE-SKIP` 的原因浮到日志里，不当失败处理
- 新增 `release.yml` 工作流（M8 增量，宪章 §9.7）：tag 触发，版本一致性校验不通过
  即整条失败；三个工作流都带 `concurrency` 组
- 所有验收脚本统一强制 UTF-8 stdout：GitHub `windows-*` runner 的 Python stdout
  是 cp1252，打印中文会在任何断言之前抛 `UnicodeEncodeError`
- CI 步骤统一「合并所有流」的写法（`Continue` +
  `$PSNativeCommandUseErrorActionPreference = $false` + `*>&1 | Tee-Object`），
  失败时若日志里没有 `FAIL/RESULT/Traceback` 行则补打尾部输出

### Notes

- 第三方依赖登记在 `third_party/LICENSES.md`：doctest v2.4.11（BSL-1.0）、
  nlohmann/json v3.11.3（MIT）、pugixml v1.14（MIT）
- 宪章 §10 原记「unattend.xml 元素顺序错会被 sysprep 静默忽略」，实测仅在
  `Microsoft-Windows-TCPIP | Interfaces | Interface` 一处成立；本工具不生成该组件

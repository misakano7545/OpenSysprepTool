# Changelog

本文件记录每个发布版本的变化。格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 `0.MINOR.PATCH`（接口稳定后进 1.0.0）。

## [Unreleased]

## [0.1.0] - 待发布

M1–M4：工程骨架、系统体检、unattend.xml 生成、运行计划调度器。

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

### Changed

- CI 的 Test 步骤在失败时把 ctest 输出里的断言行打成 `::error::` annotation，
  匿名访问仓库即可定位失败项（Actions 日志本身需要登录）
- CI 增加 `concurrency` 组，同分支新推送取消旧运行
- CI 增加 Unattend 校验步骤（M3 增量，宪章 §9.7）
- CI 增加 Runplan 校验步骤（M4 增量，宪章 §9.7），覆盖计划内嵌驱动器表与
  `--drives` 两条路径

### Notes

- 第三方依赖登记在 `third_party/LICENSES.md`：doctest v2.4.11（BSL-1.0）、
  nlohmann/json v3.11.3（MIT）、pugixml v1.14（MIT）
- 宪章 §10 原记「unattend.xml 元素顺序错会被 sysprep 静默忽略」，实测仅在
  `Microsoft-Windows-TCPIP | Interfaces | Interface` 一处成立；本工具不生成该组件

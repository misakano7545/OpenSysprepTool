# Changelog

本文件记录每个发布版本的变化。格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 `0.MINOR.PATCH`（接口稳定后进 1.0.0）。

## [Unreleased]

### Fixed

- **七个页签全部按 SC 的 DFM 坐标摆放**：目标系统、母盘系统、计划任务、部署过程、
  封装过程、运行、完成，共 111 个控件逐控件核对（`GetWindowRect` 与图纸坐标差 ≤2px）
  - 坐标由 `tools/gen_ui_coords.py` 从 `docs/sc_layout/TSCMF.json` 生成成表
    （`host/ui/ui_dfm_coords.inc`），按「页面 / 分组框 / 控件」组织；分组框用绝对矩形，
    组内控件相对分组框（DFM 的语义就是如此）。生成表随仓库提交，构建不依赖 SC 产物
  - 母盘系统页的 5 个替换面板（扫描结果 / 一键修复 / 帮助 / 导入 SRS）在 DFM 里是
    `Visible = False`，现在按图纸默认隐藏——它们占同一矩形，全显示会叠在一起
  - 我方比 SC 多的字段排到图纸下方「扩展区」，页面高度自动加长
- **修掉一个偶发段错误（约每 15~20 次冒烟崩 1 次，与本次改动无关，早已存在）**：
  冒烟测试里 `BM_CLICK` 同步触发按钮动作 → `rebuildPages()` → 把**正在派发消息的
  那个按钮自己** `DestroyWindow` 掉，其窗口过程随后在已释放内存上继续跑。改成
  `PostMessage(WM_APP+13)` 延后重建
  - 另一处同类问题：`const Page& page = frame.pages[i]` 是指向 vector 的引用，
    重建会整体重赋值 → 引用悬空；改为取副本
  - 运行页动画定时器原先每帧遍历 `owner->pages`，同样会踩悬空；改为只认缓存的
    控件句柄，并在面板销毁时清掉定时器
- **运行页 / 完成页改为按 SC 的 DFM 逐控件摆放**：这两页此前用通用的「分组框 + 一行一个
  字段」自动排列，形状与 SC 完全不符 —— SC 是「居中大图 + 居中说明 + 底部双进度条」与
  「居中结果图 + 居中说明 + 完成度条 + 倒计时 + 三个按钮」的固定版面
  - 新增 `tools/dfm_layout.py`：把 SC 的 DFM 解析成 `docs/sc_layout/*.json`（控件名 / 类 /
    Caption / 坐标 / 尺寸），随仓库提交；`test_ui_layout` 逐控件比对，漂了会红
  - 运行页 5 个控件、完成页 8 个控件全部按 DFM 坐标摆放（DPI 无关，按图纸宽度 800 等比缩放）；
    实测矩形与图中坐标误差 ≤ 2px
  - 补上图纸里有而我们缺的：`Pic_RunGif`（自绘转圈）、`PIC_Success` / `PIC_Error`
    （自绘对勾与叉，同一矩形互斥）、`Lab_Countdown` 改用图纸原文（含中文引号）、
    按钮 Caption 补回快捷键标记 `关机(&T)` / `重启(&R)`
  - `Btn_Bugs` 是完成页唯一有意缺项：错误报告要把信息发到 SC 的服务器，宪章 §7 不做
- **主窗口客户区改为 SC 的 DFM 尺寸 800×540**（原 900×640）：取 900 会把图纸绝对坐标的
  页面整体放大 12%，版面比例与 SC 对不上

### Added

- **TSBOX 确认弹窗**（`host/ui/popups.cpp`）：SC 的 300×136 小确认框，两个模式
  - 重启方式：完成页「重启(&R)」先问「静默重启[无界面] / 倒计时重启[有界面]」，
    静默直接 `ExitWindowsEx`，倒计时复用完成页的 10 秒倒数（图纸 `Lab_Countdown` 文案）
  - 宽带创建方式：目标系统页「询问创建宽带连接」勾上后问「静默创建 / 倒计时创建」，
    结果落到 `firstLogon.connectionMode`
  - **文案取自语言文件而非 DFM**：DFM 里宽带页签写成「静默重启」是 SC 的复制粘贴错误，
    运行时被语言文件覆盖成「静默创建」
  - 新增 `sysprep.restartSilent` 记录重启方式；冒烟里强制 dry-run，绝不弹模态框
- **运行页接上真实进度**：`ospt` 现在真的拉起 `osptd` 跑部署，两条进度条与说明文字
  跟着走，结束后自动切到完成页
  - `osptd --progress-json`：一行一个 JSON 事件（phaseStart / taskStart / taskFinish /
    phaseFinish / result），与给人看的控制台输出并行、互不影响
  - `core/deploy_progress`：进度行解析（坏行跳过并计数，不能让界面崩）
  - `host/ui/deploy_runner`：`CreateProcess` + 管道 + 后台线程按行读，回调 PostMessage
    回界面线程再碰控件（Win32 控件不是线程安全的）
  - 完成页按结果二选一显示成功图/成功文案或失败图/失败文案（图纸上它们占同一矩形），
    成功时起 10 秒倒计时
  - **`关机(&T)` / `重启(&R)` 真的生效**：启用 `SE_SHUTDOWN_NAME` 后调 `ExitWindowsEx`；
    失败把 Win32 错误码显示出来，不静默什么都不发生
- `ospt --deploy-smoke-test --plan <run/>`：不点界面也能验整条进度链路（用的是同一份
  `DeployRunner`），CI 里跑 `verify_deploy_progress.py` 21 项
- `docs/SC窗体对照.md`：7 个 DFM 窗体逐项审计 —— 主窗体 11 个页签、5 个弹窗
  （`TSBOX` 静默重启/宽带、`TTLMF` 任务对话框、`TAPPMSGFORM` 提示框、`TSCUBOX`、`TMFFUN`）
  的状态与偏差，明确哪些还没按图纸做

## [0.1.2] - 2026-10-26

### Added

- **M10 OEM 壁纸与部署图片**（`oem.wallpaper` / `oem.deployImage`）：补齐 `SA_Lab_oembgpic`
  「遇品牌电脑则设置相应品牌桌面壁纸」与 `SA_Lab_oemDeploy`「改变部署图片」
  - 壁纸：复制进 `%SystemRoot%\Web\Wallpaper`，再离线写默认用户的
    `Control Panel\Desktop`（`Wallpaper` / `WallpaperStyle=10` / `TileWallpaper=0`），
    新用户登录即生效；沿用 B3 的 `HiveSet` + 写后回读
  - 部署图片：`deploy.backgroundImage` 为空时回落到 `oem.deployImage`；显式指定优先，
    免得品牌图悄悄覆盖用户自己的背景
  - **本仓库不含任何品牌素材**（宪章 §5 / §7）：两个字段都必须由用户给出自己有权使用的图
- **M10 宽带连接**（`Cb_CreateConnection`）：两个模式都按 SC 的语义做
  - 静默创建（`Rb_Silence_nc`）：新建 `ActionKind::PppoeCreate`，把 PPPoE 条目写进
    **默认用户**的电话簿，新用户登录即有该连接、全程无界面
  - 展示创建（`Rb_Show_nc`）：在 unattend 的 `FirstLogonCommands` 里加
    `rasphone.exe -a "<连接名>"`，首次进桌面弹创建向导询问用户
  - **不自己拼 `.pbk` 文本**：rasphone.pbk 的段与键没有公开完整规范，手写「看起来对」
    的文件写错了不报错、只是用户点进去连不上。这里由 `RasSetEntryProperties` 指向临时
    文件生成，再复制到目标位置——格式永远由 Windows 保证
  - 三处实测踩到的坑：`RASDEVINFOW.dwSize` 必须逐个预填，否则报「不正确的结构大小」；
    RAS 自己的 `ERROR_BUFFER_TOO_SMALL` 是 603，被 winerror.h 的同名宏 122 遮蔽，
    改成只认返回的缓冲区大小；`RasSetCredentials` 会返回 0 却没把账号写进指定的 pbk，
    所以写完再读回核对，没落进去就如实告警而不是假装存好了
- **M10 任务栏固定 / 取消固定**（`optimization.lockTaskbarFiles` / `unlockTaskbarFiles`）：新增
  `ActionKind::FileCopy` / `FileDelete`，把用户列出的快捷方式写进**默认用户**的
  `User Pinned\TaskBar`，新用户建配置目录时由系统带过去；取消固定只删同名项
  - **只做机制，不做推广**：SC 用这两个开关推自家快捷方式，属宪章 §7 红线。本实现不预置
    任何条目，清单为空就等于什么都不做，测试里专门钉住「清单只有一个文件 → 计划里也只有一个动作」
- **M10 卸载本机设备驱动**（`sysprep.uninstallDrivers`）：解析 `%SystemRoot%\INF\oem*.inf`
  的 `[Version]` 段找第三方驱动包，生成 `pnputil /delete-driver <inf> /uninstall /force`
  - 为什么不解析 `pnputil /enum-drivers`：那个输出是本地化的（中文「发布名称:」/英文
    "Published Name:"），CI 是 1252 英文系统，靠标签匹配必然在某一侧失灵；`oemNN.inf`
    文件名与 INF 内容语言无关
  - INF 里 `Provider=%ManufacturerName%` 会经 `[Strings]` 展开，否则微软自带包也显示成
    `%ManufacturerName%`，靠它过滤 Microsoft 会失灵
  - **存储类（SCSIAdapter / HDC / Volume / DiskDrive / CDROM / FloppyDisk）强制跳过并告警**：
    删掉之后目标机 0x7B 蓝屏起不来，不能为了「干净」把引导搭进去
  - 驱动包删除标记为不可回滚（删了回不来）
- **M10 部署前闸门**（`core/deploy/preflight.cpp` + `core/crypto/md5.cpp`）：把 SC 部署设置里
  三项「部署时才生效」的开关落到 `osptd`，且一律在**执行任何任务之前**判定
  - 校验文件防篡改（`Cb_Tampering`）：运行计划里带锁定文件清单 + MD5，部署时逐个比对；
    不一致则提示并中止，一个任务都不跑。MD5 按 RFC 1321 自己实现（SC 语言文件写的就是
    「校验被锁定文件的 MD5 值」），并用附录 A.5 的官方向量 + certutil 交叉验证
  - 禁止在虚拟机里部署（`Btn_DisableVmrun`）：CPUID `0x40000000` 叶读 hypervisor 厂商串
    （VMware / VBox / KVM / Microsoft Hv / Xen / Parallels / QEMU），不依赖 WMI
  - 部署主控被劫持（`SA_Rb_hijack_type1/2`）：生成计划时记下 `osptd.exe` 的 MD5，部署时
    比对自身；`terminate` 弹提示并中止，`continue` 只告警（对齐「防流氓封装方式可反劫持」）
  - 干跑只报告不拦截，真实执行才停；「读不到文件」与「被篡改」是两种不同的原因，分别报出
- **M10 自动设置 IP**（`core/netcfg` + `--prepare`）：对齐 SC 的 `Cb_AutoSetip` 三种模式
  —— 固定 IP（`Rb_Setip`）、随机 IP（`Rb_SetRandomip`）、IP 段（`Lab_ips` 起止地址），
  加掩码 / 网关 / 首选与备用 DNS。全部翻成 `netsh interface ipv4` 命令，dry-run 逐字可打印
  - 网卡自动探测：取「已连接且有默认网关」的适配器（`GetAdaptersAddresses`，注意必须带
    `GAA_FLAG_INCLUDE_GATEWAYS`，否则网关字段恒为空）
  - 随机模式没有给网段时，在本机当前地址的同一 /24 里取址（跳过 .0/.255），
    保证网关仍可达；SC 的原始随机算法在产物中看不到，这个取舍已写进注释与对照表
  - **网关与地址不在同一子网时告警**：随机/网段模式很容易撞到，执行完会直接断网
  - 非法 IP、非法掩码、未知模式一律只告警不生成动作，不猜
- **M10 离线 hive：我的文档 / IE 收藏夹转移、IE 主页同步**（``core/regio`` 的
  `LoadedHive` + `ActionKind::HiveSet`）：三项都要写进所有新用户的默认配置
  （`<系统盘>\Users\Default\NTUSER.DAT`），必须 `RegLoadKey` 离线挂载后写、写完卸载
  - 载入前显式启用 `SeRestorePrivilege` / `SeBackupPrivilege` —— 管理员令牌里这两项
    默认是禁用的，不启用会直接 `ERROR_PRIVILEGE_NOT_HELD(1314)`
  - 写入后**回读校验**：`Internet Explorer\Main` 在 Win10/11 上带拒绝写入的 ACL，
    会出现「API 返回成功但值没进去」的静默失败（`reg.exe` 也如此），不校验就等于假装做成了
  - 智能/指定两种目标对齐 SC 的 `Rb_SmartMoveDocuments` / `Rb_DiyMoveDocuments`：
    智能选剩余最大的固定磁盘，条件不成立则不转移
- **M10 系统优化项（SC 优化表 27 条）**（`core/tweaks`）：SC 把优化项存成
  `序号|类型|路径|键名|值` 的记录，这 27 条是从主程序字符串转储里**逐字提取**的，
  不是自己编的。`optimization.sysTweaks` 里填 id 即可由 `--prepare` 落成注册表动作
  （可 dry-run、可回滚）。SC 写死的 `ControlSet001` 统一改成 `CurrentControlSet`，
  否则设置会写进一份没被使用的控制集
- **M10 一键修复**（`ospt --repair`，对应 SC 的 `Btn_OsRepair`）：体检逐条比对优化表 →
  只修体检标了 `autoFix` 的项 → 输出 SC 的文案「优化完成，已修复 N 个问题」→ 写回滚日志。
  需要人工处理的项（杀软、克隆版、BitLocker…）一律不碰；读不到的项（如
  `SecurePipeServers\winreg` 的 ACL 只给 SYSTEM）如实报「无法判定」，不当成已修好
- **M10 OEM 信息识别与 SC 档案导入**（`core/oem`、`core/config/ini.cpp`）：
  `ospt --oem-detect [--skin <品牌目录>] [--lang CHS|EN] [--json <path>]` 读
  `HKLM\HARDWARE\DESCRIPTION\System\BIOS` 的厂商/型号，给了品牌目录就按 SC 的
  `Scoem\<品牌>\nt6.ini` 布局匹配档案；`ospt --import-oem <ini> [--into <cfg.json>]`
  把档案并进配置的 `oem` 段
- 生成的 `unattend.xml` 增加 `<OEMInformation>`（`Manufacturer` / `Model` /
  `SupportHours` / `SupportPhone` / `SupportProvider` / `SupportURL`），落在 `LogonCommands`
  之后、`OOBE` 之前；老字段给 Win7，`SupportProvider` 给 Win10+，一个字段都没有时整段不生成
- `core/config/ini.cpp`：极简 INI 解析器。编码判定顺序 UTF-8 BOM → UTF-16 BOM →
  合法 UTF-8 → ANSI（CP_ACP），**SC 那些 GBK 的 `.ini` 靠这一步才读得对**
- `tests/integration/verify_oem.py`（42 项）、`verify_tweaks.py`（22 项）
- `docs/SC功能对照.md`：以 SC 自己的语言文件为唯一依据的功能对照表，
  本仓库不再收录 SC 没有的功能
- **M10 图形部署页**（`deploy/progress_ui.cpp`）：`osptd` 执行计划时画全屏图形进度页，
  四种模块对齐 SC —— `grid`（四格 + 百分比）、`trueTrans`（Aero 条）、`colorBar`
  （按进度换色的渐变条）、`image`（背景图 + 细进度线）
- 呈现设置由 `--gen-runplan` 写进 `runplan.json` 的 `presentation` 段（模块、背景、标题、
  背景图、切换秒数、分辨率、文字颜色），`osptd` 只读这一份事实来源；非法值（未知模块/
  背景、非 `#RRGGBB`、只给一半分辨率）在生成阶段就拒绝，不静默退回默认
- 全屏分辨率切换（`ChangeDisplaySettings`，退出还原）+ **Ctrl+F11 黑屏急救**
- 图片解码走运行时 `LoadLibrary("gdiplus.dll")`，**不链接 gdiplus**（导入表实测无 gdiplus），
  取不到就退回纯色背景；BMP 还能走 `LoadImage` 兜底
- `osptd --preview-presentation [--plan <run/>] [--seconds <n>] [--screenshot <bmp>]`：
  只画页不执行任务，并把画面存成 24 位 BMP
- `tests/integration/verify_presentation.py`（24 项）：四种模块各画一遍并**读像素**
  断言画面不是单色、异常配置（缺图/坏颜色）不崩、参数校验；无桌面会话显式 SKIP
- `--prepare` 的 deferred 段逐条列出暂不执行的 12 项配置及原因（我的文档转移、IE 主页、
  自动 IP、清理 SRS、卸载驱动、文件防篡改、禁止虚拟机部署等），不静默略过
- **M9 封装前设置**：`core/regio`（注册表 RAII，HKLM / HKCU / HKU\\.DEFAULT，六种值类型，
  HKLM 固定 64 位视图）+ `core/prepare`（配置 → 动作计划 → 执行 → journal → 回滚）
- `ospt --prepare <cfg.json> --dry-run|--execute [--report <json>] [--journal <json>]`：
  把配置里以前只存不做的字段落成 11 个显式动作 —— 关闭小键盘、开启休眠、关闭关机事件
  跟踪（`ShutdownReasonOn` / `ShutdownReasonUI`）、虚拟内存换盘（`PagingFiles` 写成
  `<盘>:\pagefile.sys 0 0`）、同步 IE 收藏夹到默认用户、启用 Administrator、转移桌面
  文件到公共桌面、清空用户/Windows 临时目录、`sfc /purgecache`
- `ospt --rollback <journal.json> --dry-run|--execute`：逆序还原注册表旧值、移回文件、
  删除复制/新建的目录；临时目录清理与 `sfc /purgecache` 标记为不可回滚
- `ospt --prepare-selftest`：只写 `HKCU\Software\OpenSysprepTool\SelfTest`，写入三种
  值类型后回滚并核对，不需要管理员
- `--prepare` 的 deferred 段逐条列出暂不执行的 12 项配置及原因（我的文档转移、IE 主页、
  自动 IP、清理 SRS、卸载驱动、文件防篡改、禁止虚拟机部署等），不静默略过
- `tests/integration/verify_prepare.py`（46 项）：dry-run 零副作用、动作与配置逐条对应、
  推迟项都有原因、坏配置/坏日志退出码、自检、提权会话里真实执行 `closeNumLock` 再用
  `--rollback` 还原
- `tests/unit/test_regio.cpp` / `test_prepare.cpp`（12 个用例 / 136 条断言）
- CI 增加「封装前设置验收」步骤
- `tests/integration/verify_elevation.py`：自动提权闸门 —— 断言两个产品的嵌入清单声明
  `asInvoker`（启动时自检 + runas 重启提权）、`ospt.exe` 是 WINDOWS 子系统、
  `osptd.exe` 是 CONSOLE，子系统版本仍为 6.1；**未提权时产物正常退出（启动了提权副本）**，
  已提权时直接跑通并自述 `elevated=true`
- `tests/tools/run_acceptance_elevated.ps1`：本机验收统一入口，自己拉一个提权会话把全部闸门
  跑一遍（未提权时用 runas 自我重启，只需提权一次）
- `tests/integration/peinfo.py`：闸门共用的极小 PE 读取工具（机器类型、子系统类型与版本、
  嵌入清单里的 `requestedExecutionLevel`、当前会话是否提权），不依赖 Windows SDK 的 mt.exe

### Changed

- `--prepare` / `--rollback` 只有带 `--execute` 才自动提权；`--dry-run`、只打印计划与
  `--prepare-selftest` 都不提权（CI 与非管理员会话可直接跑）
- **`ospt.exe` 改成 GUI 子系统**（`/SUBSYSTEM:WINDOWS,6.01` + `/ENTRY:mainCRTStartup`）：
  正常启动只出现配置界面，不再附带命令行窗口。**`--console` 显式保留/新建命令行窗口**；
  CLI 与冒烟测试的输出在 stdout 不可用时才回落到父控制台，重定向与管道场景不劫流。
  提权后的 CLI 输出通过临时文件回传，调用方能拿到文本与真实退出码。
- CI 中 GUI exe 的直接 `&` 调用改为管道等待，并移除 runas 包装器残段；
  PowerShell 对 GUI exe 裸 `&` 不等待的问题不再影响 Runplan/Version 等步骤。
- **`ospt.exe` 与 `osptd.exe` 启动时自动提权**：清单保持 `asInvoker`，`main()` 开头自检
  是否提权，未提权时用 `ShellExecuteEx` + `runas` 重新启动自己（带原参数），新进程以管理员
  运行，原进程**等待其退出并返回其退出码**（CI 需要真实结果）。清单不能写
  `requireAdministrator`，否则 `CreateProcess` 在未提权时直接拒绝（740），自动提权逻辑没机会跑。
  **只读开关不自动提权**（`--version` / `--help` / `--selftest` / `--checkup` /
  `--gen-unattend` / `--gen-runplan` / `--ui-smoke-test` / `--win7-compat` / `--dry-run`），
  只有界面（无参数 / `--ui`）和真正需要管理员的操作（`--inject-drivers` / `--sysprep` /
  `--execute` / `--plan` 不带 `--dry-run`）才提权。
- `verify_win7_compat.py` 增加了子系统**类型**断言（此前只看版本 6.1，改成 GUI 子系统不会被发现）
- `verify_ui.py` 的无参数 UI 验收按 `OSPTMainFrame` 找提权子进程窗口，断言不附带
  `ConsoleWindowClass`，并从提权会话发送 `WM_CLOSE`（未提权进程会被 UIPI 拒绝）。
- `verify_drivers.py` / `verify_deploy_exec.py` 的 dry-run 验收在未提权会话也可运行。
- `osptd --win7-compat` 的自述增加 `elevated` 字段（同时用于 CI 记录 runner 的令牌状态）

## [0.1.1] - 2026-10-25

M7 收尾：把配置界面接成能日常使用的程序（可启动、可常驻、配置能读写）。

### Added

- `ospt` **不带参数**或 `ospt --ui [--config <cfg.json>]` 打开配置界面，窗口**常驻到关闭**
  才退出；补 `--help` 打印用法（此前不带参数只打用法就退，界面只能靠冒烟测试间接看到）
- 主菜单（系统标准菜单）：**文件 → 打开配置… / 另存为配置… / 退出**（带 `Ctrl+O` / `Ctrl+S`
  提示）、**工具 → 立即封装体检**（等同 `--checkup`）
- 配置文件读写走的就是 `--gen-unattend` / `--gen-runplan` 那份 `cfg.json` 模型：
  打开即重建全部页面，另存为即把界面上的编辑合并成配置；标题栏显示文件名，
  有未保存修改时带 `*`，关窗时询问是否先保存
- `host/ui` 新增 `runApplication()`（常驻消息循环）、`configFromPages()`、
  `loadConfigFile()` / `saveConfigFile()`；载入失败（非法 JSON / 空文件 / schemaVersion
  不符 / 打不开）都给出可读原因，不崩、也不用默认值顶替
- `--ui-smoke-test` 增加菜单与配置读写的检查：菜单项与文字、菜单命令走真实 `WM_COMMAND`、
  未保存标记、配置存盘再读回逐字一致、非法 JSON 被拒绝 —— 共 127 项
- `verify_ui.py` 增加交互式检查：启动 `--ui`、等窗口出现、确认 2 秒后进程仍在运行、
  发 `WM_CLOSE` 核对退出码 0；并核对 `--help` 里列出的入口
- `tests/unit/test_ui.cpp` 增加配置合并、文件读写与全部错误路径、系统名称不重复拼接

### Fixed

- **「系统名称」重复拼接**：Windows 11 上 `ProductName` 报 `Windows 10 Enterprise`、
  `EditionID` 又是 `Enterprise`，直接拼接会显示成「Windows 10 Enterprise Enterprise」；
  改为先判包含再决定要不要补（活窗口实测发现）
- **选中「工作组」时域输入被静默丢弃**：改选工作组现在会同时清空域名 / 域用户 / 域密码
  三个输入框，做到所见即所存（单测发现）
- 界面按钮仍然**只改配置不执行**：装驱动 / sysprep / 关机依旧只能由命令行 `--execute` 触发

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

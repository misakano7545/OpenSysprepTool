# SC 封装工具功能对照表

> 本表的**唯一依据**是 SC 封装工具 3.0.0.178 自己的产物，不引用任何其他工具的功能：
>
> - `04_resources/LANGS_SCCHS_2052.bin` —— 厂商自带简体中文语言文件（28,796 B，UTF-16LE），
>   内含 `[Controls]` / `[VarControl]` / `[Word]` / `[Tip]` / `[Scandata]` 五个节
> - `06_report/strings_all.txt` —— 主程序字符串转储（UTF-16 需按字节交换还原）
> - `15_spec/功能规格.md` —— 由上面两份整理的规格
>
> **判断规矩**：本表里没有的功能，一律不做。SC 有而我们没有的，才补。
> 反编译产物只读，实现独立编写（宪章 §5）。

状态图例：✅ 已实现并验收 ｜ ⬜ 待做 ｜ ⛔ 明确不做

---

## 一、系统体检（`Gb_Osallinfo`）

### 系统信息（`Gb_Osbasicinfo`）

| 项 | SC 键名 | 状态 |
|---|---|---|
| 系统名称 | `Lab_Osname` | ✅ |
| 内核版本 | `Lab_Osntver` | ✅ |
| 系统位宽 | `Lab_OsBit` | ✅ |
| 封装支持 | `Lab_SysprepSupport` / `Supportok` / `Supportno` | ✅ |
| 系统分区 | `Lab_Osdrives` | ✅ |
| 当前用户 | `Lab_OsUser` | ✅ |
| 运行环境 | `Lab_RunEnviron` | ✅ |
| 使用容量 / 相对容量 | `Lab_Osusesize` / `Lab_OsRelativesize` | ✅ |
| **一键修复** | `Btn_OsRepair` + `[Word] RepairendA/B`（"优化完成，已修复 N 个问题"） | ⬜ |
| 立即处理 / 执行中 / 执行完成 | `[Scandata] Btn_Execute` / `executing` / `Execution` | ⬜ |

### 体检项（`[Scandata]`）

| 分组 | 项 | SC 键名 | 状态 |
|---|---|---|---|
| 封装环境体检 | 缺少必备文件 | `Packageinspect` / `NotExistFile` | ✅ |
| | 克隆版 | `SecondSysprep` | ✅ |
| | 杀软干扰 | `ExistSafeSoft` | ✅ |
| 系统设置体检 | 休眠占用大小 | `Ossetinspect` / `Dormancyon` | ✅ |
| | Dllcache 占用大小 | `optimize_Dllcache` | ✅ |
| 开机启动项体检 | 启动项_文件 | `Osstartrun_Path` | ✅（只做了文件） |
| | 启动项_服务名 | `Osstartrun_Sname` | ✅ |
| | 启动项_计划任务 | `Osstartrun_Task` | ✅（ACL 只给 SYSTEM，非提权时如实报无法判定） |
| 系统插件体检 | BHO 插件 | `Osobj_BHO` | ✅ |
| | BE 插件 | `Osobj_BE` | ✅ |
| | ActiveX 插件 | `Osobj_ActiveX` | ✅ |
| 结果文案 | 发现 N 个问题 / 各项正常 / 部分需手动修复 | `ScanResultsA~D`、`Normaltip` | ✅ |

### 系统优化项（33 条，SC 自带数据表）

SC 把优化项存成一条条 `序号|类型|路径|键名|值` 的记录（序号 2 = HKCU、3 = HKLM）。
下列 27 条已从二进制逐字提取，是**数据而不是猜测**：

| # | SC 描述键 | 注册表位置 | 键名 | 值 |
|---|---|---|---|---|
| 1 | `optimize_diskouttime` | `SYSTEM\ControlSet001\Control\Session Manager` | `AutoChkTimeOut` | `5` (int) |
| 2 | — | `Software\...\Policies\Explorer` | `NoLowDiskSpaceChecks` | `1` (int) |
| 3 | `optimize_Memory` | `SYSTEM\ControlSet001\Control\Session Manager\Memory Management` | `IoPageLockLimit` | `134217728` (int) |
| 4 | `optimize_Closetool` | `Control Panel\Desktop` | `AutoEndTasks` | `1` (str) |
| 5 | `optimize_Pro` | `Software\Microsoft\Windows\CurrentVersion\Explorer` | `DesktopProcess` | `1` (int) |
| 6 | — | `SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon` | `AutoRestartShell` | `1` (int) |
| 7 | `optimize_Toolwtime` | `Control Panel\Desktop` | `HungAppTimeout` | `3000` (str) |
| 8 | `optimize_Toolwtime` | `Control Panel\Desktop` | `WaitToKillAppTimeout` | `10000` (str) |
| 9 | `optimize_Pro` | `SYSTEM\ControlSet001\Control\PriorityControl` | `Win32PriorityControl` | `38` (int) |
| 10 | `optimize_BsReboot` | `SYSTEM\ControlSet001\Control\CrashControl` | `AutoReboot` | `0` (int) |
| 11 | `optimize_EditReg` | `SYSTEM\ControlSet001\Control\SecurePipeServers\winreg` | `remoteregaccess` | `1` (int) |
| 12 | `optimize_AeDebug` | `SOFTWARE\Microsoft\Windows NT\CurrentVersion\AeDebug` | `Auto` | `0` (str) |
| 13 | `optimize_NTFS` | `SYSTEM\ControlSet001\Control\FileSystem` | `NtfsDisableLastAccessUpdate` | `1` (int) |
| 14 | `optimize_Datasave` | `SOFTWARE\Microsoft\Windows\CurrentVersion\OptimalLayout` | `EnableAutoLayout` | `1` (int) |
| 15 | `optimize_MaxCached` | `SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer` | `Max Cached Icons` | `2000` (str) |
| 16 | `optimize_IEConnect` | `Software\Microsoft\Windows\CurrentVersion\Internet Settings` | `MaxConnectionsPer1_0Server` | `10` (int) |
| 17 | `optimize_Drvcat` | `Software\Policies\Microsoft\Windows NT\Driver Signing` | `BehaviorOnFailedVerify` | `0` (int) |
| 18 | `optimize_USBauto` | `SYSTEM\ControlSet001\Services\USBSTOR` | `AutoRun` | `1` (int) |
| 19 | `optimize_Winstime` | `SYSTEM\ControlSet001\services\NetBT\Parameters` | `NameSrvQueryTimeout` | `3000` (int) |
| 20 | `optimize_NetTTL` | `SYSTEM\ControlSet001\services\Tcpip\Parameters` | `DefaultTTL` | `64` (int) |
| 21 | `optimize_NetOpts` | `SYSTEM\ControlSet001\services\Tcpip\Parameters` | `Tcp1323Opts` | `1` (int) |
| 22 | `optimize_Dellnk` | `Software\Microsoft\Windows\CurrentVersion\Explorer` | `Link` | `00000000` (binary) |
| 23 | `optimize_Mousetime` | `Control Panel\Mouse` | `mousehovertime` | `100` (str) |
| 24 | `optimize_MenuShow` | `Control Panel\desktop` | `MenuShowDelay` | `0` (str) |
| 25 | — | `Software\Microsoft\Windows\CurrentVersion\Policies\system` | `disablecad` | `1` (int) |
| 26 | `optimize_Boot` | `SOFTWARE\Microsoft\Dfrg\BootOptimizeFunction` | `Enable` | `Y` (str) |
| 27 | — | `Software\Microsoft\Windows\CurrentVersion\Policies\Explorer` | `NoSimpleNetIDList` | `1` (int) |

另有 6 条在语言文件里有描述、但二进制里没有对应的单行记录（走命令或另有实现）：
`optimize_Aero`、`optimize_RefreshFile`、`optimize_Process`、`optimize_AutoRefresh`、
`optimize_Cmsgsd`、`optimize_Sdmsg`（后者已由 `closeRebootDialog` 覆盖）。

> 实现约定：`ControlSet001` 一律改写为 `CurrentControlSet` —— 目标机的当前控制集不一定是
> 001，照抄会把设置写到一份没被使用的控制集里。这是唯一一处刻意偏离 SC 数据的地方，
> 原因写在代码注释里。

---

## 二、目标系统设置

### 系统信息（`Gb_Targetsys_info`）

| 项 | SC 键名 | 状态 |
|---|---|---|
| 注册名称 / 注册单位 / 序列号（需含 `-`） | `Ed_Owner` / `Ed_Organization` / `Ed_ProductKey` | ✅ |
| 计算机名（系统默认随机 / 前缀随机 / 完全固定） | `Cb_Pcname` + `Pcname_0..2` | ✅ |
| 启动菜单时间 | `Se_Boottime` | ✅ |
| OEM 设置入口 | `Btn_OemSet` | ✅（见「OEM」一节） |

### 首次进桌面（`Gb_Targetsys_Desktop`）

| 项 | SC 键名 | 状态 |
|---|---|---|
| 运行设备管理器 | `Cb_ShowDevmgmt` | ✅ |
| 运行完成后重启 | `Cb_Reboot` | ✅ |
| 设置指定分辨率 / 刷新率 / 颜色质量 | `Cb_SetResolution` / `Ed_Refresh` / `Ed_ColorQuality` | ✅ |
| 黑屏急救 Ctrl+F11 | `Cb_ResolutionSos` | ✅ |
| **同步封装前 IE 主页网址** | `Cb_Saveiehp` | ✅（Win10+ 的 IE Main 键带拒绝写入 ACL，写不进时降级为告警） |
| **同步封装前 IE 收藏夹数据** | `Cb_SaveieFavorites` | ✅（写默认用户 hive 的 Favorites 路径） |

### 系统优化（`Gb_ScTools` 与相邻控件）

| 项 | SC 键名 | 状态 |
|---|---|---|
| 清理多余的 SRS 驱动 | `Cb_ClearSrs` | ✅ |
| 询问是否创建宽带连接（静默 / 倒计时） | `Cb_CreateConnection` + `Rb_Silence_nc` / `Rb_Show_nc` | ⬜ |
| 自动设置网络位置 | `Cb_SetNetWork` | ✅ |
| 关闭系统重启对话框 | `Cb_CloseRestartbox` | ✅ |
| **将文件固定到任务栏 / 取消固定**（含文件清单） | `Cb_Locktaskbar` / `Cb_Unlocktaskbar` / `Items_Lockfile` / `Items_UnLockfile` | ✅（只做机制：写默认用户的 `User Pinned\TaskBar`，清单必须由用户填；SC 用它推推广快捷方式，那部分不做） |
| **智能转移我的文档**（智能 / 指定目录） | `Cb_MoveDocuments` + `Rb_SmartMoveDocuments` / `Rb_DiyMoveDocuments` | ✅ |
| **智能转移 IE 收藏夹**（智能 / 指定目录） | `Cb_MoveFavorites` + `Rb_SmartMoveFavorites` / `Rb_DiyMoveFavorites` | ✅ |
| 智能转移虚拟内存（智能 / 指定盘） | `Cb_MovePageFile` + `Rb_SmartMovePageFile` / `Rb_DiyMovePageFile` | ✅ |
| **虚拟内存值（物理内存倍数 / 固定值）** | `Rb_PageFilevarValue` / `Rb_PageFileconstValue` + 四个数值框 | ⬜ |
| 关闭笔记本小键盘 / 开启笔记本休眠 | `Cb_CloseKeypad` / `Cb_Dormancy` | ✅ |
| **自动设置 IP**（固定 / 随机 / IP 段 + 掩码 / 网关 / DNS） | `Cb_AutoSetip` + `Rb_Setip` / `Rb_SetRandomip` / `Lab_Startip`… | ✅（随机模式=同 /24 取址；SC 原始算法未见，已在代码注释与本节写明） |
| 联网安装缺失的硬件驱动 | `Cb_Driveinstall` | ⛔ 推广（驱动总裁），宪章 §7 |
| 联网安装总裁软件安装器 | `Cb_Mboxinstall` | ⛔ 推广（软件魔盒），宪章 §7 |

---

## 三、运行计划（`Lab_RunPlan`）

| 维度 | SC 取值 | 状态 |
|---|---|---|
| 5 个阶段 | 部署前 / 部署中 / 部署后 / 登录时 / 进桌面 | ✅ |
| 任务类型 | 运行程序 / 导注册表 / 删除文件 / 删除目录 | ✅ |
| 运行方式 | 正常 / 隐藏 / 最小化 / 最大化 | ✅ |
| 运行等待 | 等待完成 / 不等待 / 自定义秒数 | ✅ |
| 善后处理 | 保持默认 / 删除文件 | ✅ |
| 执行路径 | 系统路径 / 外置U盘 / 外置光盘 / 所有盘符 | ✅ |
| 任务列表编辑（编辑 / 上移 / 下移 / 删除 / 删除所有） | `Tasklist_menu_*` | ✅ |

---

## 四、部署设置（`Gb_DeploySet`）

| 项 | SC 键名 | 状态 |
|---|---|---|
| 四格进度 / TrueTrans / 多彩进度条 / 图片控件 | `Items_Line11~42` | ✅ |
| 单图 / 多图（切换速度）/ 系统默认 | `BackgroundType_0..2` / `Lab_PicSpeed` | ✅ |
| 顶部文字标题 / 部署分辨率 / 文字颜色 | `Cb_DeployTitle` / `Lab_DeployResolution` / `Lab_DeployFontcolour` | ✅ |
| 跳过 OOBE / 自动新增用户 / 开启 Administrator | `Cb_Skipoobe` / `Cb_AddUser` / `Cb_Admin` | ✅ |
| **禁止在虚拟机里部署** | `Btn_DisableVmrun` | ✅（CPUID hypervisor 位 + 厂商串判定） |
| **校验文件防篡改**（锁定文件 + MD5 + 篡改提示） | `Cb_Tampering` + `Items_Tampering` / `Items_FileMd5` / `Lab_TamperingTip` | ✅（部署前闸门，MD5 用 RFC 1321 实现，与 certutil 交叉验证） |
| **四套模块各自的配色与文字设置** | `Gb_DM1_Fontset` / `Gb_DM1_Probarset` / `Text1~4_DM1_*` / `Pbtype_*` / `Pictype*` | ⬜ |
| 模块附加开关（安装计时 / 已用时间 / 系统时间 / 快捷键启动） | `Cb_DM1_Timer` / `Cb_DM2usetime` / `Cb_DM2systime` / `Cb_Openhotkey` | ⬜ |

---

## 五、封装（`Gb_Sysprep_*`）

| 项 | SC 键名 | 状态 |
|---|---|---|
| 封装方式：传统 / 防流氓 / 大客户高级 | `Rb_Sysprep_Type1..3` | ✅ 传统、✅ 防流氓、⬜ 大客户高级 |
| 生成新 SID | `Cb_Createnewsid` | ✅ |
| **卸载本机设备驱动** | `Cb_Uninstalldriver` | ✅（解析 `INF\oem*.inf` 找第三方包 → `pnputil /delete-driver`；存储类强制跳过，删了会 0x7B） |
| 加载 SRS 磁盘控制器驱动 | `Cb_LoadSrsDrv` | ✅ |
| 转移桌面文件到所有用户 | `Cb_Cutdesktopfile` | ✅ |
| 封装后清理系统垃圾文件 / 清理 DLLcache | `Cb_Cleanjunkfiles` / `Cb_Dllcache` | ✅ |
| 自动登录 Administrator（密码 / 次数） | `SA_Lab_AdminAutoLogon` / `SA_Lab_Adminpw` / `SA_Lab_Lnum` | ✅ |
| **部署主控被劫持**（继续 / 终止 + 弹提示） | `SA_Gb_hijack` / `SA_Rb_hijack_type1..2` / `SA_Lab_ShowTip` | ✅（比对 osptd 自身 MD5；terminate 弹窗并中止） |
| OEM 智能识别（属性 / 壁纸 / 部署图片） | `SA_Gb_oem` / `SA_Lab_oemlogo` / `SA_Lab_oembgpic` / `SA_Lab_oemDeploy` | ✅ 属性、⬜ 壁纸与部署图片 |
| 工作组 / 加入域（域用户 + 密码） | `Rb_WorkGroup` / `Rb_Domain` / `Lab_Domain_user` / `Lab_Domain_pw` | ✅ |
| 每设备或每用户 / 每服务器（同时连接数） | `Rb_EveryOne` / `Rb_EverySer` | ⬜ |
| 封装后：停留 / 重启 / 关机 | `Items_AfterSysprep1..3` | ✅ |

### OEM 设置（`Btn_OemSet`）细分

| 分组 | 项 | SC 键名 | 状态 |
|---|---|---|---|
| 系统属性信息 | 厂商名称 / 系统型号 / 厂商网址 / 服务时间 / 服务电话 | `Gb_Sysattribute` + `Lab_Oem*` | ✅ |
| | 厂商图片 | `Lab_OemLogo` | ⬜（Win10+ 已弃用 Logo，需先确认落点） |
| 开始菜单信息 | 注册姓名 / 注册单位 | `Gb_Startmenuattribute` / `Lab_OemRegname` / `Lab_OemRegunit` | ⬜ |
| 技术支持信息 | IE 标题栏 / IE 底部 / 菜单名称 / 链接说明 / 右键说明 / 链接动作 / 菜单图标 | `Gb_Supportinfo` / `Lab_Oemietitle` … `Lab_Oemmenupic` | ⬜ |
| 预设品牌标识 / 清除 OEM / 预览 | `Lab_OemManufacturerlist` / `Btn_OemClear` / `Btn_OemPreview` | ✅ 预设品牌、⬜ 清除与预览 |

---

## 六、其他（`PM_*` / 联盟）

| 项 | SC 键名 | 状态 |
|---|---|---|
| 导入配置文件（`.sc` / `.ini`） | `PM_Loadcon` | ✅ `.ini` 的 OEM 段；⬜ 设置段 |
| 导出配置文件（`.sc` / `.ini`） | `PM_Saveconsc` / `PM_Saveconini` | ⬜（宪章只承诺导入） |
| 补丁安装 | `Pm_hotfix` | ⬜ |
| 检查新版本 / 错误报告 / 论坛 / 官网 / 关于 | `PM_Cver` / `PM_Bug` / `PM_Bbs` / `PM_Url` / `PM_About` | ⛔ 非封装功能，宪章 §8 明确排除 |
| 登录装机联盟 / 特权 / 生成授权文件 | `Lab_UnLogin` … `Btn_Accredit` | ⛔ 联盟分成，宪章 §7 |
| 防止文件删除 / 防止注册表删除（`Page_Nodelfile` / `Page_Nodelreg`） | 需要内核驱动常驻 | ⛔ 宪章 §7 明令禁止内核驱动防删除 |

---

## 七、本仓库**删掉**的非 SC 功能

这些是前期误从其他工具（ES5P / Easy Sysprep）搬来的，SC 里没有，已删除：

| 已删功能 | 原落点 | 为什么删 |
|---|---|---|
| 映像捕获（`DISM /Capture-Image`） | `core/dism/capture.cpp`、`host/cli_capture.cpp` | SC 的字符串表里没有任何 WIM/映像捕获；`功能规格.md` 也无此项 |
| 隐私痕迹清理（地址栏/运行/最近文档/托盘） | `optimization.clearPrivacyTraces` | SC 的清理只有「垃圾文件」与「DLLcache」，没有痕迹清理 |
| Windows 更新缓存清理 | `optimization.clearUpdateCache` | 同上，SC 无 |
| 封装前清理 6 类（进程/卷影/事件日志/回收站/组件库/unattend 残留） | `core/prepare` 的 `clean*` | SC 只有 `Cb_Cleanjunkfiles` 与 `Cb_Dllcache` 两项 |

# SC 窗体对照表（UI 逐控件对齐）

> 依据：`D:\dev\scpt\out\02_forms\dfm\` 下 7 个 DFM 窗体与同目录的 Pascal 类声明，
> 配合 SC 自带的 `LANGS_SCCHS_2052.bin` 语言文件（Caption 来源）。
> 布局数据由 `tools/dfm_layout.py` 解析成 `docs/sc_layout/*.json` 并随仓库提交，
> `tests/unit/test_ui_layout.cpp` 拿它逐控件比对界面模型 —— 漂了会红。
>
> **只提取界面事实**（控件名 / 类名 / Caption / 坐标 / 尺寸），不搬运 Delphi 代码，
> 也不搬运图片二进制（`Picture.Data` 只记字节数）。宪章 §5。

---

## 1. 七个窗体

| # | DFM | 类 | 尺寸 | 是什么 | 我方状态 |
|---|---|---|---|---|---|
| 1 | `TSCMF` | `TScmf` | 800×540 | 主窗体，`Page_mf` 下 11 个页签 | 🟡 7/11 页签，见下 |
| 2 | `TSBOX` | `TSbox` | 300×136 | 小弹窗：页签「重启方式」`Rb_Silence`/`Rb_Show`+`Btn_Scrok`；页签「宽带创建方式」`Rb_Silence_nc`/`Rb_Show_nc`+`Btn_Ncok` | ✅ `host/ui/popups.cpp` |
| 3 | `TSCUBOX` | `TScubox` | 300×136 | 小弹窗：`Lab_pwub` + `Ed_Unpwub`（密码）+ `Btn_Okub` / `Btn_Ubox_Exit` | ⛔ 语言文件里查不到对应文案，用途无法确认 |
| 4 | `TAPPMSGFORM` | `TAppMsgForm` | 480×242 | 通用提示框：`lbTip` 正文 + 单按钮（`Btn_Bigok`）或双按钮（`Btn_Exit`/`Btn_Ok`） | ✅ `showMessageDialog` |
| 5 | `TTLMF` | `TTlmf` | 740×400 | 「任务」对话框：任务类型 / 五个阶段按钮 / 运行方式与等待 / 执行路径与参数 / 善后处理 / 状态行 | ✅ `host/ui/task_dialog.cpp` |
| 6 | `TMFFUN` | `TMFfun` | 740×400 | **高级设置总窗体**：图纸里 22 个 `TMlSkinTabSheet`、约 370 个控件 | 🟡 窗体 + 前三页已做；§7 红线两页整页删掉；其余 17 页标未做（见 §4.4） |
| 7 | `TMLSKINRESDM` | — | — | 皮肤资源（2 个控件，无界面语义） | — 不适用 |

---

## 2. 主窗体 `TSCMF` 的 11 个页签

`Page_mf`（800×398）下按 DFM 顺序：

| # | 页签对象 | Caption | 我方 | 说明 |
|---|---|---|---|---|
| 1 | `MlSkinTabSheet1` | 母盘系统 | ✅ 字段 / ⬜ 版面 | 见 §4 |
| 2 | `MlSkinTabSheet2` | 目标系统 | ✅ 字段 + ✅ 版面 | 已按 DFM 绝对坐标摆放 |
| 3 | `MlSkinTabSheet3` | 计划任务 | ✅ 字段 / ⬜ 版面 | 见 §4 |
| 4 | `MlSkinTabSheet4` | 部署过程 | ✅ 字段 / ⬜ 版面 | 见 §4 |
| 5 | `MlSkinTabSheet5` | 封装过程 | ✅ 字段 / ⬜ 版面 | 见 §4 |
| 6 | `MlSkinTabSheet6` | 运行 | ✅ **本次改为 DFM 绝对坐标** | 见 §3.1 |
| 7 | `MlSkinTabSheet7` | 完成 | ✅ **本次改为 DFM 绝对坐标** | 见 §3.2，唯一缺项 `Btn_Bugs` |
| 8 | `MlSkinTabSheet8` | Temp | ⛔ 不对用户暴露 | SC 内部临时页 |
| 9 | `MlSkinTabSheet9` | 更新 | ⛔ 不做 | 依赖 SC 的更新服务器（2026-10-25 裁决） |
| 10 | `MlSkinTabSheet10` | 更新中 | ⛔ 不做 | 同上 |
| 11 | `MlSkinTabSheet11` | End | ⛔ 不对用户暴露 | 内部收尾页 |

---

## 3. 运行 / 完成页（本次修正）

之前这两页走的是通用「分组框 + 一行一个字段」的自动排列，形状与 SC 完全不符：
SC 是「居中大图 + 居中说明 + 底部双进度条」和「居中结果图 + 居中说明 + 完成度条 +
倒计时 + 三个按钮」的固定版面。现在整页按 DFM 绝对坐标摆放（设计空间 800×398，
按客户区宽度等比缩放），控件名、顺序、坐标、尺寸、Caption 全部来自 DFM。

### 3.1 运行页 `MlSkinTabSheet6`

| 控件 | 类 | 坐标 | 尺寸 | Caption |
|---|---|---|---|---|
| `Pan_Work` | TMlSkinPanel | 3,3 | 794×375 | （无边框面板） |
| `Pic_RunGif` | TImage | 347,104 | 100×100 | 动图（我方自绘转圈） |
| `Pro_Total` | TAeroColorProgressBar | 97,333 | 600×10 | — |
| `Pro_Task` | TAeroColorProgressBar | 97,322 | 600×5 | — |
| `Lab_Run` | TMlSkinLabel | 0,231 | 794×34 | 正在执行，请稍候…（居中） |
| `Lab_Total` | TMlSkinLabel | 13,331 | 78×22 | 总进度:（右对齐） |

### 3.2 完成页 `MlSkinTabSheet7`

| 控件 | 类 | 坐标 | 尺寸 | Caption | 我方 |
|---|---|---|---|---|---|
| `Pan_End` | TMlSkinPanel | 3,3 | 794×375 | — | ✅ |
| `PIC_Success` | TImage | 331,96 | 118×96 | — | ✅ 自绘对勾 |
| `Lab_Complete` | TLabel | 13,209 | 762×19 | 恭喜您，执行成功！ | ✅ |
| `PIC_Error` | TImage | 331,96 | 118×96 | — | ✅ 自绘叉（与成功图同矩形，互斥） |
| `Pro_End` | TAeroColorProgressBar | 152,254 | 525×8 | — | ✅ |
| `Lab_Countdown` | TLabel | 152,268 | 288×12 | 程序将在10秒后重启计算机，如需取消请点“返回”。 | ✅ 逐字 |
| `Lab_CompleteErr` | TLabel | 13,209 | 762×19 | 错误，执行失败！ | ✅ |
| `Btn_Bugs` | TMlSkinColorButton | 331,268 | 118×27 | Bug | ⛔ 错误报告要连 SC 的服务器 |
| `Btn_Shutdown` | TMlSkinColorButton | 275,242 | 94×27 | 关机(&T) | ✅ |
| `Btn_Restart` | TMlSkinColorButton | 421,242 | 94×27 | 重启(&R) | ✅ |

`Btn_Bugs` 是本页唯一的有意缺项：它的动作是把错误信息发到 SC 的服务器，属宪章 §7
红线（不再为它预留接口），2026-10-25 已裁决不做。

---

## 4. 配置页签的版面：目标系统已对齐，其余四页待做

| 页签 | 控件集合 | 版面（DFM 绝对坐标） |
|---|---|---|
| `MlSkinTabSheet1` 母盘系统 | 16 | ✅ |
| `MlSkinTabSheet2` 目标系统 | 34 | ✅（整页生成物） |
| `MlSkinTabSheet3` 计划任务 | 15 | ✅ |
| `MlSkinTabSheet4` 部署过程 | 12 | ✅ |
| `MlSkinTabSheet5` 封装过程 | 21 | ✅ |
| `MlSkinTabSheet6` 运行 | 5 | ✅ |
| `MlSkinTabSheet7` 完成 | 8 | ✅ |

七页共 111 个控件按图纸坐标摆放，冒烟测试逐控件 `GetWindowRect` 核对（位置容差
2px、高度容差 6px —— 组合框的实际高度受字体影响，比图纸差几像素是系统行为）。

**我方比 SC 多出来的字段**（自动登录三件套、单个 RunPlan 按钮等图纸里没有位置的）
排在图纸区域下方的一条「扩展区」，页面高度跟着加长。这样既保住图纸版面，也不丢
功能；它们的存在理由在 `docs/SC功能对照.md` 里各自记着。

做法已经固化成工具链，剩余四页是重复劳动：

1. `python tools/dfm_layout.py --src <SC 的 dfm 目录> --out docs/sc_layout`
   —— 从 DFM 提取控件树与坐标（含 `Items.Strings` 下拉条目）
2. 写 `tools/ui_bindings/<page>.json`：控件名 → `kind` / `binding` / 取值表达式；
   红线控件在 `exclude` 里写名字 + 原因
3. `python tools/gen_ui_page.py --layout docs/sc_layout/TSCMF.json --binding ... --out host/ui/ui_pages_dfm.inc`
   —— 生成 C++ 代码（坐标由 DFM 给，人不碰数字）
4. 页面构造里 `#include "ui_pages_dfm.inc"`，`page.dfmLayout = true`，
   同步 `expectedControlIds()` 与 `verify_ui.py` 的核对条数

**坐标不能手抄**：本轮就抓到两处手抄错误 —— 分辨率下拉我按记忆写了 `800x600` /
11 条到 `1920x1080`，而 DFM 里实际是 `800*600` … `1920*1200`（分隔符和条目都不同）。
生成器的存在就是为了让这类错误不可能发生。

---

## 4.1 TSBOX 弹窗（已完成）

`host/ui/popups.cpp` 实现 SC 的 300×136 确认框，两个模式：

| 模式 | 触发点 | 单选按钮 | 确定按钮 |
|---|---|---|---|
| 重启方式 | 完成页「重启(&R)」 | 静默重启[无界面] / 倒计时重启[有界面] | 确定(&K) |
| 宽带创建方式 | 目标系统页「询问创建宽带连接」 | 静默创建[无界面] / 倒计时创建[有界面] | 确定(&K) |

**文案以语言文件为准，不是 DFM**：DFM 里宽带页签的两个单选按钮 Caption 写的是
「静默重启」，那是 SC 自己的复制粘贴错误；运行时被 `LANGS_SCCHS_2052.bin` 覆盖成
「静默创建」。这一条写进了 `popups.cpp` 的注释与 `test_popups.cpp`，避免以后有人
「照着 DFM 改回去」。

坐标：页签容器 `Sbpage` 在 (7,8)；单选在相对 (5,36) / (147,36)，确定按钮在 (184,81)，
尺寸 73×27 —— 全部逐字来自 DFM。冒烟测试实测客户区 300×136、按钮落在 (191,89)。

## 4.2 TTLMF 任务对话框（已完成）

`host/ui/task_dialog.cpp`，740×400，控件坐标逐条来自 DFM：

| 控件 | DFM 坐标 | 说明 |
|---|---|---|
| `Cb_Tasktype` | 150,67 106×26 | 运行程序 / 导注册表 / 删除文件 / 删除目录 |
| `Btn_RunPlan1..5` | 4/103/202/301/401, y=9（在 `Pl_RunPlan` 内） | 部署前 / 部署中 / 部署后 / 登录时 / 桌面后 |
| `Cb_RunMode` | 154,171 106×26 | 常规 / 隐藏 / 最小化 / 最大化 |
| `Cb_RunWait` | 346,171 106×26 | 等待完成 / 不等待 / 自定义秒数 |
| `Sed_WaitTime` + `Lab_WaitTime_S` | 537,171 90×26 / 626,176 | 等待秒数，右侧「S」 |
| `Cb_RunPath` / `Ed_RunPath` / `Btn_Open` | 154,219 / 266,219 / 569,219 | 路径类型 / 具体路径 / 浏览 |
| `Ed_CmdLine` | 154,267 298×26 | 执行参数 |
| `Cb_RunAfter` | 537,267 106×26 | 善后处理：保留 / 删除 |
| `Btn_Save` | 537,330 106×27 | 添 加(&T) |
| `Lab_run` | 87,336 444×15 | 状态行：校验失败时在这里说明（不弹二级对话框） |

**不做**：图纸里 `Ed_Temp1..4`（y=366）和一个跑到 (5440,371) 的 `Ed_temp` 是 SC 存中间值
用的隐藏编辑框，界面上看不见也没有语义。

入口：计划任务页的「添加」按钮打开它，页面上那排字段作为对话框初值（我们的快速草稿）。

## 4.3 TAPPMSGFORM 提示框（已完成）

`showMessageDialog(owner, text, MessageKind)`：480×242，`lbTip` 在 (16,72 449×73)。
两种模式：

| 模式 | 按钮 | 用在 |
|---|---|---|
| `Info` | `Btn_Bigok`(0,191 480×51)「确定(&K)」 | 载入/保存失败、无法开始部署、关机重启失败 |
| `Confirm` | `Btn_Exit`(0,191 240×51)「取消(&E)」+ `Btn_Ok`(240,191 240×51)「确定(&K)」 | 打开其它配置会丢弃未保存修改时 |

标题行 `Lab_little`(16,6 121×25) 在 SC 里写的是它的品牌名「封装总裁」，这里换成
`OpenSysprepTool` —— 品牌字样属于宪章 §5 不搬运的范围。

**保留系统对话框的一处**：退出时「是 / 否 / 取消」三选。图纸只有两个按钮，做不出第三键，
所以那一处仍用 `MessageBox`，并在代码注释里写明原因。

## 4.4 TMFFUN 高级设置总窗体（窗体 + 前三页已做）

这是 SC 的「高级设置」总窗体，740×400，18 个页签，约 370 个控件：

| 页签 | 控件数 | 对应我们的配置 |
|---|---|---|
| 模块1设置 / 模块2设置 | 39 / 42 | `deploy` 段的部署模块参数 |
| 转移文档 / 转移收藏夹 | 11 / 11 | `optimization.myDocumentsTarget` / `favoritesTarget` |
| 转移虚拟内存 | 21 | `optimization.pageFileTarget` |
| 新增用户 | 10 | `deploy.autoCreateUser` + 账号密码 |
| 预置IP | 28 | `optimization.autoSetIp` 三模式 + 掩码/网关/DNS |
| 固定文件 / 取消固定 | 7 / 7 | `lockTaskbarFiles` / `unlockTaskbarFiles` |
| OEM | 47 | `oem` 六项 + 壁纸 + Logo |
| 选择网络 | 10 | `autoNetworkLocation` / `networkLocation` |
| 登录 | 34 | `sysprep.autoLogon` + 次数 + 密码 |
| 文件锁定 | 30 | `deploy.tamperFiles`（防篡改清单） |
| 图片部署 | 36 | `deploy.backgroundImage` / 分辨率 / 切换秒数 |
| 封装高级设置 | 21 | `sysprep` 高级项 |
| 补丁安装 | 14 | **无对应**：SC 的补丁安装我们没做 |
| TrueTrans / End / 两个空页 | 3 / 1 / 1 | 内部页 |

**做法**（和主窗体那边一致，坐标不手抄）：
`tools/ui_bindings/tmffun.json` 写绑定，`tools/gen_tmffun.py` 从
`docs/sc_layout/TMFFUN.json` 取坐标，生成 `host/ui/tmffun_table.inc`。
生成器按页签输出控件表，**先控件表后页签表**（C++ 先声明后使用），
组框里的控件坐标相对组框（与 DFM 语义一致）。

**已做**（`host/ui/tmffun.cpp`）：窗体框架 + 五个页签

| 页签 | 控件 | 落到 |
|---|---|---|
| 转移文档 | 10 | `optimization.documentsSmart` / `myDocumentsTarget` |
| 转移收藏夹 | 10 | `optimization.favoritesSmart` / `favoritesTarget` |
| 转移虚拟内存 | 20 | `pageFileSmart` / `pageFileDrive` / `pageFileSizeMode` + 四个数值 |
| 预置IP | 26 | `ipMode` / `ipAddress` / `ipRandomBase` / `ipRandomOctetStart·End` / 掩码·网关·两个 DNS |
| 文件锁定 | 8 | `deploy.tamperFiles`（path + MD5 + 说明），`verifyIntegrity` 随之打开 |
| OEM | 24 | `oem` 六项 + `oem.logo`（→ unattend `<Logo>`）+ `oem.ieTitle`（→ IE 窗口标题）+ 注册姓名/单位 |
| 封装高级设置 | 16 | 自动登录开关/密码/次数 + 劫持方式（继续/终止）+ 提示开关与文案 + OEM 智能识别 |

**预置IP 页带出一处修正**：SC 的随机 IP 是「`Ed_ips` 三段基址 + `Ed_Startip`/`Ed_Endip`
末段范围」，随机时只换末段。早先的实现没有这两项，只能在本机当前 /24 里随机 ——
那是我们的取舍而非 SC 的行为，现在按 SC 的语义补上了（`ipRandomBase` /
`ipRandomOctetStart` / `ipRandomOctetEnd`），原来的兜底逻辑仅在既没给基址时使用。

**OEM 页做了 24 个控件，另有 7 个不做**（都在 `tools/ui_bindings/tmffun.json` 的
`excludeControls` 里写了原因）：

| 不做的控件 | 原因 |
|---|---|
| `Gb_Startmenuattribute` 整组（菜单名称/链接说明/右键说明/链接动作/菜单图标） | 这是往开始菜单里拉一个自定义入口、链接动作可指向任意 URL／程序的位置，正是 SC 投放推广入口的地方（§7）。我们没有创建快捷方式的模块，也不提供半成品入口 |
| `Cb_OemManufacturerlist` + 它的标签 | 「预设品牌标识」下拉：SC 自带合作品牌清单，属 §5 不搬运范围；要预设只能让用户自己导入 |
| `Lab_Loadusesoem` | 「加载上一次使用的OEM信息」：得先定义「上一次」存在哪，本轮不做 |
| `Ed_Oemiebottom` + 标签 | 「IE底部」：图纸只有 Caption，查不到写进哪个键/文件，没有依据不猜 |
| `MM_Supportinfo` | 「技术支持信息」多行框：oeminfo 里没有对应元素，写到哪里没有证据，不猜 |

**两项新增字段是真的落地的**：`oem.logo` 进 unattend 的 `<OEMInformation><Logo>`（文档
字母序里排在 Manufacturer 之后、Model 之前）；`oem.ieTitle` 由 `--prepare` 写进默认用户
配置单元的 `Software\Microsoft\Internet Explorer\Main` 的 `Window Title`
（`tolerateFailure`，Win10+ 对该键的部分值会拒绝写入）。IE 标题属 OEM 品牌字样，
§7 禁止的是改主页/默认搜索，不是标题栏。

**文件锁定页是嵌套页**：图纸里这一页套着一个 `Page_Tn` 页签控件，里面三层
`Page_Tampering` / `Page_Nodelfile` / `Page_Nodelreg`。生成器现在只把直接挂在
`Mfpage` 下的当顶层页签（19 个），嵌套子页按 `subSheet` 摊平成一级坐标。
只取 `Page_Tampering`（防篡改清单，机制我们已有），另两层是 §7 红线，整页删掉。

**图纸里的页签是 22 个**（18 个编号页 + `TrueTrans` + `Page_Tampering` +
`Page_Nodelfile` + `Page_Nodelreg`），处理分三类：

| 类别 | 页签 | 处理 |
|---|---|---|
| 本轮已排 | 转移文档 / 转移收藏夹 / 转移虚拟内存 | 按 DFM 坐标摆控件 |
| 未做但保留 | 其余 12 个（模块设置、新增用户、固定/取消固定、选择网络、图片部署…） | 页签仍在，点进去写明未做的原因 |
| **明确不做（有原因）** | 「图片部署」页 | 这一页是 SC 图形化部署外壳的皮肤设置（进度条样式/字体颜色/取色/快捷键启动程序/已用时间/系统时间）。我们的部署端是命令行 + 结构化进度通道，没有那个外壳，做出来就是「存了不生效的设置」 |
| **整页删掉** | `Page_Nodelfile`、`Page_Nodelreg` | 宪章 §7 红线：SC 用内核驱动锁住文件/注册表不让删。连页签都不出现，不给它留入口 |
| **整页不做** | 「登录」页 | 查清后才发现这一页不是自动登录，而是「登录系统总裁装机联盟」+ VIP 特权（计费ID、生成授权文件、退出当前用户、购买特权）。这是宪章 §7 禁止的联盟账户体系，留在界面上等于给它留入口 |
| 不做（已定） | 「补丁安装」页 | 补丁安装（MSI/MSU 安装与回滚）是独立一大块，本工具不做，页签保留并写明 |

删掉而不是标「未实现」的理由：标未实现等于告诉用户「这里本该有个功能」，
而这两个功能我们**永远不做**，留着页签就是留一个未来可能被填上的坑。

**防篡改清单可删**：右键清单 →「删除 / 删除所有」（对应图纸的 `Tampering_menu`）。
清单清空时 `verifyIntegrity` 自动关掉 —— 否则部署端会以「要求校验但列表为空」拒绝执行。

**虚拟内存大小可回到系统管理**：图纸只有「倍数 / 固定值」两个单选，没有第三个。
所以做法是**再点一次已选中的单选就取消**，两个都不选 = 由系统管理；底部状态行
会写明「当前：由系统管理大小」。这样不加图纸里没有的控件，也到得了这个状态。

**两处行为约定**（写在这里免得以后又改回去）：

- 页签**只在第一次显示时从配置装载**。切页签不重读，否则正在编辑但没点「保存」的内容
  会被静默丢掉——切一下页签就白打了。保存过的页签算「已装载」。
- 每页底部的「保存」只提交当前页签；没保存就退出会丢，这是 SC 的行为，保持不变。

**入口**：菜单「工具 → 高级设置(&A)…」（对应 SC 的 `Btn_Sysprepaset`）与
「工具 → 高级设置：OEM(&M)…」（对应 `Btn_OemSet`，直接落在 OEM 页）。

**一处事实纠正**：早先按 `15_spec/功能规格.md` 的简述写成「SC 的 MovePageFile 只搬位置，
大小仍交给系统管理（`0 0`）」。查 `LANGS_SCCHS_2052.bin` 后发现这是错的 ——
语言文件明确有 `Rb_PageFilevarValue`「指定虚拟内存值为物理内存的倍数」与
`Rb_PageFileconstValue`「指定虚拟内存固定值」，各带最小/最大值。所以大小是要设的；
不选任何一个单选时才回落 `0 0`。已按此实现 `pageFileSizeFields()`。

## 5. 与 DFM 有关的三条硬规矩（写进代码注释与测试）

1. **页签 Caption 逐字**：`Page::title` 必须等于 DFM 的 `TabSheet.Caption`
   （`母盘系统` / `目标系统` / `计划任务` / `部署过程` / `封装过程` / `运行` / `完成`）。
2. **控件名逐字**：`Field::id` 必须等于 DFM 的对象名（`Cb_*` / `Lab_*` / `Btn_*` /
   `Pro_*` / `PIC_*`），走查时按名字一对一核对。
3. **按钮 Caption 带快捷键标记**：`关机(&T)` / `重启(&R)`，不能省略 `(&T)`。

---

## 6. 复现与校验

```bash
# 重新生成布局表（需要 SC 反编译产物，只在开发机上跑）
python tools/dfm_layout.py --src D:/dev/scpt/out/02_forms/dfm --out docs/sc_layout

# 界面模型 vs 布局表逐控件比对（CI 只跑这一条，不依赖 D 盘）
build/bin/Release/osp_unit_tests.exe -tc="ui:*"
```

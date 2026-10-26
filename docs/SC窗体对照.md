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
| 2 | `TSBOX` | `TSbox` | 300×136 | 小弹窗：页签「静默重启」`Rb_Silence`/`Rb_Show`+`Btn_Scrok`；页签「宽带」`Rb_Silence_nc`/`Rb_Show_nc`+`Btn_Ncok` | ⬜ 未做成窗口 |
| 3 | `TSCUBOX` | `TScubox` | 300×136 | 小弹窗：`Lab_pwub` + `Ed_Unpwub`（密码）+ `Btn_Okub` / `Btn_Ubox_Exit` | ⛔ 语言文件里查不到对应文案，用途无法确认 |
| 4 | `TAPPMSGFORM` | `TAppMsgForm` | 480×242 | 通用提示框：`lbTip` / `Lab_little` / `Btn_Ok` / `Btn_Bigok` / `Btn_Exit` / `Btn_Close` | ⬜ 未做成窗口 |
| 5 | `TTLMF` | `TTlmf` | 740×400 | 「任务」对话框：`Cb_Tasktype`（任务类型）+ `Btn_RunPlan1..5`（部署前/中/后/登录时/进桌面）+ 运行计划列表 | ⬜ 未做成窗口（现为页内表格） |
| 6 | `TMFFUN` | `TMFfun` | — | 部署呈现设置的大窗体（`Gb_DM1_Fontset` 等，407 个控件） | 🟡 只做了字段，未按窗体布局 |
| 7 | `TMLSKINRESDM` | — | — | 皮肤资源（2 个控件，无界面语义） | — 不适用 |

---

## 2. 主窗体 `TSCMF` 的 11 个页签

`Page_mf`（800×398）下按 DFM 顺序：

| # | 页签对象 | Caption | 我方 | 说明 |
|---|---|---|---|---|
| 1 | `MlSkinTabSheet1` | 母盘系统 | ✅ | 字段齐全，布局为自动排列（见 §4） |
| 2 | `MlSkinTabSheet2` | 目标系统 | ✅ | 同上 |
| 3 | `MlSkinTabSheet3` | 计划任务 | ✅ | 同上 |
| 4 | `MlSkinTabSheet4` | 部署过程 | ✅ | 同上 |
| 5 | `MlSkinTabSheet5` | 封装过程 | ✅ | 同上 |
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

## 4. 其余五个配置页签：字段齐、版面未对齐

母盘系统 / 目标系统 / 计划任务 / 部署过程 / 封装过程这五页，**控件集合与 Caption
已经逐项来自 DFM 与语言文件**（`docs/SC功能对照.md` 逐条记录），但**摆放方式仍是
引擎的自动纵向排列**，不是 DFM 的绝对坐标。也就是说：内容对，版面不对。

这是与本轮运行/完成页同一类问题的剩余部分，尚未修完。要做的是把每一页的
`Group/Field` 补上 `x/y/width/height`（数据来自已生成的 `docs/sc_layout/TSCMF.json`），
并把 `page.dfmLayout` 打开。工作量集中在 302 个控件的坐标转写与容器（GroupBox /
Panel）嵌套，不需要新逻辑。

---

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

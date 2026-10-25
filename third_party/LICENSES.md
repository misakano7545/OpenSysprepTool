# 第三方依赖登记

本项目以 **GPL-3.0** 分发（见仓库根 `LICENSE`）。下表所有依赖的许可均已确认与 GPL-3.0 兼容，
且全部以**源码/单头文件**形式内置于 `third_party/`，便于满足 GPL-3.0 的源码可得性要求。

| 库 | 版本 | 用途 | 许可 | 本地路径 | 上游 |
|---|---|---|---|---|---|
| doctest | v2.4.11 | 单元测试框架（单头文件） | BSL-1.0 | `third_party/doctest/doctest.h` | https://github.com/doctest/doctest |
| nlohmann/json | v3.11.3 | 配置 JSON 序列化（单头文件） | MIT | `third_party/nlohmann/json.hpp` | https://github.com/nlohmann/json |
| pugixml | v1.14 | unattend.xml 生成（源码编入） | MIT | `third_party/pugixml/` | https://github.com/zeux/pugixml |
| WTL | 10.0（build 10320） | 主界面框架（头文件库，薄封装原生控件） | MS-PL | `third_party/WTL/` | https://sourceforge.net/projects/wtl/（归档取自镜像 https://github.com/Win32-WTL/WTL） |

### WTL 的引入记录

- 归档：`WTL10_10320_Final.zip`，718,708 字节，sha256 `13de5cd7ed41454b8f287e9bcc4525234c6ac8c85905e4d4fe1a0776709fd78f`
- 入库范围：归档里的 `Include/`（20 个头文件）与 `MS-PL.txt`；未入库 `AppWizard/`、`Samples/`、`ReadMe.html`
- 逐文件 sha256 见 `third_party/WTL/MANIFEST.json`，可据此核对「与官方发行逐字节一致」
- **WTL 依赖 ATL**：生成机需装 VS 组件 `Microsoft.VisualStudio.Component.VC.ATL`
  （`atlwin.h` 等）。CI 的 windows-2022 镜像自带该组件。
- 许可为 **MS-PL**（头文件首行与归档内的 `MS-PL.txt` 一致）。
  注意 NuGet 上的 `wtl` 包 nuspec 写的是 CPL-1.0，与头文件自述不符 —— 因此不用它作来源。

## 计划引入（尚未内置）

| 库 | 用途 | 许可 | 引入里程碑 |
|---|---|---|---|
| wil | Win32 RAII 包装（可选） | MIT | 需要时 |

## 禁止引入

- 任何 GPL 不兼容的许可（专有 SDK、带广告条款的库等）
- AGPL 组件（如需混用必须单独评估并记录结论）

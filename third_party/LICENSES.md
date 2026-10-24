# 第三方依赖登记

本项目以 **GPL-3.0** 分发（见仓库根 `LICENSE`）。下表所有依赖的许可均已确认与 GPL-3.0 兼容，
且全部以**源码/单头文件**形式内置于 `third_party/`，便于满足 GPL-3.0 的源码可得性要求。

| 库 | 版本 | 用途 | 许可 | 本地路径 | 上游 |
|---|---|---|---|---|---|
| doctest | v2.4.11 | 单元测试框架（单头文件） | BSL-1.0 | `third_party/doctest/doctest.h` | https://github.com/doctest/doctest |
| nlohmann/json | v3.11.3 | 配置 JSON 序列化（单头文件） | MIT | `third_party/nlohmann/json.hpp` | https://github.com/nlohmann/json |

## 计划引入（尚未内置）

| 库 | 用途 | 许可 | 引入里程碑 |
|---|---|---|---|
| pugixml | unattend.xml 生成 | MIT | M3 |
| WTL 10 | Win32 UI 框架（头文件库） | MS-PL | M7 |
| wil | Win32 RAII 包装（可选） | MIT | 需要时 |

## 禁止引入

- 任何 GPL 不兼容的许可（专有 SDK、带广告条款的库等）
- AGPL 组件（如需混用必须单独评估并记录结论）

# OpenSysprepTool

[![CI](https://github.com/misakano7545/OpenSysprepTool/actions/workflows/ci.yml/badge.svg)](https://github.com/misakano7545/OpenSysprepTool/actions/workflows/ci.yml)

用 C++20 重写的 Windows 系统封装工具，功能对标商业封装工具的完整流程，
**不含任何捆绑、改主页、装扩展、装推广软件等行为**。

> 项目宪章：[`docs/CHARTER.md`](docs/CHARTER.md) —— 任何接手本项目的会话先读这一页。

## 状态

M1（工程骨架与核心库）进行中。当前可交付：CMake 工程、配置模型与 JSON 往返、
文件日志、`--selftest` 自检、CI 流水线。

## 构建

需要 CMake 3.25+ 与 MSVC 2022（含 Windows SDK）。

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

产物：

| 文件 | 说明 |
|---|---|
| `build/bin/Release/ospt.exe` | 主程序 |
| `build/bin/Release/osptd.exe` | 部署运行时 |

## 测试

```bash
ctest --test-dir build -C Release --output-on-failure
build/bin/Release/ospt.exe --selftest
```

`--selftest` 会构造一份完整配置、序列化为 JSON、再反序列化、二次序列化并逐字节比对，
最后打印 `config roundtrip OK`。

## 验收

M1 起每个里程碑都有**两条并列的验收线，缺一不可**：本地跑通不算完成，CI 不绿也不算完成。
本机缺 Windows SDK 时，以 CI 为准。

### 本地验收

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
build/bin/Release/ospt.exe --selftest
```

| 命令 | 通过标准 |
|---|---|
| `cmake -S . -B build` | configure 成功，无 error |
| `cmake --build build --config Release` | 0 error |
| `ctest --test-dir build -C Release` | 全绿。**零用例时 ctest 返回 exit code 8，视为失败**，不用 `--no-tests=ignore` 掩盖 |
| `ospt.exe --selftest` | 打印 `config roundtrip OK` |

### CI 验收

推送后 [CI 工作流](https://github.com/misakano7545/OpenSysprepTool/actions/workflows/ci.yml)
必须在 `windows-2022` runner 上全绿：

| 步骤 | 通过标准 |
|---|---|
| Configure / Build | x64 Release 构建 0 error |
| Test | `ctest` 全绿，禁止 `continue-on-error` |
| Selftest | `ospt.exe --selftest` 打印 `config roundtrip OK` |
| Artifact 内容断言 | 日志打印 `ospt.exe` / `osptd.exe` 的实际字节数与 SHA256；缺失或 0 字节直接失败 |
| 产物上传 | artifact `ospt-x64` 可下载，内含两个 exe |

CI 对同一分支启用了 `concurrency` 组，新推送会取消该分支上仍在跑的旧运行，避免排队堆积。

## 目录

```
core/            静态库 osp_core，两个 exe 共用
  include/core/  公共头
  src/           核心实现
  config/        配置模型 + JSON 序列化
  log/           文件日志
  unattend/      unattend.xml 生成（M3）
  runplan/       运行计划（M4）
  sysinfo/       系统查询（M2）
  regio/         注册表 RAII
  dism/          DISM 封装
  oem/           OEM 信息
host/            主程序 ospt.exe
deploy/          部署运行时 osptd.exe
tests/unit/      单元测试（doctest）
third_party/     第三方单头文件，许可见 third_party/LICENSES.md
docs/            项目宪章等文档
```

## 许可

GPL-3.0，见 [`LICENSE`](LICENSE)。分发二进制须同时提供源码：
<https://github.com/misakano7545/OpenSysprepTool>

## 明确不做

静默安装浏览器扩展、改写主页/默认搜索、破解 Edge Secure Preferences、预置收藏夹、
静默安装推广软件、检测规避安全软件、装内核驱动阻止删除、装机联盟分成。
完整清单与证据位置见宪章第 7 节。

# OpenSysprepTool

[![CI](https://github.com/misakano7545/OpenSysprepTool/actions/workflows/ci.yml/badge.svg)](https://github.com/misakano7545/OpenSysprepTool/actions/workflows/ci.yml)
[![Lint](https://github.com/misakano7545/OpenSysprepTool/actions/workflows/lint.yml/badge.svg)](https://github.com/misakano7545/OpenSysprepTool/actions/workflows/lint.yml)

用 C++20 重写的 Windows 系统封装工具，功能对标商业封装工具的完整流程，
**不含任何捆绑、改主页、装扩展、装推广软件等行为**。

> 项目宪章：[`docs/CHARTER.md`](docs/CHARTER.md) —— 任何接手本项目的会话先读这一页。

## 状态

M1–M3 完成：工程骨架与核心库、系统体检、unattend.xml 生成。
当前可交付：CMake 工程、配置模型与 JSON 往返、文件日志、系统体检、应答文件生成、
CI 与 Lint 流水线。详见 [`CHANGELOG.md`](CHANGELOG.md)。

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

## 命令行

```
ospt --selftest                                         配置 JSON 往返自检
ospt --checkup [--json <path>]                          系统体检
ospt --gen-unattend <cfg.json> --out <unattend.xml>     生成应答文件
ospt --version                                          版本与源码地址
```

`--checkup` 的失败项写入 `warnings` 而不中断，退出码恒为 0；
`--gen-unattend` 失败时退出码非 0 并把原因打到 stderr。

## 测试与验收

M1 起每个里程碑都有**两条并列的验收线，缺一不可**：本地跑通不算完成，CI 不绿也不算完成。

### 本地验收

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure

build/bin/Release/ospt.exe --selftest

build/bin/Release/ospt.exe --checkup --json out.json
python tests/integration/verify_checkup.py out.json

build/bin/Release/ospt.exe --gen-unattend tests/integration/fixtures/m3_cfg.json --out build/m3/unattend.xml
python tests/integration/verify_unattend.py build/m3/unattend.xml
```

| 命令 | 通过标准 |
|---|---|
| `cmake -S . -B build` | configure 成功，无 error |
| `cmake --build build --config Release` | 0 error 0 warning |
| `ctest --test-dir build -C Release` | 全绿。**零用例时 ctest 返回 exit code 8，视为失败**，不用 `--no-tests=ignore` 掩盖 |
| `ospt.exe --selftest` | 打印 `config roundtrip OK` |
| `verify_checkup.py` | 全绿；含「OS 版本与 `cmd /c ver` 一致」「sysprep.exe 真实存在」「分区容量偏差 ≤1%」三条硬证据 |
| `verify_unattend.py` | 全绿；含命名空间、元素顺序、父子嵌套、pass 合法性、计算机名规则、AutoLogon 次数、FirstLogonCommands 条数 |

`verify_unattend.py` 会自动探测与 xml 同目录的 `cfg.json` 作为输入做交叉断言；
找不到时显式打印 SKIP，不静默通过。

### CI 验收

推送后两个工作流都必须在 `windows-2022` runner 上全绿：

[ci.yml](https://github.com/misakano7545/OpenSysprepTool/actions/workflows/ci.yml)：

| 步骤 | 通过标准 |
|---|---|
| Configure / Build | x64 Release 构建 0 error |
| Test | `ctest` 全绿，禁止 `continue-on-error` |
| Selftest | `ospt.exe --selftest` 打印 `config roundtrip OK` |
| Unattend 校验 | fixture 生成 unattend.xml 后跑 116 项断言 |
| Artifact 内容断言 | 日志打印 `ospt.exe` / `osptd.exe` 的实际字节数与 SHA256；缺失或 0 字节直接失败 |
| 产物上传 | artifact `ospt-x64` 可下载，内含两个 exe |

[lint.yml](https://github.com/misakano7545/OpenSysprepTool/actions/workflows/lint.yml)：

| 步骤 | 通过标准 |
|---|---|
| 安装固定版本 clang-format | 版本必须等于工作流里 `CLANG_FORMAT_VERSION`，不符则失败 |
| clang-format 检查 | **只检查本次改动涉及的源文件**（`git diff --name-only`），不要求历史文件重排 |

两个工作流都对同一分支启用了 `concurrency` 组，新推送会取消该分支上仍在跑的旧运行。

### 代码风格

```bash
clang-format -i <files>                                 格式化
python tests/tools/check_whitespace_only.py <base> HEAD 证明某提交只有空白/换行变化
```

`.clang-format` 的 `ColumnLimit` 是 110，`SortIncludes` / `ReflowComments` /
`AllowShort*` / `BinPack*` 全部关闭，避免重排 include、重排注释、把多行压成一行、
行内补对齐空格。纯格式化提交用 `check_whitespace_only.py` 校验，
**不要用 `git diff -w`**：clang-format 会合并或拆分手工折行的表达式，
行数一变 diff 无法对齐，`-w` 会报出不存在的内容差异。

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
tests/integration/  各里程碑的验收校验脚本
tests/tools/     开发工具脚本
third_party/     第三方依赖，许可见 third_party/LICENSES.md
docs/            项目宪章等文档
```

## 许可

GPL-3.0，见 [`LICENSE`](LICENSE)。分发二进制须同时提供源码：
<https://github.com/misakano7545/OpenSysprepTool>

## 明确不做

静默安装浏览器扩展、改写主页/默认搜索、破解 Edge Secure Preferences、预置收藏夹、
静默安装推广软件、检测规避安全软件、装内核驱动阻止删除、装机联盟分成。
完整清单与证据位置见宪章第 7 节。

# Changelog

本文件记录每个发布版本的变化。格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 `0.MINOR.PATCH`（接口稳定后进 1.0.0）。

## [Unreleased]

## [0.1.0] - 待发布

M1 —— 工程骨架与核心库。

### Added

- CMake 3.25+ / MSVC 2022 工程，C++20，静态运行库 `/MT`，产物 `ospt.exe` 与 `osptd.exe`
- `core/config`：配置模型（系统信息、首次进桌面、系统优化、运行计划、部署设置、封装设置）
  与 JSON 往返序列化，缺字段取默认值，解析失败返回错误而不抛异常
- `core/log`：带时间戳与级别前缀的文件日志
- `ospt --selftest`：JSON 往返自检，通过时打印 `config roundtrip OK`
- `ospt --version`：版本号与源码地址（GPL-3.0 要求）
- 单元测试（doctest v2.4.11），含配置往返与错误路径用例
- `.github/workflows/ci.yml`：`windows-2022` 上 configure → build → ctest → selftest，
  并上传 `ospt-x64` artifact

### Notes

- 第三方依赖登记在 `third_party/LICENSES.md`，全部为 MIT / BSL-1.0 单头文件

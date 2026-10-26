#pragma once

namespace osp {

// 三段版本号 —— 发版 tag 的判据。
//
// `.github/workflows/release.yml` 的第一步会拿 tag 去掉前缀 `v` 后的字符串与这三个
// 数字逐段比对，不一致就整条流水线失败、不产出 Release。所以发版顺序是：
//   1. 先把这三个数字改成目标版本并提交到 main；
//   2. 再打 tag（形如 `v<major>.<minor>.<patch>`）并推送。
// 版本号遵循 `0.MINOR.PATCH`，接口稳定后进 1.0.0（见 README 与 CHANGELOG 的开头）。
// versionString() 由这三段常量拼出，不会与它们不一致。
inline constexpr int kVersionMajor = 0;
inline constexpr int kVersionMinor = 1;
inline constexpr int kVersionPatch = 2;

// 形如 "0.1.2"
const char* versionString() noexcept;

} // namespace osp

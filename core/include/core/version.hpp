#pragma once

namespace osp {

inline constexpr int kVersionMajor = 0;
inline constexpr int kVersionMinor = 1;
inline constexpr int kVersionPatch = 0;

// 形如 "0.1.0"
const char* versionString() noexcept;

} // namespace osp

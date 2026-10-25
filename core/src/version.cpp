#include "core/version.hpp"

#include <string>

namespace osp {
namespace {

// 版本字符串由三段常量拼出来，而不是手写死值 —— 手写的话改了常量忘了改字符串，
// `--version`、发布说明与产物名就会各说各话。函数内静态保证只构造一次。
const std::string& cachedVersionString()
{
    static const std::string text = [] {
        return std::to_string(kVersionMajor) + "." + std::to_string(kVersionMinor) + "." +
               std::to_string(kVersionPatch);
    }();
    return text;
}

} // namespace

const char* versionString() noexcept
{
    return cachedVersionString().c_str();
}

} // namespace osp

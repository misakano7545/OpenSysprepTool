#pragma once

// MD5 —— SC 的文件防篡改校验用的就是它
//
// 依据：SC 语言文件里 `Lab_TamperinguseTip` 写的是「部署时会校验被锁定文件的
// **MD5值**，不匹配将弹出提示并退出部署过程」（见 docs/SC功能对照.md 第四节）。
// 所以这里按 RFC 1321 实现，而不是换成 SHA-256 —— 换了就不是 SC 的行为了。
//
// 为什么自己写而不引第三方：算法只有 100 多行，RFC 附了测试向量，
// 引一个 crypto 库反而要单独审许可（宪章 §6）。测试见 tests/unit/test_md5.cpp。

#include <cstdint>
#include <string>

namespace osp::crypto {

// 算内存缓冲区的 MD5，返回 32 位小写十六进制串
std::string md5Hex(const void* data, std::size_t size);

// 算文件内容的 MD5。utf8Path 按 UTF-8 解释；读不到返回空串并把 error 填上。
std::string md5File(const std::string& utf8Path, std::string& error);

} // namespace osp::crypto

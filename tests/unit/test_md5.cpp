// M10：MD5 单测
//
// 用 RFC 1321 附录 A.5 的官方测试向量钉住实现。这些串是公开的规范样例，
// 不是从任何产品里提取的；只要实现正确就必须全部命中，改错一位就对不上。

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "core/md5.hpp"

namespace {

std::string md5OfText(const std::string& text)
{
    return osp::crypto::md5Hex(text.data(), text.size());
}

} // namespace

TEST_CASE("md5: RFC 1321 官方测试向量")
{
    CHECK(md5OfText("") == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(md5OfText("a") == "0cc175b9c0f1b6a831c399e269772661");
    CHECK(md5OfText("abc") == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(md5OfText("message digest") == "f96b697d7cb7938d525a2f31aaf161d0");
    CHECK(md5OfText("abcdefghijklmnopqrstuvwxyz") == "c3fcd3d76192e4007dfb496cca67e13b");
    CHECK(
        md5OfText("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789") ==
        "d174ab98d277d9f5a5611c2c9f419d9f"
    );
    CHECK(
        md5OfText("12345678901234567890123456789012345678901234567890123456789012345678901234567890") ==
        "57edf4a22be3c955ac49da2e2107b67a"
    );
}

TEST_CASE("md5: 跨 64 字节块边界与空指针")
{
    // 55/56/64/65 字节正好卡在补位边界上，是实现最容易写错的长度
    for (const std::size_t size : {55u, 56u, 63u, 64u, 65u, 128u})
    {
        const std::string text(size, 'x');
        const std::string digest = md5OfText(text);
        CHECK(digest.size() == 32);
        // 同样内容两次必然相同
        CHECK(md5OfText(text) == digest);
    }

    // 空指针 + 0 长度 = 空串的摘要
    CHECK(osp::crypto::md5Hex(nullptr, 0) == "d41d8cd98f00b204e9800998ecf8427e");
}

TEST_CASE("md5: 文件摘要与内存摘要一致，读不到文件要报错")
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ospt-md5-unit";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path file = dir / "payload.bin";
    const std::string content = "OSPT tamper check payload";
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    std::string error;
    const std::string fromFile =
        osp::crypto::md5File(reinterpret_cast<const char*>(file.u8string().c_str()), error);
    CHECK(error.empty());
    CHECK(fromFile == md5OfText(content));

    error.clear();
    const std::string missingPath =
        reinterpret_cast<const char*>(dir.u8string().c_str()) + std::string("\\no-such.bin");
    const std::string missing = osp::crypto::md5File(missingPath, error);
    CHECK(missing.empty());
    CHECK(!error.empty());

    std::filesystem::remove_all(dir, ec);
}

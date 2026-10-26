#include "core/md5.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "core/win7.hpp"

namespace osp::crypto {
namespace {

// RFC 1321 §3.4 的 T 表：T[i] = floor(2^32 * abs(sin(i + 1)))
constexpr std::array<std::uint32_t, 64> kTable{
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
    0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu, 0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
    0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau, 0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu, 0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
    0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu, 0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
    0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u, 0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u, 0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u,
};

// 每轮的移位量（RFC 1321 §3.4）
constexpr std::array<int, 64> kShift{
    7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 5,  9,  14, 20, 5,  9,
    14, 20, 5,  9,  14, 20, 5,  9,  14, 20, 4,  11, 16, 23, 4,  11, 16, 23, 4,  11, 16, 23,
    4,  11, 16, 23, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21,
};

inline std::uint32_t rotateLeft(std::uint32_t value, int bits)
{
    return (value << bits) | (value >> (32 - bits));
}

class Md5
{
public:
    void update(const void* data, std::size_t size)
    {
        const auto* bytes = static_cast<const unsigned char*>(data);
        bitCount_ += static_cast<std::uint64_t>(size) * 8u;
        for (std::size_t i = 0; i < size; ++i)
        {
            buffer_[used_++] = bytes[i];
            if (used_ == 64)
            {
                transform();
                used_ = 0;
            }
        }
    }

    std::array<unsigned char, 16> finish()
    {
        // 补位：0x80 后补 0，直到长度 ≡ 56 (mod 64)，最后 8 字节写原始比特长度（小端）
        const std::uint64_t bits = bitCount_;
        std::array<unsigned char, 64> padding{};
        padding[0] = 0x80u;
        const std::size_t padLength = (used_ < 56) ? (56 - used_) : (120 - used_);
        update(padding.data(), padLength);

        std::array<unsigned char, 8> lengthBytes{};
        for (int i = 0; i < 8; ++i)
        {
            lengthBytes[static_cast<std::size_t>(i)] = static_cast<unsigned char>((bits >> (8 * i)) & 0xFFu);
        }
        update(lengthBytes.data(), lengthBytes.size());

        std::array<unsigned char, 16> digest{};
        for (std::size_t i = 0; i < 4; ++i)
        {
            for (int j = 0; j < 4; ++j)
            {
                digest[i * 4 + static_cast<std::size_t>(j)] =
                    static_cast<unsigned char>((state_[i] >> (8 * j)) & 0xFFu);
            }
        }
        return digest;
    }

private:
    void transform()
    {
        std::uint32_t m[16] = {};
        for (int i = 0; i < 16; ++i)
        {
            m[i] = static_cast<std::uint32_t>(buffer_[static_cast<std::size_t>(i) * 4]) |
                (static_cast<std::uint32_t>(buffer_[static_cast<std::size_t>(i) * 4 + 1]) << 8) |
                (static_cast<std::uint32_t>(buffer_[static_cast<std::size_t>(i) * 4 + 2]) << 16) |
                (static_cast<std::uint32_t>(buffer_[static_cast<std::size_t>(i) * 4 + 3]) << 24);
        }

        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];

        for (int i = 0; i < 64; ++i)
        {
            std::uint32_t f = 0;
            int g = 0;
            if (i < 16)
            {
                f = (b & c) | (~b & d);
                g = i;
            }
            else if (i < 32)
            {
                f = (d & b) | (~d & c);
                g = (5 * i + 1) % 16;
            }
            else if (i < 48)
            {
                f = b ^ c ^ d;
                g = (3 * i + 5) % 16;
            }
            else
            {
                f = c ^ (b | ~d);
                g = (7 * i) % 16;
            }

            const std::uint32_t tmp = d;
            d = c;
            c = b;
            b = b +
                rotateLeft(
                    a + f + kTable[static_cast<std::size_t>(i)] + m[g], kShift[static_cast<std::size_t>(i)]
                );
            a = tmp;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
    }

    std::uint32_t state_[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
    std::array<unsigned char, 64> buffer_{};
    std::size_t used_ = 0;
    std::uint64_t bitCount_ = 0;
};

} // namespace

std::string md5Hex(const void* data, std::size_t size)
{
    Md5 md5;
    if (data != nullptr && size > 0)
    {
        md5.update(data, size);
    }
    const std::array<unsigned char, 16> digest = md5.finish();
    char text[33] = {};
    for (std::size_t i = 0; i < digest.size(); ++i)
    {
        std::snprintf(text + i * 2, 3, "%02x", digest[i]);
    }
    return std::string(text, 32);
}

std::string md5File(const std::string& utf8Path, std::string& error)
{
    std::ifstream in(osp::win7::pathFromUtf8(utf8Path), std::ios::binary);
    if (!in)
    {
        error = "打不开文件：" + utf8Path;
        return std::string();
    }
    // 分块读，避免把大文件整个塞进内存（部署包里可能有几百 MB 的镜像）
    Md5 md5;
    std::vector<char> buffer(64 * 1024);
    while (in)
    {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = in.gcount();
        if (got > 0)
        {
            md5.update(buffer.data(), static_cast<std::size_t>(got));
        }
    }
    if (in.bad())
    {
        error = "读文件中途失败：" + utf8Path;
        return std::string();
    }
    const std::array<unsigned char, 16> digest = md5.finish();
    char text[33] = {};
    for (std::size_t i = 0; i < digest.size(); ++i)
    {
        std::snprintf(text + i * 2, 3, "%02x", digest[i]);
    }
    return std::string(text, 32);
}

} // namespace osp::crypto

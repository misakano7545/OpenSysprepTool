// 自动设置 IP（SC 的 Cb_AutoSetip + 三种模式）
//
// SC 的字段来自语言文件：Rb_Setip（指定固定 IP）、Rb_SetRandomip（随机 IP）、
// Lab_ips（IP 段：起始/结束）、Lab_SubntMask / Lab_DefaultGateway /
// Lab_PreferredDNS / Lab_AltenateDNS。本模块把它们翻成 netsh 命令。
//
// 为什么用 netsh 而不是 WMI/COM：与项目其它地方一致（DISM 也是命令行），
// dry-run 能逐字打印将要执行的命令，而且不引入新的 COM 依赖。

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace osp::netcfg {

// 点分十进制 IPv4 -> 32 位整数。格式非法返回 false。
inline bool parseIpv4(const std::string& text, std::uint32_t& out)
{
    std::array<int, 4> parts{};
    int index = 0;
    int value = -1;
    for (const char ch : text)
    {
        if (ch == '.')
        {
            if (index >= 4 || value < 0 || value > 255)
            {
                return false;
            }
            parts[static_cast<std::size_t>(index++)] = value;
            value = -1;
        }
        else if (ch >= '0' && ch <= '9')
        {
            value = (value < 0 ? 0 : value * 10) + (ch - '0');
            if (value > 255)
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }
    if (index != 3 || value < 0)
    {
        return false;
    }
    parts[3] = value;
    out = (static_cast<std::uint32_t>(parts[0]) << 24) | (static_cast<std::uint32_t>(parts[1]) << 16) |
        (static_cast<std::uint32_t>(parts[2]) << 8) | static_cast<std::uint32_t>(parts[3]);
    return true;
}

inline std::string formatIpv4(std::uint32_t value)
{
    char buffer[24] = {};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%u.%u.%u.%u",
        (value >> 24) & 0xFFu,
        (value >> 16) & 0xFFu,
        (value >> 8) & 0xFFu,
        value & 0xFFu
    );
    return buffer;
}

// 在 [start, end] 里取一个地址。seed 为 0 时用真随机；非 0 时同样的 seed 得同样结果，
// 这样单测能钉住行为，而实机部署每次都不一样。
inline std::uint32_t pickInRange(std::uint32_t start, std::uint32_t end, std::uint64_t seed)
{
    if (end < start)
    {
        std::swap(start, end);
    }
    const std::uint64_t span = static_cast<std::uint64_t>(end) - start + 1;
    std::uint64_t offset = 0;
    if (seed == 0)
    {
        std::random_device device;
        std::mt19937_64 engine(device());
        std::uniform_int_distribution<std::uint64_t> dist(0, span - 1);
        offset = dist(engine);
    }
    else
    {
        // splitmix64：不引随机库状态，纯函数式，seed 相同结果必然相同
        std::uint64_t x = seed + 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        x = x ^ (x >> 31);
        offset = x % span;
    }
    return static_cast<std::uint32_t>(start + offset);
}

// 同一 /24 网段内的随机主机地址（跳过 .0 网络号与 .255 广播）
inline std::uint32_t pickSameSubnet24(std::uint32_t current, std::uint64_t seed)
{
    const std::uint32_t network = current & 0xFFFFFF00u;
    std::uint32_t candidate = pickInRange(network + 2, network + 254, seed);
    return candidate;
}

} // namespace osp::netcfg

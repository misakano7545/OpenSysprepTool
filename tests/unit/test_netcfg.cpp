// M10：自动设置 IP 的单元测试
//
// SC 的三种模式（固定 / 随机 / IP 段）最终都落到 netsh 命令，所以这里断言
// 命令行逐字正确，而不是「有没有调用过什么」。随机与网段模式通过 ipRandomSeed
// 固定种子，保证同样输入同样输出。

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/netcfg.hpp"
#include "core/prepare.hpp"

using namespace osp;

namespace {

const osp::prepare::Action* findAction(const osp::prepare::Plan& plan, const std::string& id)
{
    const auto it =
        std::find_if(plan.actions.begin(), plan.actions.end(), [&id](const osp::prepare::Action& a) {
            return a.id == id;
        });
    return it == plan.actions.end() ? nullptr : &*it;
}

bool hasWarning(const osp::prepare::Plan& plan, const std::string& needle)
{
    for (const std::string& warning : plan.warnings)
    {
        if (warning.find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

osp::prepare::Options ipOptions()
{
    osp::prepare::Options options = osp::prepare::defaultOptions();
    options.ipInterface = "以太网"; // 固定网卡名，避免测试依赖本机有几块网卡
    return options;
}

} // namespace

TEST_CASE("netcfg: IPv4 解析与格式化往返")
{
    std::uint32_t value = 0;
    REQUIRE(osp::netcfg::parseIpv4("192.168.50.10", value));
    CHECK(osp::netcfg::formatIpv4(value) == "192.168.50.10");

    CHECK_FALSE(osp::netcfg::parseIpv4("192.168.50", value));
    CHECK_FALSE(osp::netcfg::parseIpv4("192.168.50.256", value));
    CHECK_FALSE(osp::netcfg::parseIpv4("192.168.50.10.1", value));
    CHECK_FALSE(osp::netcfg::parseIpv4("abc", value));
    CHECK_FALSE(osp::netcfg::parseIpv4("", value));
}

TEST_CASE("netcfg: 网段取址在范围内，且同种子同结果")
{
    std::uint32_t start = 0;
    std::uint32_t end = 0;
    REQUIRE(osp::netcfg::parseIpv4("192.168.50.20", start));
    REQUIRE(osp::netcfg::parseIpv4("192.168.50.60", end));

    for (std::uint64_t seed : {1ull, 2ull, 3ull, 12345ull})
    {
        const std::uint32_t picked = osp::netcfg::pickInRange(start, end, seed);
        CHECK(picked >= start);
        CHECK(picked <= end);
        // 同种子必须同结果，否则单测没法钉住行为
        CHECK(osp::netcfg::pickInRange(start, end, seed) == picked);
    }

    // 起止写反也要能用
    std::uint32_t reversed = 0;
    netcfg::parseIpv4("192.168.50.60", reversed);
    const std::uint32_t flipped = osp::netcfg::pickInRange(reversed, start, 7ull);
    CHECK(flipped >= start);
    CHECK(flipped <= reversed);
}

TEST_CASE("netcfg: 同 /24 随机地址跳过网络号与广播")
{
    std::uint32_t current = 0;
    REQUIRE(osp::netcfg::parseIpv4("10.20.30.40", current));
    for (std::uint64_t seed = 1; seed <= 20; ++seed)
    {
        const std::string text = osp::netcfg::formatIpv4(osp::netcfg::pickSameSubnet24(current, seed));
        CHECK(text.rfind("10.20.30.", 0) == 0);
        CHECK(text != "10.20.30.0");
        CHECK(text != "10.20.30.255");
    }
}

TEST_CASE("prepare: 固定 IP 生成 netsh 静态地址与 DNS 命令")
{
    osp::config::Config cfg;
    cfg.optimization.autoSetIp = true;
    cfg.optimization.ipMode = "fixed";
    cfg.optimization.ipAddress = "192.168.50.10";
    cfg.optimization.subnetMask = "255.255.255.0";
    cfg.optimization.gateway = "192.168.50.1";
    cfg.optimization.dnsPreferred = "114.114.114.114";
    cfg.optimization.dnsAlternate = "8.8.8.8";

    const osp::prepare::Plan plan = osp::prepare::build(cfg, ipOptions());
    REQUIRE(plan.ok);

    const osp::prepare::Action* address = findAction(plan, "optimization.autoSetIp");
    REQUIRE(address != nullptr);
    CHECK(address->kind == osp::prepare::ActionKind::RunCommand);
    CHECK(address->program == "netsh");
    CHECK(
        address->arguments ==
        "interface ipv4 set address name=\"以太网\" static 192.168.50.10 255.255.255.0 192.168.50.1"
    );

    const osp::prepare::Action* dns = findAction(plan, "optimization.autoSetIp.dns");
    REQUIRE(dns != nullptr);
    CHECK(dns->arguments == "interface ipv4 set dns name=\"以太网\" static 114.114.114.114");

    const osp::prepare::Action* dnsAlt = findAction(plan, "optimization.autoSetIp.dnsAlt");
    REQUIRE(dnsAlt != nullptr);
    CHECK(dnsAlt->arguments == "interface ipv4 add dns name=\"以太网\" 8.8.8.8 index=2");
}

TEST_CASE("prepare: IP 段模式在范围内取址，同种子可复现")
{
    osp::config::Config cfg;
    cfg.optimization.autoSetIp = true;
    cfg.optimization.ipMode = "range";
    cfg.optimization.ipRangeStart = "192.168.50.20";
    cfg.optimization.ipRangeEnd = "192.168.50.60";
    cfg.optimization.subnetMask = "255.255.255.0";

    osp::prepare::Options options = ipOptions();
    options.ipRandomSeed = 20261026ull;

    const osp::prepare::Plan plan = osp::prepare::build(cfg, options);
    REQUIRE(plan.ok);
    const osp::prepare::Action* address = findAction(plan, "optimization.autoSetIp");
    REQUIRE(address != nullptr);

    // 结果落在段内，且同样的种子必然得到同样的命令行
    CHECK(address->arguments.find("192.168.50.") != std::string::npos);
    const osp::prepare::Plan again = osp::prepare::build(cfg, options);
    const osp::prepare::Action* second = findAction(again, "optimization.autoSetIp");
    REQUIRE(second != nullptr);
    CHECK(second->arguments == address->arguments);
}

TEST_CASE("prepare: 网关不在同一子网时必须告警")
{
    osp::config::Config cfg;
    cfg.optimization.autoSetIp = true;
    cfg.optimization.ipMode = "fixed";
    cfg.optimization.ipAddress = "192.168.10.50";
    cfg.optimization.subnetMask = "255.255.255.0";
    cfg.optimization.gateway = "192.168.50.1"; // 明显不同网段

    const osp::prepare::Plan plan = osp::prepare::build(cfg, ipOptions());
    CHECK(plan.ok);
    // 这种配置执行完会直接断网，不能静默通过
    CHECK(hasWarning(plan, "不在同一子网"));
}

TEST_CASE("prepare: 非法输入只告警不生成动作")
{
    // 非法 IP
    {
        osp::config::Config cfg;
        cfg.optimization.autoSetIp = true;
        cfg.optimization.ipMode = "fixed";
        cfg.optimization.ipAddress = "192.168.50.999";

        const osp::prepare::Plan plan = osp::prepare::build(cfg, ipOptions());
        CHECK(plan.ok);
        CHECK(findAction(plan, "optimization.autoSetIp") == nullptr);
        CHECK(hasWarning(plan, "不是合法 IPv4"));
    }

    // 未知模式
    {
        osp::config::Config cfg;
        cfg.optimization.autoSetIp = true;
        cfg.optimization.ipMode = "no-such-mode";

        const osp::prepare::Plan plan = osp::prepare::build(cfg, ipOptions());
        CHECK(plan.ok);
        CHECK(findAction(plan, "optimization.autoSetIp") == nullptr);
        CHECK(hasWarning(plan, "未知模式"));
    }

    // 网段模式的起止不合法
    {
        osp::config::Config cfg;
        cfg.optimization.autoSetIp = true;
        cfg.optimization.ipMode = "range";
        cfg.optimization.ipRangeStart = "not-an-ip";
        cfg.optimization.ipRangeEnd = "192.168.50.60";

        const osp::prepare::Plan plan = osp::prepare::build(cfg, ipOptions());
        CHECK(plan.ok);
        CHECK(findAction(plan, "optimization.autoSetIp") == nullptr);
        CHECK(hasWarning(plan, "起始/结束 IP 不合法"));
    }
}

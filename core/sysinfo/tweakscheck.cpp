// 体检里的「系统优化项」：逐条比对 SC 优化表里登记的当前值。
//
// 为什么把体检和优化表绑在一起：SC 的「系统设置体检」列的就是这些优化项，
// 一键修复（Btn_OsRepair）修的也是这些。分成两套数据就必然出现
// 「体检查 A、修复改 B」的错位，所以这里直接吃 core/tweaks 的同一张表。

#include "core/sysinfo.hpp"

#include <string>
#include <vector>

#include "core/regio.hpp"
#include "core/tweaks.hpp"

namespace osp::sysinfo {
namespace {

// 把 SC 的值按类型转成可比较的文本，便于给出「当前值 → 期望值」的证据
std::string describeCurrent(const regio::Value& value, bool exists)
{
    if (!exists)
    {
        return "(未设置)";
    }
    switch (value.type)
    {
    case regio::ValueType::Dword:
        return std::to_string(value.dword);
    case regio::ValueType::Qword:
        return std::to_string(value.qword);
    case regio::ValueType::String:
    case regio::ValueType::ExpandString:
        return value.text;
    case regio::ValueType::MultiString: {
        std::string joined;
        for (const std::string& item : value.multi)
        {
            joined += (joined.empty() ? "" : ";") + item;
        }
        return joined;
    }
    case regio::ValueType::Binary: {
        std::string hex;
        for (std::uint8_t byte : value.bytes)
        {
            char buffer[3] = {};
            std::snprintf(buffer, sizeof(buffer), "%02x", byte);
            hex += buffer;
        }
        return hex.empty() ? "(空)" : hex;
    }
    }
    return "(未知类型)";
}

bool matches(const tweaks::Tweak& tweak, const regio::Value& value, bool exists)
{
    if (!exists)
    {
        return false;
    }
    switch (tweak.type)
    {
    case tweaks::ValueType::Integer:
        // SC 的一律是 dword；值不存在或类型不符都算「没优化」
        return value.type == regio::ValueType::Dword && std::to_string(value.dword) == tweak.value;
    case tweaks::ValueType::String:
        return (value.type == regio::ValueType::String || value.type == regio::ValueType::ExpandString) &&
            value.text == tweak.value;
    case tweaks::ValueType::Binary: {
        std::string hex;
        for (std::uint8_t byte : value.bytes)
        {
            char buffer[3] = {};
            std::snprintf(buffer, sizeof(buffer), "%02x", byte);
            hex += buffer;
        }
        return hex == tweak.value;
    }
    }
    return false;
}

} // namespace

// 逐条比对 SC 优化表。命中的项带上 autoFix = 该优化项 id，
// 这样 --repair 能直接把它变成动作，不需要另一套映射表。
void checkTweaks(std::vector<Check>& checks)
{
    for (const tweaks::Tweak& tweak : tweaks::all())
    {
        const regio::Hive hive =
            tweak.hive == tweaks::Hive::CurrentUser ? regio::Hive::CurrentUser : regio::Hive::LocalMachine;
        std::string error;
        regio::Key key = regio::open(hive, tweak.path, false, error);

        Check check;
        check.id = tweak.id;
        check.title = tweak.title;
        check.severity = "info";
        check.resolution = "一键修复可自动设置";
        check.autoFix = tweak.id;

        if (!key.valid())
        {
            // 打不开有两种完全不同的原因，不能混为一谈：
            //   a) 键确实不存在 -> 这项还没设置过，算待修复
            //   b) 键存在但读不了（如 SecurePipeServers\winreg 的 ACL 只给 SYSTEM）
            //      -> 我们**无法判定**，必须如实报「未检查」，不能凭空说「未设置」，
            //         否则一键修复会反复重写一个读不回来的值，看起来永远修不好。
            const bool denied = error.find("拒绝访问") != std::string::npos ||
                error.find("ACCESS_DENIED") != std::string::npos || error.find("5") != std::string::npos;
            if (denied)
            {
                check.detected = false;
                check.evaluated = false;
                check.evidence = "读不到（权限不足）：" + tweak.path + " —— " + error;
                check.message = tweak.title + "：无法判定（需要 SYSTEM 权限才能读取）";
                check.autoFix.clear();
            }
            else
            {
                check.detected = true;
                check.evaluated = true;
                check.evidence = "键不存在：" + tweak.path;
                check.message = tweak.title + "：当前 (未设置)，期望 " + tweak.value;
            }
            checks.push_back(std::move(check));
            continue;
        }

        regio::Value value;
        bool exists = false;
        if (!regio::readValue(key, tweak.name, value, exists, error))
        {
            check.detected = false;
            check.evaluated = false;
            check.evidence = "读不到：" + error;
            checks.push_back(std::move(check));
            continue;
        }

        const bool optimized = matches(tweak, value, exists);
        check.detected = !optimized;
        check.evaluated = true;
        check.message = optimized
            ? tweak.title + "：已优化"
            : tweak.title + "：当前 " + describeCurrent(value, exists) + "，期望 " + tweak.value;
        check.evidence = tweak.path + "\\" + tweak.name;
        checks.push_back(std::move(check));
    }
}

} // namespace osp::sysinfo

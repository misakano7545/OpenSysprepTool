// M10：环境体检扩展的单元测试
//
// 覆盖 SC 的 [Scandata] 三组：系统设置（休眠/Dllcache）、开机启动项（服务/计划任务）、
// 系统插件（BHO/BE/ActiveX）。
//
// 这些函数读的是**本机真实状态**，所以测试只断言「行为契约」而不是具体数值：
// 例如「读不到时必须报未检查，不许报 0 个」，这条正是 CI runner 上会踩的坑。

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/sysinfo.hpp"

using namespace osp;

namespace {

const osp::sysinfo::Check* findCheck(const std::vector<osp::sysinfo::Check>& checks, const std::string& id)
{
    const auto it = std::find_if(checks.begin(), checks.end(), [&id](const osp::sysinfo::Check& c) {
        return c.id == id;
    });
    return it == checks.end() ? nullptr : &*it;
}

} // namespace

TEST_CASE("envcheck: 休眠与 Dllcache 都给出可读结论")
{
    std::vector<osp::sysinfo::Check> checks;
    osp::sysinfo::checkHibernateSpace(checks);
    osp::sysinfo::checkDllCacheSpace(checks);

    const osp::sysinfo::Check* hibernate = findCheck(checks, "hibernateSpace");
    REQUIRE(hibernate != nullptr);
    CHECK(!hibernate->title.empty());
    CHECK(!hibernate->message.empty());
    // 开了休眠就报占用并能一键关掉；没开就说明没占用 —— 两种都不许是空话
    if (hibernate->detected)
    {
        CHECK(hibernate->message.find("占用") != std::string::npos);
        CHECK(hibernate->autoFix == "cmd:powercfg /hibernate off");
    }
    else
    {
        CHECK(hibernate->message.find("未开启") != std::string::npos);
        CHECK(hibernate->autoFix.empty());
    }

    const osp::sysinfo::Check* dllcache = findCheck(checks, "dllCacheSpace");
    REQUIRE(dllcache != nullptr);
    CHECK(!dllcache->message.empty());
    // 清理 DLLcache 是封装期动作，不该在这里号称能一键修掉
    CHECK(dllcache->autoFix.empty());
}

TEST_CASE("envcheck: 插件体检三项齐全，且没有插件时不虚报")
{
    std::vector<osp::sysinfo::Check> checks;
    osp::sysinfo::checkBrowserPlugins(checks);

    for (const char* id : {"browserBho", "browserBe", "browserActiveX"})
    {
        const osp::sysinfo::Check* check = findCheck(checks, id);
        REQUIRE(check != nullptr);
        CHECK(!check->title.empty());
        CHECK(!check->message.empty());
        // 插件要人工判断，不能由一键修复代劳
        CHECK(check->autoFix.empty());
        CHECK(check->evaluated);
    }

    // 报了 detected 就必须有证据（插件名列表），不能只说「发现 N 个」却给不出是谁
    for (const osp::sysinfo::Check& check : checks)
    {
        if (check.detected)
        {
            CHECK(!check.evidence.empty());
            CHECK(check.message.find("发现") != std::string::npos);
        }
        else
        {
            CHECK(check.message.find("没有") != std::string::npos);
        }
    }
}

TEST_CASE("envcheck: 启动项扫描读不到服务时要明说，不许当成 0 个")
{
    std::vector<osp::sysinfo::StartupItem> items;
    const osp::sysinfo::StartupScan scan = osp::sysinfo::fillStartupServices(items);

    // 服务表在任何会话都读得到（HKLM\SYSTEM\...\Services 对用户可读），所以必须可访问
    CHECK(scan.accessible);
    CHECK(scan.count == items.size());
    for (const osp::sysinfo::StartupItem& item : items)
    {
        CHECK(item.scope == "Service");
        CHECK(!item.name.empty());
    }
}

TEST_CASE("envcheck: 计划任务读不到时报无法判定，读到才报数量")
{
    std::vector<osp::sysinfo::StartupItem> items;
    const osp::sysinfo::StartupScan scan = osp::sysinfo::fillStartupTasks(items);

    std::vector<osp::sysinfo::Check> checks;
    osp::sysinfo::checkStartupCoverage(checks, osp::sysinfo::StartupScan{true, 0, ""}, scan);

    const osp::sysinfo::Check* tasks = findCheck(checks, "startupTasks");
    REQUIRE(tasks != nullptr);
    if (!scan.accessible)
    {
        // 非提权会话：System32\Tasks 的 ACL 只给 SYSTEM，必须如实说读不到
        CHECK_FALSE(tasks->evaluated);
        CHECK(tasks->message.find("无法判定") != std::string::npos);
        const bool evidenceSaysPermission = tasks->evidence.find("权限") != std::string::npos ||
            tasks->evidence.find("denied") != std::string::npos;
        CHECK(evidenceSaysPermission);
    }
    else
    {
        CHECK(tasks->evaluated);
        CHECK(tasks->message.find(std::to_string(scan.count)) != std::string::npos);
    }

    // 服务那一栏的可访问性由入参决定，读不到同样不能报 0
    std::vector<osp::sysinfo::Check> denied;
    osp::sysinfo::checkStartupCoverage(denied, osp::sysinfo::StartupScan{false, 0, "拒绝访问"}, scan);
    const osp::sysinfo::Check* services = findCheck(denied, "startupServices");
    REQUIRE(services != nullptr);
    CHECK_FALSE(services->evaluated);
    CHECK(services->message.find("无法判定") != std::string::npos);
}

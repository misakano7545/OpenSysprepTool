// M10：部署前闸门的单元测试（虚拟机检测 / 文件防篡改 / 部署主控校验）
//
// 三项都在「执行任何任务之前」判定，所以测试的重点是**判定边界**：
// 通过必须放行、不通过必须拦住并给出原因，而不是只跑不崩。

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <windows.h>

#include "core/deploy.hpp"
#include "core/md5.hpp"
#include "core/win7.hpp"

using namespace osp;

namespace {

osp::deploy::Plan basePlan()
{
    osp::deploy::Plan plan;
    plan.ok = true;
    return plan;
}

std::filesystem::path makeTempDir()
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ospt-preflight-unit";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string selfMd5()
{
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        return std::string();
    }
    std::string error;
    return osp::crypto::md5File(osp::win7::toUtf8Path(path), error);
}

} // namespace

TEST_CASE("preflight: 没有开启任何校验时直接放行")
{
    const osp::deploy::PreflightResult result = osp::deploy::preflight(basePlan());
    CHECK_FALSE(result.blocked);
    CHECK(result.checked.empty());
}

TEST_CASE("preflight: 开了防篡改但没有锁定文件 = 拒绝")
{
    osp::deploy::Plan plan = basePlan();
    plan.verifyIntegrity = true; // 故意不给 tamperFiles
    const osp::deploy::PreflightResult result = osp::deploy::preflight(plan);
    CHECK(result.blocked);
    CHECK(result.reason.find("锁定文件列表为空") != std::string::npos);
}

TEST_CASE("preflight: 防篡改逐个比对，命中就停并指出是哪个文件")
{
    const std::filesystem::path dir = makeTempDir();
    const std::filesystem::path good = dir / "good.bin";
    const std::filesystem::path bad = dir / "bad.bin";
    {
        std::ofstream out(good, std::ios::binary | std::ios::trunc);
        out << "good payload";
    }
    {
        std::ofstream out(bad, std::ios::binary | std::ios::trunc);
        out << "bad payload";
    }
    const std::string goodPath = osp::win7::toUtf8Path(good);
    const std::string badPath = osp::win7::toUtf8Path(bad);

    std::string error;
    const std::string goodMd5 = osp::crypto::md5File(goodPath, error);
    REQUIRE(goodMd5.size() == 32);

    // 全部匹配：放行，且逐条记录通过项
    {
        osp::deploy::Plan plan = basePlan();
        plan.verifyIntegrity = true;
        plan.tamperFiles.push_back({goodPath, goodMd5});
        const osp::deploy::PreflightResult result = osp::deploy::preflight(plan);
        CHECK_FALSE(result.blocked);
        CHECK(result.checked.size() == 1);
    }

    // 第二个文件不匹配：必须拦住，并点名是哪个文件
    {
        osp::deploy::Plan plan = basePlan();
        plan.verifyIntegrity = true;
        plan.tamperFiles.push_back({goodPath, goodMd5});
        plan.tamperFiles.push_back({badPath, std::string(32, '0')});
        const osp::deploy::PreflightResult result = osp::deploy::preflight(plan);
        CHECK(result.blocked);
        CHECK(result.tamperedFile == badPath);
        CHECK(result.reason.find("被篡改") != std::string::npos);
    }

    // 文件被删掉：也算不通过，且原因说的是读不到而不是篡改
    {
        osp::deploy::Plan plan = basePlan();
        plan.verifyIntegrity = true;
        plan.tamperFiles.push_back({osp::win7::toUtf8Path(dir / "gone.bin"), goodMd5});
        const osp::deploy::PreflightResult result = osp::deploy::preflight(plan);
        CHECK(result.blocked);
        CHECK(result.reason.find("读不到") != std::string::npos);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("preflight: 主控摘要不一致按 hijackMode 决定终止还是继续")
{
    const std::string actual = selfMd5();
    REQUIRE(actual.size() == 32);

    // 摘要一致：放行
    {
        osp::deploy::Plan plan = basePlan();
        plan.controllerMd5 = actual;
        const osp::deploy::PreflightResult result = osp::deploy::preflight(plan);
        CHECK_FALSE(result.blocked);
        CHECK_FALSE(result.controllerHijacked);
        CHECK(!result.checked.empty());
    }

    // 不一致 + terminate：拦住，并把提示原样带出来（SC 的 SA_Lab_ShowTip）
    {
        osp::deploy::Plan plan = basePlan();
        plan.controllerMd5 = std::string(32, 'a');
        plan.hijackMode = "terminate";
        plan.hijackTip = "部署主程序已被替换";
        const osp::deploy::PreflightResult result = osp::deploy::preflight(plan);
        CHECK(result.blocked);
        CHECK(result.controllerHijacked);
        CHECK(result.reason == "部署主程序已被替换");
    }

    // 不一致 + continue：放行但告警
    {
        osp::deploy::Plan plan = basePlan();
        plan.controllerMd5 = std::string(32, 'a');
        plan.hijackMode = "continue";
        plan.hijackTip = "部署主程序已被替换";
        const osp::deploy::PreflightResult result = osp::deploy::preflight(plan);
        CHECK_FALSE(result.blocked);
        CHECK(result.controllerHijacked);
        REQUIRE(result.warnings.size() == 1);
        CHECK(result.warnings[0] == "部署主程序已被替换");
    }

    // 没记摘要（老计划）：跳过校验，不能因此拦住部署
    {
        osp::deploy::Plan plan = basePlan();
        const osp::deploy::PreflightResult result = osp::deploy::preflight(plan);
        CHECK_FALSE(result.blocked);
        CHECK_FALSE(result.controllerHijacked);
    }
}

TEST_CASE("preflight: 虚拟机检测结果与本机环境一致")
{
    osp::deploy::Plan plan = basePlan();
    plan.blockVirtualMachine = true;
    const osp::deploy::PreflightResult result = osp::deploy::preflight(plan);

    // 无法预知跑测试的机器是不是虚拟机，所以断言「判定自洽」：
    // 拦住时必须给出虚拟机原因；放行时必须记录做了检测。
    if (result.blocked)
    {
        CHECK(result.reason.find("虚拟机") != std::string::npos);
        CHECK(result.reason.find("禁止在虚拟机里部署") != std::string::npos);
    }
    else
    {
        REQUIRE(!result.checked.empty());
        CHECK(result.checked[0].find("虚拟机检测") != std::string::npos);
    }
}

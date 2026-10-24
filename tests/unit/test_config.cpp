#include <doctest/doctest.h>

#include <string>

#include "core/config.hpp"

namespace {

osp::config::Config sampleConfig()
{
    osp::config::Config cfg;
    cfg.systemInfo.registeredName = "OpenSysprep";
    cfg.systemInfo.serialNumber = "00000-00000-00000-00000";
    cfg.systemInfo.bootMenuTimeoutSec = 5;

    cfg.firstLogon.resolutionX = 1920;
    cfg.firstLogon.resolutionY = 1080;

    cfg.optimization.pageFileTarget = "D:\\";
    cfg.optimization.networkLocation = "work";

    osp::config::RunPlanTask task;
    task.phase = "desktop";
    task.type = "run";
    task.target = "C:\\Windows\\System32\\notepad.exe";
    task.windowStyle = "normal";
    task.waitMode = "noWait";
    cfg.runPlan.push_back(task);

    cfg.deploy.titleText = "正在部署系统，请稍候…";
    cfg.deploy.skipOobe = true;

    cfg.sysprep.afterAction = "reboot";
    cfg.sysprep.generateNewSid = true;

    return cfg;
}

} // namespace

TEST_CASE("config survives a JSON roundtrip byte for byte")
{
    const osp::config::Config original = sampleConfig();

    const std::string first = osp::config::toJson(original);
    const osp::config::ParseResult parsed = osp::config::fromJson(first);

    REQUIRE(parsed.ok);
    CHECK(osp::config::toJson(parsed.config) == first);
}

TEST_CASE("config fields are preserved across the roundtrip")
{
    const osp::config::Config original = sampleConfig();
    const osp::config::ParseResult parsed = osp::config::fromJson(osp::config::toJson(original));

    REQUIRE(parsed.ok);

    CHECK(parsed.config.schemaVersion == osp::config::kSchemaVersion);
    CHECK(parsed.config.systemInfo.registeredName == "OpenSysprep");
    CHECK(parsed.config.systemInfo.bootMenuTimeoutSec == 5);
    CHECK(parsed.config.firstLogon.resolutionX == 1920);
    CHECK(parsed.config.optimization.networkLocation == "work");
    CHECK(parsed.config.deploy.titleText == "正在部署系统，请稍候…");
    CHECK(parsed.config.sysprep.afterAction == "reboot");
    REQUIRE(parsed.config.runPlan.size() == 1);
    CHECK(parsed.config.runPlan[0].target == "C:\\Windows\\System32\\notepad.exe");
}

TEST_CASE("missing fields fall back to defaults")
{
    const osp::config::ParseResult parsed = osp::config::fromJson("{}");

    REQUIRE(parsed.ok);
    CHECK(parsed.config.schemaVersion == osp::config::kSchemaVersion);
    CHECK(parsed.config.firstLogon.resolutionX == 1024);
    CHECK(parsed.config.firstLogon.colorDepth == 32);
    CHECK(parsed.config.sysprep.generateNewSid == true);
    CHECK(parsed.config.runPlan.empty());
}

TEST_CASE("malformed JSON is reported, not thrown")
{
    const osp::config::ParseResult parsed = osp::config::fromJson("{ this is not json");

    CHECK_FALSE(parsed.ok);
    CHECK_FALSE(parsed.error.empty());
}

TEST_CASE("non-object root is rejected")
{
    const osp::config::ParseResult parsed = osp::config::fromJson("[1, 2, 3]");

    CHECK_FALSE(parsed.ok);
    CHECK(parsed.error == "root element is not a JSON object");
}

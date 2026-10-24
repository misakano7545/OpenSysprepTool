#include <doctest/doctest.h>

#include <cctype>
#include <string>

#include <nlohmann/json.hpp>

#include "core/sysinfo.hpp"

TEST_CASE("checkup report contains every required section")
{
    const osp::sysinfo::CheckupReport report = osp::sysinfo::collect();

    CHECK_FALSE(report.generatedAtUtc.empty());
    CHECK_FALSE(report.hostName.empty());
    CHECK_FALSE(report.os.version.empty());
    CHECK_FALSE(report.os.architecture.empty());
    CHECK_FALSE(report.partitions.empty());
    CHECK(report.requiredFiles.size() == 4);
    CHECK(report.cloneIndicators.size() == 4);
}

TEST_CASE("os version string is dotted numeric with four parts")
{
    const osp::sysinfo::CheckupReport report = osp::sysinfo::collect();

    int dots = 0;
    for (const char c : report.os.version) {
        if (c == '.') {
            ++dots;
        } else {
            CHECK(std::isdigit(static_cast<unsigned char>(c)) != 0);
        }
    }
    CHECK(dots == 3);

    CHECK(report.os.major >= 6); // 最低支持 Windows Vista 之后
}

TEST_CASE("exactly one partition is flagged as the system drive")
{
    const osp::sysinfo::CheckupReport report = osp::sysinfo::collect();

    int systemDrives = 0;
    for (const osp::sysinfo::PartitionInfo& p : report.partitions) {
        CHECK(p.drive.size() == 2);
        CHECK(p.drive[1] == ':');
        if (p.systemDrive) {
            ++systemDrives;
            CHECK(p.totalBytes > 0);
        }
        CHECK(p.usedBytes + p.freeBytes == p.totalBytes);
    }
    CHECK(systemDrives == 1);
}

TEST_CASE("sysprep availability matches the reported path")
{
    const osp::sysinfo::CheckupReport report = osp::sysinfo::collect();

    CHECK_FALSE(report.sysprep.exePath.empty());
    CHECK(report.sysprep.exePath.find("Sysprep") != std::string::npos);
    // available 与 exePath 必须自洽：路径非空即代表探测过该路径
    CHECK(report.sysprep.available == !report.sysprep.exePath.empty());
    CHECK_FALSE(report.sysprep.note.empty());
    // GeneralizationState 单独不能作为"已封装"判据，正常机器上它也是 7
    if (report.sysprep.imageState == "IMAGE_STATE_COMPLETE") {
        CHECK_FALSE(report.sysprep.sysprepPending);
    }
}

TEST_CASE("required file entries carry absolute paths")
{
    const osp::sysinfo::CheckupReport report = osp::sysinfo::collect();

    for (const osp::sysinfo::RequiredFile& f : report.requiredFiles) {
        CHECK_FALSE(f.path.empty());
        // 绝对路径：形如 C:\Windows\...
        CHECK(f.path.size() > 3);
        CHECK(f.path[1] == ':');
        if (f.exists) {
            CHECK(f.sizeBytes > 0);
        }
    }
}

TEST_CASE("json output parses and exposes the documented keys")
{
    const osp::sysinfo::CheckupReport report = osp::sysinfo::collect();
    const std::string text = osp::sysinfo::toJson(report);

    const nlohmann::json parsed = nlohmann::json::parse(text, nullptr, false);
    REQUIRE_FALSE(parsed.is_discarded());

    CHECK(parsed["schemaVersion"].get<int>() == 1);
    CHECK(parsed["os"]["version"].get<std::string>() == report.os.version);
    CHECK(parsed["os"]["architecture"].get<std::string>() == report.os.architecture);
    CHECK(parsed["partitions"].is_array());
    CHECK(parsed["partitions"].size() == report.partitions.size());
    CHECK(parsed["requiredFiles"].is_array());
    CHECK(parsed["cloneIndicators"].is_array());
    CHECK(parsed["antivirus"].contains("queried"));
    CHECK(parsed["antivirus"].contains("products"));
    CHECK(parsed["startupItems"].is_array());
    CHECK(parsed["isLikelyClone"].is_boolean());
    CHECK(parsed["warnings"].is_array());
}

TEST_CASE("summary text mentions every section header")
{
    const osp::sysinfo::CheckupReport report = osp::sysinfo::collect();
    const std::string text = osp::sysinfo::toSummaryText(report);

    CHECK(text.find("[系统]") != std::string::npos);
    CHECK(text.find("[sysprep]") != std::string::npos);
    CHECK(text.find("[分区]") != std::string::npos);
    CHECK(text.find("[必备文件]") != std::string::npos);
    CHECK(text.find("[克隆版检测]") != std::string::npos);
    CHECK(text.find("[杀软]") != std::string::npos);
    CHECK(text.find("[启动项]") != std::string::npos);
}

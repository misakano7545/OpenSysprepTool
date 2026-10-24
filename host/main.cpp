#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/config.hpp"
#include "core/log.hpp"
#include "core/sysinfo.hpp"
#include "core/unattend.hpp"
#include "core/version.hpp"

namespace {

constexpr const char* kRepoUrl = "https://github.com/misakano7545/OpenSysprepTool";

// GPL-3.0 要求分发二进制时提供源码获取方式
void printVersion()
{
    std::printf("ospt %s\n", osp::versionString());
    std::printf("source: %s\n", kRepoUrl);
    std::printf("license: GPL-3.0\n");
}

void printUsage()
{
    std::printf("ospt %s\n\n", osp::versionString());
    std::printf("usage:\n");
    std::printf("  ospt --selftest    配置 JSON 往返自检\n");
    std::printf("  ospt --checkup [--json <path>]  系统体检\n");
    std::printf("  ospt --gen-unattend <cfg.json> --out <unattend.xml>  生成应答文件\n");
    std::printf("  ospt --version     版本与源码地址\n");
}

osp::config::Config makeSampleConfig()
{
    osp::config::Config cfg;
    cfg.schemaVersion = osp::config::kSchemaVersion;

    cfg.systemInfo.registeredName = "OpenSysprep";
    cfg.systemInfo.registeredOrg = "misakano7545";
    cfg.systemInfo.serialNumber = "00000-00000-00000-00000";
    cfg.systemInfo.computerName = "PC-";
    cfg.systemInfo.computerNameMode = "prefixRandom";
    cfg.systemInfo.bootMenuTimeoutSec = 5;

    cfg.firstLogon.runDeviceManager = true;
    cfg.firstLogon.rebootAfterRun = false;
    cfg.firstLogon.resolutionX = 1920;
    cfg.firstLogon.resolutionY = 1080;
    cfg.firstLogon.refreshRate = 60;
    cfg.firstLogon.colorDepth = 32;
    cfg.firstLogon.syncIeHomepage = false;
    cfg.firstLogon.syncIeFavorites = false;

    cfg.optimization.pinTaskbarItems = true;
    cfg.optimization.closeNumLock = true;
    cfg.optimization.enableHibernate = false;
    cfg.optimization.closeRebootDialog = true;
    cfg.optimization.myDocumentsTarget = "";
    cfg.optimization.pageFileTarget = "D:\\";
    cfg.optimization.autoSetIp = false;
    cfg.optimization.ipMode = "fixed";
    cfg.optimization.networkLocation = "work";

    osp::config::RunPlanTask t1;
    t1.phase = "desktop";
    t1.type = "run";
    t1.target = "C:\\Windows\\System32\\notepad.exe";
    t1.windowStyle = "normal";
    t1.waitMode = "noWait";
    t1.cleanup = "keep";
    t1.searchScope = "systemPath";

    osp::config::RunPlanTask t2;
    t2.phase = "postinstall";
    t2.type = "reg";
    t2.target = "C:\\drivers\\oem.reg";
    t2.windowStyle = "hidden";
    t2.waitMode = "wait";
    t2.waitSeconds = 30;
    t2.cleanup = "deleteFile";
    t2.searchScope = "allDrives";

    cfg.runPlan.push_back(t1);
    cfg.runPlan.push_back(t2);

    cfg.deploy.module = "trueTrans";
    cfg.deploy.background = "systemDefault";
    cfg.deploy.titleText = "正在部署系统，请稍候…";
    cfg.deploy.skipOobe = true;
    cfg.deploy.autoCreateUser = true;
    cfg.deploy.userName = "User";
    cfg.deploy.userGroup = "Administrators";
    cfg.deploy.blockVirtualMachine = false;

    cfg.sysprep.mode = "classic";
    cfg.sysprep.generateNewSid = true;
    cfg.sysprep.uninstallDrivers = true;
    cfg.sysprep.loadSrsDrivers = false;
    cfg.sysprep.transferDesktopFiles = false;
    cfg.sysprep.cleanTemp = true;
    cfg.sysprep.cleanDllCache = true;
    cfg.sysprep.autoLogon = true;
    cfg.sysprep.autoLogonCount = 1;
    cfg.sysprep.workgroup = "WORKGROUP";
    cfg.sysprep.domain = "";
    cfg.sysprep.domainUser = "";
    cfg.sysprep.afterAction = "reboot";

    return cfg;
}

std::string toUtf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.c_str()), u8.size());
}

int runSelftest()
{
    const std::filesystem::path logPath =
        std::filesystem::temp_directory_path() / "ospt-selftest.log";

    if (!osp::log::openFile(toUtf8(logPath)))
    {
        std::printf("selftest FAILED: cannot open log file: %s\n", osp::log::lastError().c_str());
        return 1;
    }
    osp::log::info("selftest started");

    const osp::config::Config original = makeSampleConfig();
    const std::string first = osp::config::toJson(original);
    osp::log::info("config serialized, " + std::to_string(first.size()) + " bytes");

    const osp::config::ParseResult parsed = osp::config::fromJson(first);
    if (!parsed.ok)
    {
        osp::log::error("parse failed: " + parsed.error);
        std::printf("selftest FAILED: %s\n", parsed.error.c_str());
        osp::log::close();
        return 1;
    }

    const std::string second = osp::config::toJson(parsed.config);
    if (first != second)
    {
        osp::log::error("roundtrip mismatch");
        std::printf("selftest FAILED: roundtrip mismatch\n");
        osp::log::close();
        return 1;
    }

    if (parsed.config.runPlan.size() != original.runPlan.size())
    {
        std::printf("selftest FAILED: runPlan size mismatch\n");
        osp::log::close();
        return 1;
    }

    if (parsed.config.systemInfo.registeredName != original.systemInfo.registeredName ||
        parsed.config.sysprep.afterAction != original.sysprep.afterAction ||
        parsed.config.deploy.titleText != original.deploy.titleText)
    {
        std::printf("selftest FAILED: field mismatch after roundtrip\n");
        osp::log::close();
        return 1;
    }

    osp::log::info("selftest finished, roundtrip verified");
    osp::log::close();

    std::printf("log: %s\n", toUtf8(logPath).c_str());
    std::printf("config roundtrip OK\n");
    return 0;
}

// M2：系统体检。--json <path> 时额外落盘 JSON，退出码恒为 0（失败项写进 warnings）。
int runCheckup(int argc, char** argv)
{
    const char* jsonPath = nullptr;

    for (int i = 2; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--json") == 0)
        {
            if (i + 1 >= argc)
            {
                std::printf("--json requires an output path\n");
                return 2;
            }
            jsonPath = argv[++i];
        }
        else
        {
            std::printf("unknown option: %s\n\n", argv[i]);
            printUsage();
            return 2;
        }
    }

    const osp::sysinfo::CheckupReport report = osp::sysinfo::collect();

    if (jsonPath != nullptr)
    {
        const std::string text = osp::sysinfo::toJson(report);
        std::ofstream out(std::filesystem::path(jsonPath), std::ios::binary | std::ios::trunc);
        if (!out)
        {
            std::printf("cannot open for writing: %s\n", jsonPath);
            return 1;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.put('\n');
        out.close();
        if (!out)
        {
            std::printf("write failed: %s\n", jsonPath);
            return 1;
        }
        std::printf("checkup json: %s\n", jsonPath);
    }

    std::printf("%s", osp::sysinfo::toSummaryText(report).c_str());
    return 0;
}

// M3：生成 unattend.xml
// 用法: ospt --gen-unattend <cfg.json> --out <unattend.xml>
int runGenUnattend(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("--gen-unattend requires a config path\n\n");
        printUsage();
        return 2;
    }

    const char* configPath = argv[2];
    const char* outPath = nullptr;

    for (int i = 3; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--out") == 0)
        {
            if (i + 1 >= argc)
            {
                std::printf("--out requires a path\n");
                return 2;
            }
            outPath = argv[++i];
        }
        else
        {
            std::printf("unknown option: %s\n\n", argv[i]);
            printUsage();
            return 2;
        }
    }

    if (outPath == nullptr)
    {
        std::printf("--gen-unattend requires --out <unattend.xml>\n\n");
        printUsage();
        return 2;
    }

    std::ifstream in(std::filesystem::path(configPath), std::ios::binary);
    if (!in)
    {
        std::printf("cannot open config: %s\n", configPath);
        return 1;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    const osp::config::ParseResult parsed = osp::config::fromJson(text);
    if (!parsed.ok)
    {
        std::printf("config parse failed: %s\n", parsed.error.c_str());
        return 1;
    }

    const osp::unattend::GenResult generated = osp::unattend::generate(parsed.config);
    if (!generated.ok)
    {
        std::printf("unattend generation failed: %s\n", generated.error.c_str());
        return 1;
    }

    std::string error;
    if (!osp::unattend::writeToFile(outPath, generated.xml, error))
    {
        std::printf("%s\n", error.c_str());
        return 1;
    }

    std::printf("unattend: %s (%zu bytes)\n", outPath, generated.xml.size());
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printUsage();
        return 0;
    }

    const char* arg = argv[1];

    if (std::strcmp(arg, "--selftest") == 0)
    {
        return runSelftest();
    }

    if (std::strcmp(arg, "--checkup") == 0)
    {
        return runCheckup(argc, argv);
    }

    if (std::strcmp(arg, "--gen-unattend") == 0)
    {
        return runGenUnattend(argc, argv);
    }

    if (std::strcmp(arg, "--version") == 0)
    {
        printVersion();
        return 0;
    }

    std::printf("unknown option: %s\n\n", arg);
    printUsage();
    return 2;
}

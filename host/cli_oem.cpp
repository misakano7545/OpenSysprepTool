#include "core/oem.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/config.hpp"
#include "core/win7.hpp"

namespace osp::host {
namespace {

void printOemUsage()
{
    std::printf("  ospt --oem-detect [--skin <品牌目录>] [--lang CHS|EN] [--json <path>]\n");
    std::printf("       读本机 BIOS 的厂商/型号；给了 --skin 就按 SC 的品牌目录布局匹配档案\n");
    std::printf("  ospt --import-oem <nt6.ini> [--lang CHS|EN] [--into <cfg.json>] [--out <path>]\n");
    std::printf("       把 SC 的 OEM 档案并进配置；不给 --into 就只打印解析结果\n");
    std::printf("\n");
}

std::string profileToJsonText(const osp::oem::Profile& p, const std::string& indent = std::string())
{
    nlohmann::json j;
    j["manufacturer"] = p.manufacturer;
    j["model"] = p.model;
    j["supportHours"] = p.supportHours;
    j["supportPhone"] = p.supportPhone;
    j["supportProvider"] = p.supportProvider;
    j["supportUrl"] = p.supportUrl;
    j["source"] = p.source;
    return j.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string biosToJsonText(const osp::oem::BiosInfo& bios)
{
    nlohmann::json j;
    j["ok"] = bios.ok;
    j["manufacturer"] = bios.manufacturer;
    j["productName"] = bios.productName;
    j["version"] = bios.version;
    j["family"] = bios.family;
    j["biosVendor"] = bios.biosVendor;
    j["biosVersion"] = bios.biosVersion;
    j["error"] = bios.error;
    return j.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
}

bool writeText(const std::string& utf8Path, const std::string& text, std::string& error)
{
    std::ofstream out(osp::win7::pathFromUtf8(utf8Path), std::ios::binary | std::ios::trunc);
    if (!out)
    {
        error = "打不开写入目标：" + utf8Path;
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.put('\n');
    out.close();
    if (!out)
    {
        error = "写入失败：" + utf8Path;
        return false;
    }
    return true;
}

void printProfile(const osp::oem::Profile& p)
{
    const auto line = [](const char* label, const std::string& value) {
        if (!value.empty())
        {
            std::printf("  %-16s %s\n", label, value.c_str());
        }
    };
    line("厂商（Manufacturer）", p.manufacturer);
    line("型号（Model）", p.model);
    line("支持时段", p.supportHours);
    line("支持电话", p.supportPhone);
    line("支持入口名", p.supportProvider);
    line("支持网址", p.supportUrl);
    line("来源", p.source);
}

} // namespace

int runOemDetect(int argc, char** argv)
{
    std::string skinDir;
    std::string language = "CHS";
    std::string jsonPath;
    bool wantJsonOnly = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const auto value = [&i, argc, argv](const char* name) -> const char* {
            if (i + 1 >= argc)
            {
                std::printf("%s 需要一个值\n", name);
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--oem-detect")
        {
            continue;
        }
        if (arg == "--skin")
        {
            const char* v = value("--skin");
            if (v == nullptr)
            {
                return 2;
            }
            skinDir = v;
        }
        else if (arg == "--lang")
        {
            const char* v = value("--lang");
            if (v == nullptr)
            {
                return 2;
            }
            language = v;
        }
        else if (arg == "--json")
        {
            const char* v = value("--json");
            if (v == nullptr)
            {
                return 2;
            }
            jsonPath = v;
        }
        else if (arg == "--json-only")
        {
            // 只输出 JSON，便于脚本消费；仍然写 --json 指定的文件
            wantJsonOnly = true;
        }
        else
        {
            std::printf("unknown option: %s\n\n", arg.c_str());
            printOemUsage();
            return 2;
        }
    }

    const osp::oem::BiosInfo bios = osp::oem::readBios();

    nlohmann::json root;
    root["bios"] = nlohmann::json::parse(biosToJsonText(bios));

    osp::oem::Profile profile = osp::oem::fromBios(bios);
    if (!skinDir.empty())
    {
        const osp::oem::DetectResult detected = osp::oem::detectFromSkinDir(skinDir, bios, language);
        root["detected"]["ok"] = detected.ok;
        root["detected"]["brand"] = detected.brand;
        root["detected"]["iniPath"] = detected.iniPath;
        root["detected"]["error"] = detected.error;
        root["detected"]["tried"] = detected.tried;
        if (detected.ok)
        {
            profile = detected.profile;
        }
    }
    root["profile"] = nlohmann::json::parse(profileToJsonText(profile));

    std::string error;
    if (!jsonPath.empty())
    {
        if (!writeText(jsonPath, root.dump(2), error))
        {
            std::printf("%s\n", error.c_str());
            std::printf("RESULT: FAILED\n");
            return 1;
        }
    }

    if (wantJsonOnly)
    {
        std::printf("%s\n", root.dump(2).c_str());
        std::printf("RESULT: OK\n");
        return 0;
    }

    std::printf("=== OEM 识别 ===\n");
    if (bios.ok)
    {
        std::printf("  BIOS 厂商        %s\n", bios.manufacturer.c_str());
        std::printf("  BIOS 型号        %s\n", bios.productName.c_str());
        if (!bios.biosVendor.empty())
        {
            std::printf("  BIOS 版本        %s %s\n", bios.biosVendor.c_str(), bios.biosVersion.c_str());
        }
    }
    else
    {
        std::printf("  %s\n", bios.error.c_str());
    }
    std::printf("\n");
    std::printf("=== OEM 档案 ===\n");
    printProfile(profile);
    if (!jsonPath.empty())
    {
        std::printf("  （已写入 %s）\n", jsonPath.c_str());
    }
    std::printf("RESULT: OK\n");
    return 0;
}

int runImportOem(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("--import-oem 需要一个 ini 路径\n\n");
        printOemUsage();
        return 2;
    }

    const std::string iniPath = argv[1];
    std::string language = "CHS";
    std::string intoPath;
    std::string outPath;

    for (int i = 2; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const auto value = [&i, argc, argv](const char* name) -> const char* {
            if (i + 1 >= argc)
            {
                std::printf("%s 需要一个值\n", name);
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--lang")
        {
            const char* v = value("--lang");
            if (v == nullptr)
            {
                return 2;
            }
            language = v;
        }
        else if (arg == "--into")
        {
            const char* v = value("--into");
            if (v == nullptr)
            {
                return 2;
            }
            intoPath = v;
        }
        else if (arg == "--out")
        {
            const char* v = value("--out");
            if (v == nullptr)
            {
                return 2;
            }
            outPath = v;
        }
        else
        {
            std::printf("unknown option: %s\n\n", arg.c_str());
            printOemUsage();
            return 2;
        }
    }

    const osp::oem::LoadResult loaded = osp::oem::loadIni(iniPath, language);
    for (const std::string& warning : loaded.warnings)
    {
        std::printf("warning: %s\n", warning.c_str());
    }
    if (!loaded.ok)
    {
        std::printf("%s\n", loaded.error.c_str());
        std::printf("RESULT: FAILED\n");
        return 1;
    }

    std::printf("=== 解析 %s ===\n", iniPath.c_str());
    printProfile(loaded.profile);

    if (intoPath.empty())
    {
        std::printf("\n没有给 --into，只做解析（要合并进配置请加 --into <cfg.json>）\n");
        std::printf("RESULT: OK\n");
        return 0;
    }

    std::string text;
    std::string error;
    {
        std::ifstream in(osp::win7::pathFromUtf8(intoPath), std::ios::binary);
        if (!in)
        {
            std::printf("打不开配置文件：%s\n", intoPath.c_str());
            std::printf("RESULT: FAILED\n");
            return 1;
        }
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const osp::config::ParseResult parsed = osp::config::fromJson(text);
    if (!parsed.ok)
    {
        std::printf("配置解析失败：%s\n", parsed.error.c_str());
        std::printf("RESULT: FAILED\n");
        return 1;
    }

    osp::config::Config cfg = parsed.config;
    cfg.oem.manufacturer = loaded.profile.manufacturer;
    cfg.oem.model = loaded.profile.model;
    cfg.oem.supportHours = loaded.profile.supportHours;
    cfg.oem.supportPhone = loaded.profile.supportPhone;
    cfg.oem.supportProvider = loaded.profile.supportProvider;
    cfg.oem.supportUrl = loaded.profile.supportUrl;
    cfg.oem.source = loaded.profile.source;

    const std::string target = outPath.empty() ? intoPath : outPath;
    if (!writeText(target, osp::config::toJson(cfg), error))
    {
        std::printf("%s\n", error.c_str());
        std::printf("RESULT: FAILED\n");
        return 1;
    }
    std::printf("\n已写入 %s 的 oem 段（来源：%s）\n", target.c_str(), loaded.profile.source.c_str());
    std::printf("RESULT: OK\n");
    return 0;
}

} // namespace osp::host

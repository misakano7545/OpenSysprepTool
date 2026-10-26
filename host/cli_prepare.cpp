#include "core/config.hpp"
#include "core/prepare.hpp"
#include "core/win7.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace osp::host {
namespace {

bool readText(const std::string& path, std::string& text, std::string& error)
{
    std::ifstream in(osp::win7::pathFromUtf8(path), std::ios::binary);
    if (!in)
    {
        error = "打不开文件：" + path;
        return false;
    }
    text.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    if (text.empty())
    {
        error = "文件是空的：" + path;
        return false;
    }
    return true;
}

bool writeText(const std::string& path, const std::string& text, std::string& error)
{
    const std::filesystem::path target(path);
    if (target.has_parent_path())
    {
        std::error_code ec;
        std::filesystem::create_directories(target.parent_path(), ec);
    }
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        error = "打不开写入目标：" + path;
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
    if (!out)
    {
        error = "写入失败：" + path;
        return false;
    }
    return true;
}

std::string entrySummary(const osp::prepare::JournalEntry& entry)
{
    if (entry.kind == "registry")
    {
        return "registry  " + entry.hive + "\\" + entry.key + "  \"" + entry.valueName + "\"  " +
            (entry.existed ? "原值存在" : "原值不存在");
    }
    if (entry.kind == "move")
    {
        return "move      " + entry.from + " → " + entry.to;
    }
    return entry.kind + "  " + entry.path;
}

} // namespace

int runPrepareCommandLine(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("--prepare 需要一个配置文件路径\n\n");
        return 2;
    }

    const char* configPath = argv[1];
    bool dryRun = false;
    bool execute = false;
    std::string reportPath;
    std::string journalPath;

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

        if (arg == "--dry-run")
        {
            dryRun = true;
        }
        else if (arg == "--execute")
        {
            execute = true;
        }
        else if (arg == "--report")
        {
            const char* path = value("--report");
            if (path == nullptr)
            {
                return 2;
            }
            reportPath = path;
        }
        else if (arg == "--journal")
        {
            const char* path = value("--journal");
            if (path == nullptr)
            {
                return 2;
            }
            journalPath = path;
        }
        else
        {
            std::printf("unknown option: %s\n", arg.c_str());
            return 2;
        }
    }

    std::string text;
    std::string error;
    if (!readText(configPath, text, error))
    {
        std::printf("%s\n", error.c_str());
        return 1;
    }

    const osp::config::ParseResult parsed = osp::config::fromJson(text);
    if (!parsed.ok)
    {
        std::printf("配置解析失败：%s\n", parsed.error.c_str());
        return 1;
    }

    const osp::prepare::Plan plan = osp::prepare::build(parsed.config, osp::prepare::defaultOptions());
    if (!plan.ok)
    {
        std::printf("%s\n", plan.error.c_str());
        return 1;
    }

    std::fputs(osp::prepare::dryRunReport(plan).c_str(), stdout);

    if (!reportPath.empty())
    {
        if (!writeText(reportPath, osp::prepare::toJson(plan), error))
        {
            std::printf("%s\n", error.c_str());
            return 1;
        }
        std::printf("report: %s\n", reportPath.c_str());
    }

    if (dryRun)
    {
        return 0;
    }
    if (!execute)
    {
        std::printf("\n真实执行会改注册表、移动文件并运行系统命令，必须显式加 --execute；"
                    "只想看将做什么请加 --dry-run\n");
        return 2;
    }
    if (!plan.blockers.empty())
    {
        std::printf("\n存在阻塞项，拒绝执行：\n");
        for (const std::string& blocker : plan.blockers)
        {
            std::printf("  - %s\n", blocker.c_str());
        }
        return 1;
    }

    const osp::prepare::ExecResult result = osp::prepare::execute(plan);
    for (const std::string& line : result.log)
    {
        std::printf("  %s\n", line.c_str());
    }
    for (const std::string& warning : result.warnings)
    {
        std::printf("warning: %s\n", warning.c_str());
    }

    if (journalPath.empty())
    {
        journalPath = "ospt-prepare-journal.json";
    }
    if (!writeText(journalPath, osp::prepare::journalToJson(result.journal), error))
    {
        std::printf("%s\n", error.c_str());
        std::printf("RESULT: FAILED\n");
        return 1;
    }
    std::printf("journal: %s（%zu 条回滚记录）\n", journalPath.c_str(), result.journal.entries.size());

    if (!result.ok)
    {
        std::printf("%s\n", result.error.c_str());
        std::printf("RESULT: FAILED\n");
        return 1;
    }
    std::printf("RESULT: OK  （%zu 个动作已执行）\n", result.applied);
    return 0;
}

int runRollbackCommandLine(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("--rollback 需要一个回滚日志路径\n\n");
        return 2;
    }

    const char* journalPath = argv[1];
    bool dryRun = false;
    bool execute = false;

    for (int i = 2; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--dry-run")
        {
            dryRun = true;
        }
        else if (arg == "--execute")
        {
            execute = true;
        }
        else
        {
            std::printf("unknown option: %s\n", arg.c_str());
            return 2;
        }
    }

    std::string text;
    std::string error;
    if (!readText(journalPath, text, error))
    {
        std::printf("%s\n", error.c_str());
        return 1;
    }

    osp::prepare::Journal journal;
    if (!osp::prepare::journalFromJson(text, journal, error))
    {
        std::printf("回滚日志解析失败：%s\n", error.c_str());
        return 1;
    }

    std::printf("回滚日志：%zu 条记录（%s）\n", journal.entries.size(), journal.createdAt.c_str());
    for (const osp::prepare::JournalEntry& entry : journal.entries)
    {
        std::printf("  %s\n", entrySummary(entry).c_str());
    }

    if (dryRun)
    {
        const osp::prepare::ExecResult preview = osp::prepare::rollback(journal, true);
        for (const std::string& line : preview.log)
        {
            std::printf("  %s\n", line.c_str());
        }
        std::printf("RESULT: DRY-RUN OK\n");
        return 0;
    }
    if (!execute)
    {
        std::printf("\n回滚会改注册表并移动文件，必须显式加 --execute；只想看将做什么请加 --dry-run\n");
        return 2;
    }

    const osp::prepare::ExecResult result = osp::prepare::rollback(journal, false);
    for (const std::string& line : result.log)
    {
        std::printf("  %s\n", line.c_str());
    }
    for (const std::string& warning : result.warnings)
    {
        std::printf("warning: %s\n", warning.c_str());
    }
    if (!result.ok)
    {
        std::printf("%s\n", result.error.c_str());
        std::printf("RESULT: FAILED\n");
        return 1;
    }
    std::printf("RESULT: OK  （%zu 条已回滚）\n", result.applied);
    return 0;
}

int runPrepareSelfTest()
{
    std::string report;
    std::string error;
    if (!osp::prepare::selfTest(report, error))
    {
        std::printf("prepare selftest FAILED: %s\n", error.c_str());
        return 1;
    }
    std::printf("%s\n", report.c_str());
    return 0;
}

} // namespace osp::host

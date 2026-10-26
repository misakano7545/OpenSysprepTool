// 一键修复（SC 的 Btn_OsRepair / Btn_Execute）
//
// SC 的做法：体检 → 列出可修项 → 「立即处理」→ 「优化完成，已修复 N 个问题」。
// 这里照同一条链路做，但**只修体检明确标了 autoFix 的项**：
// 需要人工处理的（杀软、克隆版、BitLocker、脏位……）一律不碰，绝不假装能修。
//
// 修复动作本身复用 core/prepare：优化项 → RegistrySet 动作 → journal → 可回滚。
// 这样「一键修复」和「封装前设置」走的是同一套执行器，不会出现两套行为。

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "core/config.hpp"
#include "core/prepare.hpp"
#include "core/sysinfo.hpp"
#include "core/win7.hpp"

namespace osp::host {
namespace {

void printRepairUsage()
{
    std::printf("  ospt --repair [--dry-run|--execute] [--journal <json>] [--report <json>]\n");
    std::printf("       体检并修复可自动处理的优化项（等同 SC 的『一键修复』）\n");
}

} // namespace

int runRepair(int argc, char** argv)
{
    bool dryRun = false;
    bool execute = false;
    std::string journalPath;
    std::string reportPath;

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
        if (arg == "--repair" && i == 1)
        {
            continue;
        }
        if (arg == "--dry-run")
        {
            dryRun = true;
        }
        else if (arg == "--execute")
        {
            execute = true;
        }
        else if (arg == "--journal")
        {
            const char* v = value("--journal");
            if (v == nullptr)
            {
                return 2;
            }
            journalPath = v;
        }
        else if (arg == "--report")
        {
            const char* v = value("--report");
            if (v == nullptr)
            {
                return 2;
            }
            reportPath = v;
        }
        else
        {
            std::printf("unknown option: %s\n\n", arg.c_str());
            printRepairUsage();
            return 2;
        }
    }

    // 第 1 步：体检（只跑优化项那一段，避免整份采集的开销）
    std::vector<osp::sysinfo::Check> checks;
    osp::sysinfo::checkTweaks(checks);

    std::vector<std::string> fixable;
    std::vector<const osp::sysinfo::Check*> manual;
    std::vector<const osp::sysinfo::Check*> unknown;
    int alreadyOk = 0;
    for (const osp::sysinfo::Check& check : checks)
    {
        if (!check.evaluated)
        {
            // 读不到 / 查不了：既不算已优化，也不算待修复，单独列出来
            unknown.push_back(&check);
            continue;
        }
        if (!check.detected)
        {
            ++alreadyOk;
            continue;
        }
        if (!check.autoFix.empty())
        {
            fixable.push_back(check.autoFix);
        }
        else
        {
            manual.push_back(&check);
        }
    }

    std::printf("=== 系统体检（优化项）===\n");
    std::printf(
        "  共 %zu 项：已优化 %d 项，待修复 %zu 项，无法判定 %zu 项\n",
        checks.size(),
        alreadyOk,
        fixable.size() + manual.size(),
        unknown.size()
    );
    for (const osp::sysinfo::Check& check : checks)
    {
        if (check.detected)
        {
            std::printf("  [待修] %s\n", check.message.c_str());
        }
    }
    if (!unknown.empty())
    {
        std::printf("\n以下 %zu 项本次无法判定（不影响修复其余项）：\n", unknown.size());
        for (const osp::sysinfo::Check* check : unknown)
        {
            std::printf("  [未知] %s —— %s\n", check->title.c_str(), check->evidence.c_str());
        }
    }

    if (!manual.empty())
    {
        std::printf("\n以下 %zu 项需要人工处理，一键修复不会碰：\n", manual.size());
        for (const osp::sysinfo::Check* check : manual)
        {
            std::printf("  [人工] %s —— %s\n", check->title.c_str(), check->resolution.c_str());
        }
    }

    if (!fixable.empty())
    {
        std::printf("\n以下 %zu 项可以自动修复：\n", fixable.size());
        for (const std::string& id : fixable)
        {
            std::printf("  [可修] %s\n", id.c_str());
        }
    }

    if (fixable.empty())
    {
        std::printf("\n恭喜你，体检各项指标正常\n");
        std::printf("RESULT: OK（无需修复）\n");
        return 0;
    }

    // 第 2 步：把可修项翻成 prepare 的动作
    osp::config::Config cfg;
    cfg.optimization.sysTweaks = fixable;
    osp::prepare::Plan plan = osp::prepare::build(cfg, osp::prepare::defaultOptions());
    if (!plan.ok)
    {
        std::printf("\n%s\n", plan.error.c_str());
        std::printf("RESULT: FAILED\n");
        return 1;
    }

    // 只留优化项动作：默认配置里的 bootMenuTimeoutSec 也会生成一个 BcdTimeout 动作，
    // 那是「封装前设置」的事，不该被「一键修复」顺手改掉。白名单过滤，绝不靠默认值。
    plan.actions.erase(
        std::remove_if(
            plan.actions.begin(),
            plan.actions.end(),
            [](const osp::prepare::Action& action) {
                return action.sourceField != "optimization.sysTweaks";
            }
        ),
        plan.actions.end()
    );
    if (plan.actions.empty())
    {
        std::printf("\n没有任何可自动修复的动作\n");
        std::printf("RESULT: OK（无需修复）\n");
        return 0;
    }

    std::fputs(osp::prepare::dryRunReport(plan).c_str(), stdout);

    if (!execute)
    {
        std::printf("\n以上为预览：没有改注册表。真实修复会写 HKLM/HKCU 并可能需要重启，"
                    "确认后加 --execute\n");
        std::printf("RESULT: DRY-RUN OK\n");
        return 0;
    }
    if (dryRun)
    {
        std::printf("\n--dry-run 与 --execute 不能同时给\n");
        std::printf("RESULT: FAILED\n");
        return 2;
    }
    if (!plan.blockers.empty())
    {
        std::printf("\n存在阻塞项，拒绝执行：\n");
        for (const std::string& blocker : plan.blockers)
        {
            std::printf("  - %s\n", blocker.c_str());
        }
        std::printf("RESULT: FAILED\n");
        return 1;
    }

    // 第 3 步：执行 + 写回滚日志
    const osp::prepare::ExecResult result = osp::prepare::execute(plan);
    for (const std::string& line : result.log)
    {
        std::printf("  %s\n", line.c_str());
    }
    for (const std::string& warning : result.warnings)
    {
        std::printf("warning: %s\n", warning.c_str());
    }

    std::string error;
    const std::string journal = journalPath.empty() ? "ospt-repair-journal.json" : journalPath;
    {
        std::ofstream out(osp::win7::pathFromUtf8(journal), std::ios::binary | std::ios::trunc);
        if (out)
        {
            const std::string text = osp::prepare::journalToJson(result.journal);
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            out.put('\n');
        }
    }
    std::printf("journal: %s（%zu 条回滚记录）\n", journal.c_str(), result.journal.entries.size());

    if (!reportPath.empty())
    {
        std::ofstream out(osp::win7::pathFromUtf8(reportPath), std::ios::binary | std::ios::trunc);
        if (out)
        {
            std::string text = "applied\t" + std::to_string(result.applied) + "\n";
            for (const std::string& line : result.log)
            {
                text += "log\t" + line + "\n";
            }
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
        }
    }

    if (!result.ok)
    {
        std::printf("%s\n", result.error.c_str());
        std::printf("RESULT: FAILED\n");
        return 1;
    }

    // SC 的文案：优化完成，已修复 N 个问题
    std::printf("\n优化完成，已修复 %zu 个问题\n", result.applied);
    if (!plan.deferred.empty() ||
        std::any_of(plan.actions.begin(), plan.actions.end(), [](const osp::prepare::Action& a) {
            return a.requiresReboot;
        }))
    {
        std::printf("（部分设置需要重启后生效）\n");
    }
    std::printf("RESULT: OK\n");
    return 0;
}

} // namespace osp::host

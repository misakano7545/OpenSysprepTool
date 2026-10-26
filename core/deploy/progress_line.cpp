#include "core/deploy_progress.hpp"

#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

namespace osp::deploy {
namespace {

std::string trim(const std::string& text)
{
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0)
    {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
    {
        --end;
    }
    return text.substr(begin, end - begin);
}

} // namespace

ProgressEvent parseProgressLine(const std::string& line)
{
    ProgressEvent event;
    const std::string text = trim(line);
    if (text.empty() || text.front() != '{')
    {
        // osptd 的普通人读输出（"执行  xxx"、"[通过] ..."）不是进度行，正常跳过
        event.error = "不是进度行";
        return event;
    }

    nlohmann::json parsed;
    try
    {
        parsed = nlohmann::json::parse(text);
    }
    catch (const nlohmann::json::exception& exception)
    {
        event.error = std::string("JSON 解析失败：") + exception.what();
        return event;
    }
    if (!parsed.is_object())
    {
        event.error = "不是 JSON 对象";
        return event;
    }

    event.event = parsed.value("event", std::string());
    if (event.event.empty())
    {
        event.error = "缺少 event 字段";
        return event;
    }

    event.phase = parsed.value("phase", std::string());
    event.phaseOrdinal = parsed.value("phaseOrdinal", 0);
    event.phaseTotal = parsed.value("phaseTotal", 0);
    event.taskIndex = parsed.value("taskIndex", 0);
    event.taskTotal = parsed.value("taskTotal", 0);
    event.finished = parsed.value("finished", 0);
    event.totalTasks = parsed.value("totalTasks", 0);
    event.ok = parsed.value("ok", true);
    event.failed = parsed.value("failed", 0);
    event.executed = parsed.value("executed", 0);
    event.elapsedMs = parsed.value("elapsedMs", static_cast<std::uint64_t>(0));

    event.parsed = true;
    return event;
}

int overallPercent(int finished, int totalTasks)
{
    if (totalTasks <= 0)
    {
        return 0;
    }
    const int clamped = std::clamp(finished, 0, totalTasks);
    return clamped * 100 / totalTasks;
}

int phasePercent(int taskIndex, int taskTotal)
{
    if (taskTotal <= 0)
    {
        return 0;
    }
    const int clamped = std::clamp(taskIndex, 0, taskTotal);
    return clamped * 100 / taskTotal;
}

} // namespace osp::deploy

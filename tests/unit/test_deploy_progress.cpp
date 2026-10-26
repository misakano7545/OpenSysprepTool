// M10：部署进度行的解析（osptd --progress-json -> ospt 运行页）
//
// 这条通道是运行页唯一的进度来源，坏一行不能让界面崩，也不能把「不是进度行」
// 误判成错误。所以测试重点是三种情况的区分：合法事件 / 普通人读输出 / 坏 JSON。

#include <doctest/doctest.h>

#include <string>

#include "core/deploy_progress.hpp"

using namespace osp;

TEST_CASE("progress: 解析 phaseStart / taskStart / taskFinish / result")
{
    const osp::deploy::ProgressEvent start = osp::deploy::parseProgressLine(
        R"({"event":"phaseStart","phase":"install","phaseOrdinal":2,"phaseTotal":5})"
    );
    REQUIRE(start.parsed);
    CHECK(start.event == "phaseStart");
    CHECK(start.phase == "install");
    CHECK(start.phaseOrdinal == 2);
    CHECK(start.phaseTotal == 5);

    const osp::deploy::ProgressEvent task = osp::deploy::parseProgressLine(
        R"({"event":"taskStart","phase":"install","taskIndex":3,"taskTotal":12,"finished":7,"totalTasks":25})"
    );
    REQUIRE(task.parsed);
    CHECK(task.taskIndex == 3);
    CHECK(task.taskTotal == 12);
    CHECK(task.finished == 7);
    CHECK(task.totalTasks == 25);

    const osp::deploy::ProgressEvent done = osp::deploy::parseProgressLine(
        R"({"event":"taskFinish","phase":"install","ok":false,"elapsedMs":1234,"finished":8,"totalTasks":25})"
    );
    REQUIRE(done.parsed);
    CHECK_FALSE(done.ok);
    CHECK(done.elapsedMs == 1234);

    const osp::deploy::ProgressEvent result = osp::deploy::parseProgressLine(
        R"({"event":"result","ok":true,"executed":25,"failed":0,"elapsedMs":9000})"
    );
    REQUIRE(result.parsed);
    CHECK(result.event == "result");
    CHECK(result.ok);
    CHECK(result.executed == 25);
    CHECK(result.elapsedMs == 9000);
}

TEST_CASE("progress: 普通人读输出不算错误，只报告不是进度行")
{
    for (const char* line :
         {"执行  C:\\Windows\\System32\\bcdedit.exe /set {bootmgr} timeout 30",
          "  [通过] 防篡改校验通过：payload.txt",
          "RESULT: OK",
          ""})
    {
        const osp::deploy::ProgressEvent event = osp::deploy::parseProgressLine(line);
        CHECK_FALSE(event.parsed);
        CHECK(!event.error.empty());
    }
}

TEST_CASE("progress: 坏 JSON 与缺少 event 都被识别，且不会抛异常")
{
    const osp::deploy::ProgressEvent broken = osp::deploy::parseProgressLine(R"({"event": )");
    CHECK_FALSE(broken.parsed);
    CHECK(broken.error.find("JSON") != std::string::npos);

    const osp::deploy::ProgressEvent noEvent = osp::deploy::parseProgressLine(R"({"phase":"install"})");
    CHECK_FALSE(noEvent.parsed);
    CHECK(noEvent.error.find("event") != std::string::npos);

    // 数组不是对象
    const osp::deploy::ProgressEvent array = osp::deploy::parseProgressLine("[1,2,3]");
    CHECK_FALSE(array.parsed);
}

TEST_CASE("progress: 前后空白与 CRLF 残留不影响解析")
{
    const osp::deploy::ProgressEvent event =
        osp::deploy::parseProgressLine("  {\"event\":\"taskStart\",\"taskIndex\":1,\"taskTotal\":4}  ");
    REQUIRE(event.parsed);
    CHECK(event.taskIndex == 1);
    CHECK(event.taskTotal == 4);
}

TEST_CASE("progress: 百分比计算与边界")
{
    CHECK(osp::deploy::overallPercent(0, 25) == 0);
    CHECK(osp::deploy::overallPercent(13, 25) == 52);
    CHECK(osp::deploy::overallPercent(25, 25) == 100);
    // 越界与除零都要夹住，界面不能让进度条出 100 以上或除零崩溃
    CHECK(osp::deploy::overallPercent(30, 25) == 100);
    CHECK(osp::deploy::overallPercent(-5, 25) == 0);
    CHECK(osp::deploy::overallPercent(5, 0) == 0);

    CHECK(osp::deploy::phasePercent(1, 4) == 25);
    CHECK(osp::deploy::phasePercent(9, 4) == 100);
    CHECK(osp::deploy::phasePercent(2, 0) == 0);
}

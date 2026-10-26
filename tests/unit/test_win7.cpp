// M5：Windows 7 兼容层的单元测试
//
// 这里能测的是「编译期常量与名单本身正确」，PE 层面的三道闸由
// tests/integration/verify_win7_compat.py 负责。

#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include <windows.h>

#include "core/win7.hpp"

using namespace osp;

TEST_CASE("编译期闸门常量就是 Windows 7 SP1")
{
    CHECK(win7::kWinVer == 0x0601);
    CHECK(win7::kWin32WinNt == 0x0601);
    CHECK(win7::kNtddiVersion == 0x06010000);
    CHECK(win7::kExpectedSubsystemMajor == 6);
    CHECK(win7::kExpectedSubsystemMinor == 1);

    // 编译期常量必须等于宏本身，否则头文件里的门槛是假的
    CHECK(win7::kWinVer == static_cast<std::uint32_t>(WINVER));
    CHECK(win7::kWin32WinNt == static_cast<std::uint32_t>(_WIN32_WINNT));
    CHECK(win7::kNtddiVersion == static_cast<std::uint32_t>(NTDDI_VERSION));
}

TEST_CASE("禁止的运行库名单覆盖 /MT 被破坏的所有形态")
{
    const std::vector<const char*>& dlls = win7::forbiddenRuntimeDlls();
    REQUIRE(!dlls.empty());

    const auto has = [&dlls](const char* wanted) {
        return std::any_of(dlls.begin(), dlls.end(), [wanted](const char* item) {
            return std::string(item) == wanted;
        });
    };

    CHECK(has("vcruntime140.dll"));
    CHECK(has("vcruntime140_1.dll"));
    CHECK(has("msvcp140.dll"));
    CHECK(has("ucrtbase.dll"));
}

TEST_CASE("动态解析白名单的每条都写明出处、起始系统与回退路径")
{
    const std::vector<win7::DynamicApi>& apis = win7::dynamicApiWhitelist();

    for (const win7::DynamicApi& api : apis)
    {
        const std::string label = std::string("检查条目: ") + api.module + "!" + api.proc;
        INFO(label);
        CHECK(api.module != nullptr);
        CHECK(api.proc != nullptr);
        CHECK(std::string(api.module).find(".dll") != std::string::npos);
        CHECK(std::string(api.proc).size() > 3);
        // 「留 Win7 回退路径」的书面证据
        CHECK(api.since != nullptr);
        CHECK(std::string(api.since).find("Windows") != std::string::npos);
        CHECK(api.purpose != nullptr);
        CHECK(std::string(api.purpose).size() > 5);
        CHECK(api.fallback != nullptr);
        CHECK(std::string(api.fallback).size() > 5);
    }
}

TEST_CASE("白名单查询不区分大小写，未知 API 一律为假")
{
    for (const win7::DynamicApi& api : win7::dynamicApiWhitelist())
    {
        CHECK(win7::isWhitelisted(api.module, api.proc));
        // 换成大写也要认得
        std::string upperModule(api.module);
        std::string upperProc(api.proc);
        for (char& ch : upperModule)
        {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        for (char& ch : upperProc)
        {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        CHECK(win7::isWhitelisted(upperModule.c_str(), upperProc.c_str()));
    }

    CHECK_FALSE(win7::isWhitelisted("kernel32.dll", "这个函数不存在"));
    CHECK_FALSE(win7::isWhitelisted(nullptr, "GetTickCount64"));
    CHECK_FALSE(win7::isWhitelisted("kernel32.dll", nullptr));
}

TEST_CASE("动态解析：已加载模块能取到，取不到时返回空而绝不回退静态导入")
{
    // ntdll 在任何进程里都已加载
    CHECK(win7::resolve("ntdll.dll", "RtlGetVersion") != nullptr);
    CHECK(win7::resolve("kernel32.dll", "GetTickCount64") != nullptr);

    // 不存在的过程名只能是 nullptr —— 这正是不静态导入的意义
    CHECK(win7::resolve("kernel32.dll", "OspNotARealApiName") == nullptr);
    CHECK(win7::resolve("不存在的模块.dll", "whatever") == nullptr);
    CHECK(win7::resolve(nullptr, "GetTickCount64") == nullptr);

    // 第二次调用走缓存，结果一致
    CHECK(win7::resolve("ntdll.dll", "RtlGetVersion") == win7::resolve("ntdll.DLL", "RTLGETVERSION"));
}

TEST_CASE("运行期系统版本读得到，且满足最低目标")
{
    const win7::OsVersion version = win7::osVersion();

    CHECK(version.queriedFromNt);
    CHECK(version.major >= 6);
    CHECK(version.build > 0);
    CHECK(version.platformId == 2); // VER_PLATFORM_WIN32_NT

    // 单元测试跑在构建机上，它必然不弱于 Win7 SP1
    CHECK(win7::isSupportedTarget(version));

    const std::string text = win7::describe(version);
    CHECK(text.find('.') != std::string::npos);
    CHECK(text.find("build") != std::string::npos);
}

TEST_CASE("最低目标判定按 Win7 SP1 卡边界")
{
    const auto make = [](std::uint32_t major, std::uint32_t minor, std::uint32_t sp) {
        win7::OsVersion version;
        version.major = major;
        version.minor = minor;
        version.servicePackMajor = sp;
        version.queriedFromNt = true;
        return version;
    };

    CHECK_FALSE(win7::isSupportedTarget(make(6, 0, 2))); // Vista，SP 再高也不行
    CHECK_FALSE(win7::isSupportedTarget(make(6, 1, 0))); // Win7 RTM：MSVC 不支持
    CHECK(win7::isSupportedTarget(make(6, 1, 1)));       // Win7 SP1：最低目标
    CHECK(win7::isSupportedTarget(make(6, 1, 2)));
    CHECK(win7::isSupportedTarget(make(6, 2, 0))); // Win8
    CHECK(win7::isSupportedTarget(make(6, 3, 0))); // Win8.1
    CHECK(win7::isSupportedTarget(make(10, 0, 0)));
    CHECK_FALSE(win7::isSupportedTarget(make(5, 1, 3))); // XP
}

TEST_CASE("时间接口可用，且 Win8+ 缺失时有回退")
{
    const std::uint64_t first = win7::fileTimeNow();
    const std::uint64_t second = win7::fileTimeNow();
    CHECK(first > 0);
    CHECK(second >= first);

    // FILETIME 的 100ns 计数：1601 年到 2026 年之间必然大于 1.3e17
    CHECK(first > 130000000000000000ull);

    const std::string text = win7::formatFileTime(first);
    CHECK(text.size() == 23); // YYYY-MM-DD HH:MM:SS.mmm
    CHECK(text[4] == '-');
    CHECK(text[7] == '-');
    CHECK(text[10] == ' ');
    CHECK(text[13] == ':');
    CHECK(text[16] == ':');
    CHECK(text[19] == '.');
    CHECK(text.substr(0, 2) == "20"); // 本世纪

    const std::uint64_t ticks = win7::tickCountMs();
    CHECK(ticks > 0);
}

TEST_CASE("GetSystemTimePreciseAsFileTime 是动态解析来的，且登记在白名单里")
{
    // 这条断言的意义：如果哪天有人把它改成静态调用，编译期闸门会在
    // _WIN32_WINNT=0x0601 下直接报错；而白名单保证它仍然有回退说明。
    CHECK(win7::isWhitelisted("kernel32.dll", "GetSystemTimePreciseAsFileTime"));

    CHECK(win7::fileTimeNow() > 0);
    const std::vector<std::string> resolved = win7::resolvedProcs();
    CHECK(!resolved.empty());

    const bool sawPrecise = std::any_of(resolved.begin(), resolved.end(), [](const std::string& name) {
        return name.find("GetSystemTimePreciseAsFileTime") != std::string::npos;
    });
    // 在 Win8+ 上必然解析成功；在 Win7 上取不到是预期行为，此时走回退
    const win7::OsVersion version = win7::osVersion();
    if (win7::isSupportedTarget(version) && version.major > 6)
    {
        CHECK(sawPrecise);
    }
    else
    {
        CHECK_FALSE(sawPrecise);
    }
}

TEST_CASE("命令行按 UTF-8 取出，不再吃 ANSI 代码页的亏")
{
    // 回归背景：中文系统上 CRT 的 argv 是 GBK 字节。曾经这里误把 GBK 当 UTF-8
    // 交给 path(u8string)，MSVC 对非法 UTF-8 直接抛异常，没人接就 abort()，
    // 表现成「带中文路径调用 ospt 无输出、退出码 0xC0000409」。
    const std::vector<std::string> args = win7::utf8CommandLineArguments();
    REQUIRE(!args.empty());
    // 第一个必然是程序自身路径，且是合法 UTF-8
    CHECK(!args[0].empty());
    CHECK(win7::pathFromUtf8(args[0]).filename().string().find("osp_unit_tests") != std::string::npos);
}

TEST_CASE("UTF-8 路径构造：合法 UTF-8、GBK 字节、空串都不抛异常")
{
    const std::filesystem::path cn = win7::pathFromUtf8("D:/临时目录/配置.json");
    CHECK(win7::toUtf8Path(cn).find("配置.json") != std::string::npos);
    const std::string cnName = win7::toUtf8Path(cn.filename());
    // 注意不能用 path::string()：那给的是 ANSI 编码，中文系统上是 GBK 字节，
    // 跟 UTF-8 字面量比较必然不等 —— 这正是全项目一律走 toUtf8Path 的原因
    CHECK(cnName == "配置.json");

    // GBK 编码的「中文」（D6 D0 CE C4）：不是合法 UTF-8，必须降级而不是抛
    const std::string gbk("\xD6\xD0\xCE\xC4", 4);
    const std::filesystem::path fromGbk = win7::pathFromUtf8(gbk);
    CHECK_FALSE(fromGbk.empty());
    // 中文系统上按 CP_ACP（936）解释正好得到「中文」；英文系统上可能被替换，
    // 但**绝不能抛异常**——这条断言的重点就是不抛。
    if (GetACP() == 936)
    {
        CHECK(win7::toUtf8Path(fromGbk) == "中文");
    }

    CHECK(win7::pathFromUtf8("").empty());
}

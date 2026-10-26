#include "core/regio.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

namespace {

constexpr const char* kTestKey = "Software\\OpenSysprepTool\\UnitTest";

void cleanTestKey()
{
    std::string error;
    osp::regio::deleteTree(osp::regio::Hive::CurrentUser, kTestKey, error);
}

} // namespace

TEST_CASE("regio: hive and value type names round-trip")
{
    osp::regio::Hive hive = osp::regio::Hive::LocalMachine;
    std::string error;
    CHECK(osp::regio::parseHive("HKLM", hive, error));
    CHECK(hive == osp::regio::Hive::LocalMachine);
    CHECK(osp::regio::parseHive("HKCU", hive, error));
    CHECK(hive == osp::regio::Hive::CurrentUser);
    CHECK(osp::regio::parseHive("HKU\\.DEFAULT", hive, error));
    CHECK(hive == osp::regio::Hive::UsersDefault);
    CHECK_FALSE(osp::regio::parseHive("HKCR", hive, error));
    CHECK_FALSE(error.empty());

    osp::regio::ValueType type = osp::regio::ValueType::String;
    CHECK(osp::regio::parseValueType("dword", type, error));
    CHECK(type == osp::regio::ValueType::Dword);
    CHECK(osp::regio::parseValueType("multiSz", type, error));
    CHECK(type == osp::regio::ValueType::MultiString);
    CHECK_FALSE(osp::regio::parseValueType("nonsense", type, error));

    CHECK(std::string(osp::regio::hiveId(osp::regio::Hive::UsersDefault)) == "HKU\\.DEFAULT");
    CHECK(std::string(osp::regio::valueTypeId(osp::regio::ValueType::ExpandString)) == "expandSz");
}

TEST_CASE("regio: value types round-trip through HKCU")
{
    cleanTestKey();

    std::string error;
    osp::regio::Key key = osp::regio::create(osp::regio::Hive::CurrentUser, kTestKey, error);
    REQUIRE(key.valid());

    SUBCASE("string and expand string keep UTF-8")
    {
        osp::regio::Value text;
        text.type = osp::regio::ValueType::String;
        text.text = "hello 自检";
        CHECK(osp::regio::writeValue(key, "Text", text, error));

        osp::regio::Value expand;
        expand.type = osp::regio::ValueType::ExpandString;
        expand.text = "%SystemRoot%\\Temp";
        CHECK(osp::regio::writeValue(key, "Expand", expand, error));

        osp::regio::Value read;
        bool exists = false;
        CHECK(osp::regio::readValue(key, "Text", read, exists, error));
        CHECK(exists);
        CHECK(read.type == osp::regio::ValueType::String);
        CHECK(read.text == "hello 自检");

        CHECK(osp::regio::readValue(key, "Expand", read, exists, error));
        CHECK(exists);
        CHECK(read.type == osp::regio::ValueType::ExpandString);
        CHECK(read.text == "%SystemRoot%\\Temp");
    }

    SUBCASE("dword and qword")
    {
        osp::regio::Value number;
        number.type = osp::regio::ValueType::Dword;
        number.dword = 0xDEADBEEF;
        CHECK(osp::regio::writeValue(key, "Dword", number, error));

        osp::regio::Value big;
        big.type = osp::regio::ValueType::Qword;
        big.qword = 0x0123456789ABCDEFull;
        CHECK(osp::regio::writeValue(key, "Qword", big, error));

        osp::regio::Value read;
        bool exists = false;
        CHECK(osp::regio::readValue(key, "Dword", read, exists, error));
        CHECK(exists);
        CHECK(read.dword == 0xDEADBEEF);

        CHECK(osp::regio::readValue(key, "Qword", read, exists, error));
        CHECK(exists);
        CHECK(read.qword == 0x0123456789ABCDEFull);
    }

    SUBCASE("multi string and binary")
    {
        osp::regio::Value list;
        list.type = osp::regio::ValueType::MultiString;
        list.multi = {"a", "b", "中文"};
        CHECK(osp::regio::writeValue(key, "List", list, error));

        osp::regio::Value binary;
        binary.type = osp::regio::ValueType::Binary;
        binary.bytes = {0x00, 0x01, 0xFE, 0xFF};
        CHECK(osp::regio::writeValue(key, "Binary", binary, error));

        osp::regio::Value read;
        bool exists = false;
        CHECK(osp::regio::readValue(key, "List", read, exists, error));
        CHECK(exists);
        const std::vector<std::string> expected{"a", "b", "中文"};
        CHECK(read.multi == expected);

        CHECK(osp::regio::readValue(key, "Binary", read, exists, error));
        CHECK(exists);
        const std::vector<std::uint8_t> expectedBytes{0x00, 0x01, 0xFE, 0xFF};
        CHECK(read.bytes == expectedBytes);
    }

    SUBCASE("missing value is not an error")
    {
        osp::regio::Value read;
        bool exists = true;
        CHECK(osp::regio::readValue(key, "NoSuchValue", read, exists, error));
        CHECK_FALSE(exists);
        CHECK(error.empty());
    }

    SUBCASE("delete value is idempotent")
    {
        CHECK(osp::regio::deleteValue(key, "NoSuchValue", error));
        CHECK(osp::regio::deleteValue(key, "Text", error));
    }

    key.close();
    cleanTestKey();
}

TEST_CASE("regio: deleteTree removes the key and is idempotent")
{
    cleanTestKey();

    std::string error;
    osp::regio::Key key = osp::regio::create(osp::regio::Hive::CurrentUser, kTestKey, error);
    REQUIRE(key.valid());
    osp::regio::Value value;
    value.type = osp::regio::ValueType::Dword;
    value.dword = 1;
    CHECK(osp::regio::writeValue(key, "X", value, error));
    key.close();

    CHECK(osp::regio::deleteTree(osp::regio::Hive::CurrentUser, kTestKey, error));
    CHECK(osp::regio::deleteTree(osp::regio::Hive::CurrentUser, kTestKey, error));

    osp::regio::Key reopened = osp::regio::open(osp::regio::Hive::CurrentUser, kTestKey, false, error);
    CHECK_FALSE(reopened.valid());
}

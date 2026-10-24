#include <doctest/doctest.h>

#include <string>

#include "core/version.hpp"

TEST_CASE("version string matches the numeric constants")
{
    const std::string expected = std::to_string(osp::kVersionMajor) + "." +
        std::to_string(osp::kVersionMinor) + "." + std::to_string(osp::kVersionPatch);

    CHECK(std::string(osp::versionString()) == expected);
}

TEST_CASE("version components are non-negative")
{
    CHECK(osp::kVersionMajor >= 0);
    CHECK(osp::kVersionMinor >= 0);
    CHECK(osp::kVersionPatch >= 0);
}

#include <ezmu/foundation/version.hpp>

#include <catch2/catch_test_macros.hpp>

#include <format>

using ezmu::foundation::kEngineVersion;
using ezmu::foundation::kEngineVersionString;
using ezmu::foundation::Version;

TEST_CASE("Engine version string matches the numeric version", "[version]") {
    const auto formatted =
        std::format("{}.{}.{}", kEngineVersion.major, kEngineVersion.minor, kEngineVersion.patch);
    CHECK(formatted == kEngineVersionString);
}

TEST_CASE("Versions are ordered semantically", "[version]") {
    STATIC_CHECK(Version{0, 1, 0} < Version{0, 2, 0});
    STATIC_CHECK(Version{0, 9, 9} < Version{1, 0, 0});
    STATIC_CHECK(Version{1, 2, 3} == Version{1, 2, 3});
}

TEST_CASE("Engine name is ezMU", "[version]") {
    CHECK(ezmu::foundation::kEngineName == "ezMU");
}

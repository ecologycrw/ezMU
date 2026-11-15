#include <ezmu/foundation/geometry.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace {

using ezmu::foundation::Extent2D;
using ezmu::foundation::Offset2D;
using ezmu::foundation::Rect2D;

TEST_CASE("An extent with a zero dimension is empty", "[geometry]") {
    STATIC_CHECK(Extent2D{}.isEmpty());
    STATIC_CHECK(Extent2D{.width = 0, .height = 10}.isEmpty());
    STATIC_CHECK(Extent2D{.width = 10, .height = 0}.isEmpty());
    STATIC_CHECK_FALSE(Extent2D{.width = 1, .height = 1}.isEmpty());
}

TEST_CASE("Extent area does not overflow 32 bits", "[geometry]") {
    constexpr Extent2D kHuge{.width = 100'000, .height = 100'000};
    STATIC_CHECK(kHuge.area() == std::uint64_t{10'000'000'000});
}

TEST_CASE("Geometry types compare by value", "[geometry]") {
    constexpr Rect2D kA{.offset = {.x = -1, .y = 2}, .extent = {.width = 3, .height = 4}};
    constexpr Rect2D kB{.offset = {.x = -1, .y = 2}, .extent = {.width = 3, .height = 4}};
    STATIC_CHECK(kA == kB);
    STATIC_CHECK(kA != Rect2D{});
    STATIC_CHECK(Offset2D{.x = 1, .y = 2} != Offset2D{.x = 2, .y = 1});
}

} // namespace

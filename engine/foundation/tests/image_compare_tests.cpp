#include <ezmu/foundation/image_compare.hpp>

#include <catch2/catch_test_macros.hpp>

namespace {

using ezmu::foundation::compareImages;
using ezmu::foundation::Extent2D;
using ezmu::foundation::Image;
using ezmu::foundation::Rgba8;

constexpr Extent2D kSize{.width = 10, .height = 10};
constexpr Rgba8 kGray{.r = 100, .g = 100, .b = 100, .a = 255};
constexpr Rgba8 kRed{.r = 255, .g = 0, .b = 0, .a = 255};

TEST_CASE("Identical images match without a diff image", "[image_compare]") {
    const Image image(kSize, kGray);
    const auto result = compareImages(image, image);
    CHECK(result.matches);
    CHECK(result.sizeMatches);
    CHECK(result.differingPixels == 0);
    CHECK(result.maxChannelDelta == 0);
    CHECK(result.diff.isEmpty());
}

TEST_CASE("Images of different sizes never match", "[image_compare]") {
    const auto result = compareImages(Image(kSize), Image(Extent2D{.width = 10, .height = 9}));
    CHECK_FALSE(result.matches);
    CHECK_FALSE(result.sizeMatches);
}

TEST_CASE("Channel differences within the tolerance are ignored", "[image_compare]") {
    const Image expected(kSize, kGray);
    Image actual = expected;
    actual.setPixel(3, 4, Rgba8{.r = 102, .g = 99, .b = 100, .a = 255});

    const auto strict = compareImages(actual, expected);
    CHECK_FALSE(strict.matches);
    CHECK(strict.differingPixels == 1);
    CHECK(strict.maxChannelDelta == 2);

    const auto tolerant = compareImages(actual, expected, {.channelTolerance = 2});
    CHECK(tolerant.matches);
    CHECK(tolerant.differingPixels == 0);
    CHECK(tolerant.maxChannelDelta == 2);
}

TEST_CASE("A ratio of differing pixels can be allowed", "[image_compare]") {
    const Image expected(kSize, kGray);
    Image actual = expected;
    actual.setPixel(0, 0, kRed); // 1 of 100 pixels

    CHECK_FALSE(compareImages(actual, expected, {.maxDifferingPixelRatio = 0.005}).matches);
    CHECK(compareImages(actual, expected, {.maxDifferingPixelRatio = 0.01}).matches);
}

TEST_CASE("The diff image marks differing pixels in red", "[image_compare]") {
    const Image expected(kSize, kGray);
    Image actual = expected;
    actual.setPixel(7, 2, kRed);

    const auto result = compareImages(actual, expected);
    REQUIRE(result.diff.size() == kSize);
    CHECK(result.diff.pixel(7, 2) == kRed);
    CHECK(result.diff.pixel(0, 0) == Rgba8{.r = 25, .g = 25, .b = 25, .a = 255});
}

} // namespace

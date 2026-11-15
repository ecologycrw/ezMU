#include <ezmu/foundation/image.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace {

using ezmu::foundation::ErrorCode;
using ezmu::foundation::Extent2D;
using ezmu::foundation::Image;
using ezmu::foundation::Rgba8;

constexpr Rgba8 kRed{.r = 255, .g = 0, .b = 0, .a = 255};
constexpr Rgba8 kBlue{.r = 0, .g = 0, .b = 255, .a = 128};

TEST_CASE("A default image is empty", "[image]") {
    const Image image;
    CHECK(image.isEmpty());
    CHECK(image.bytes().empty());
}

TEST_CASE("An image is filled on construction and stores tightly packed RGBA8", "[image]") {
    const Image image(Extent2D{.width = 3, .height = 2}, kRed);
    CHECK(image.width() == 3);
    CHECK(image.height() == 2);
    REQUIRE(image.bytes().size() == 3 * 2 * Image::kBytesPerPixel);
    CHECK(image.pixel(2, 1) == kRed);
    CHECK(image.bytes()[0] == 255);
    CHECK(image.bytes()[3] == 255);
}

TEST_CASE("Pixels are addressed row-major from the top-left", "[image]") {
    Image image(Extent2D{.width = 4, .height = 3});
    image.setPixel(1, 2, kBlue);
    CHECK(image.pixel(1, 2) == kBlue);
    const std::size_t offset = ((2 * 4) + 1) * Image::kBytesPerPixel;
    CHECK(image.bytes()[offset + 2] == 255);
    CHECK(image.bytes()[offset + 3] == 128);
    CHECK(image.pixel(0, 0) == Rgba8{});
}

TEST_CASE("An image can be built from raw bytes of the right size", "[image]") {
    std::vector<std::uint8_t> bytes{1, 2, 3, 4, 5, 6, 7, 8};
    auto image = Image::fromBytes(Extent2D{.width = 2, .height = 1}, std::move(bytes));
    REQUIRE(image.has_value());
    CHECK(image->pixel(1, 0) == Rgba8{.r = 5, .g = 6, .b = 7, .a = 8});
}

TEST_CASE("Building an image from bytes of the wrong size fails", "[image]") {
    auto image = Image::fromBytes(Extent2D{.width = 2, .height = 2}, std::vector<std::uint8_t>(3));
    REQUIRE_FALSE(image.has_value());
    CHECK(image.error().code() == ErrorCode::kInvalidArgument);
}

TEST_CASE("Images compare by size and pixels", "[image]") {
    Image a(Extent2D{.width = 2, .height = 2}, kRed);
    const Image b(Extent2D{.width = 2, .height = 2}, kRed);
    CHECK(a == b);
    a.setPixel(0, 0, kBlue);
    CHECK(a != b);
    CHECK(Image(Extent2D{.width = 4, .height = 1}) != Image(Extent2D{.width = 2, .height = 2}));
}

} // namespace

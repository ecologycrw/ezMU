#include <ezmu/foundation/image_io.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>
#include <vector>

namespace {

using ezmu::foundation::decodePng;
using ezmu::foundation::encodePng;
using ezmu::foundation::ErrorCode;
using ezmu::foundation::Extent2D;
using ezmu::foundation::Image;
using ezmu::foundation::readPng;
using ezmu::foundation::Rgba8;
using ezmu::foundation::writePng;

Image makePattern() {
    Image image(Extent2D{.width = 5, .height = 3});
    for (std::uint32_t y = 0; y < image.height(); ++y) {
        for (std::uint32_t x = 0; x < image.width(); ++x) {
            image.setPixel(x, y,
                           Rgba8{.r = static_cast<std::uint8_t>(x * 50),
                                 .g = static_cast<std::uint8_t>(y * 100),
                                 .b = static_cast<std::uint8_t>((x + y) * 20),
                                 .a = static_cast<std::uint8_t>(255 - (x * 10))});
        }
    }
    return image;
}

/// A unique temporary directory, removed on destruction.
class TempDir {
public:
    TempDir() {
        std::random_device device;
        m_path = std::filesystem::temp_directory_path() /
                 ("ezmu-image-io-" + std::to_string(device()) + "-" + std::to_string(device()));
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;
};

TEST_CASE("PNG encoding round-trips pixels exactly", "[image_io]") {
    const Image original = makePattern();
    auto encoded = encodePng(original);
    REQUIRE(encoded.has_value());
    CHECK(encoded->size() > 8);

    auto decoded = decodePng(*encoded);
    REQUIRE(decoded.has_value());
    CHECK(*decoded == original);
}

TEST_CASE("Encoding an empty image fails", "[image_io]") {
    auto encoded = encodePng(Image{});
    REQUIRE_FALSE(encoded.has_value());
    CHECK(encoded.error().code() == ErrorCode::kInvalidArgument);
}

TEST_CASE("Decoding invalid data fails with a parse error", "[image_io]") {
    const std::vector<std::uint8_t> garbage{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    auto decoded = decodePng(garbage);
    REQUIRE_FALSE(decoded.has_value());
    CHECK(decoded.error().code() == ErrorCode::kParseError);

    CHECK(decodePng({}).error().code() == ErrorCode::kParseError);
}

TEST_CASE("PNG files round-trip through disk, creating parent directories", "[image_io]") {
    const TempDir dir;
    const auto path = dir.path() / "nested" / "capture.png";
    const Image original = makePattern();

    REQUIRE(writePng(path, original).has_value());
    auto loaded = readPng(path);
    REQUIRE(loaded.has_value());
    CHECK(*loaded == original);
}

TEST_CASE("Reading a missing PNG file reports not found", "[image_io]") {
    const TempDir dir;
    auto loaded = readPng(dir.path() / "missing.png");
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().code() == ErrorCode::kNotFound);
}

} // namespace

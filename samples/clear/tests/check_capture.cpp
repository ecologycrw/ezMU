// ezmu_clear_check <capture.png> <swapchain format> <r,g,b[,a]>
//
// Test helper for samples.clear: checks that every pixel of an ezmu_clear capture equals the
// linear clear color as the swapchain format stores it (sRGB-encoded for *Srgb formats), within
// one step of rounding. Exit code 0 on match, 1 otherwise.

#include <ezmu/foundation/image.hpp>
#include <ezmu/foundation/image_io.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <print>
#include <span>
#include <string_view>
#include <system_error>

namespace {

namespace foundation = ezmu::foundation;

constexpr int kChannelTolerance = 1;

[[nodiscard]] std::optional<std::array<float, 4>> parseColor(std::string_view text) {
    std::array<float, 4> color{0.0F, 0.0F, 0.0F, 1.0F};
    std::size_t count = 0;
    const char* it = text.data();
    const char* const end = text.data() + text.size();
    while (count < color.size() && it != end) {
        const auto [next, error] = std::from_chars(it, end, color.at(count));
        if (error != std::errc{}) {
            return std::nullopt;
        }
        ++count;
        it = next;
        if (it != end && *it++ != ',') {
            return std::nullopt;
        }
    }
    if (it != end || count < 3) {
        return std::nullopt;
    }
    return color;
}

[[nodiscard]] std::uint8_t toByte(float value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
}

[[nodiscard]] float encodeSrgb(float linear) {
    return linear <= 0.0031308F ? linear * 12.92F
                                : (1.055F * std::pow(linear, 1.0F / 2.4F)) - 0.055F;
}

[[nodiscard]] int run(std::span<char* const> args) {
    if (args.size() != 4) {
        std::println(stderr, "usage: ezmu_clear_check <capture.png> <format> <r,g,b[,a]>");
        return 1;
    }
    const std::filesystem::path path(args[1]);
    const std::string_view format = args[2];
    const auto color = parseColor(args[3]);
    if (!color) {
        std::println(stderr, "invalid color: {}", args[3]);
        return 1;
    }

    const bool srgb = format.ends_with("Srgb");
    const auto channel = [&](std::size_t i) {
        return toByte(srgb && i < 3 ? encodeSrgb(color->at(i)) : color->at(i));
    };
    const foundation::Rgba8 expected{
        .r = channel(0), .g = channel(1), .b = channel(2), .a = channel(3)};

    auto image = foundation::readPng(path);
    if (!image) {
        std::println(stderr, "cannot read {}: {}", path.string(), image.error().message());
        return 1;
    }
    if (image->isEmpty()) {
        std::println(stderr, "{} is empty", path.string());
        return 1;
    }

    std::uint64_t differing = 0;
    foundation::Rgba8 firstDifferent{};
    for (std::uint32_t y = 0; y < image->height(); ++y) {
        for (std::uint32_t x = 0; x < image->width(); ++x) {
            const foundation::Rgba8 p = image->pixel(x, y);
            const bool match = std::abs(p.r - expected.r) <= kChannelTolerance &&
                               std::abs(p.g - expected.g) <= kChannelTolerance &&
                               std::abs(p.b - expected.b) <= kChannelTolerance &&
                               std::abs(p.a - expected.a) <= kChannelTolerance;
            if (!match && differing++ == 0) {
                firstDifferent = p;
            }
        }
    }

    std::println("{}: {}x{} {}, expected ({}, {}, {}, {})", path.string(), image->width(),
                 image->height(), format, expected.r, expected.g, expected.b, expected.a);
    if (differing != 0) {
        std::println(stderr, "{} pixel(s) differ, e.g. ({}, {}, {}, {})", differing,
                     firstDifferent.r, firstDifferent.g, firstDifferent.b, firstDifferent.a);
        return 1;
    }
    std::println("all pixels match");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(std::span<char* const>(argv, static_cast<std::size_t>(argc)));
    } catch (const std::exception& e) {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}

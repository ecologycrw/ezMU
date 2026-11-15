#include <ezmu/foundation/image_compare.hpp>

#include <algorithm>
#include <cstdlib>

namespace ezmu::foundation {

namespace {

std::uint8_t channelDelta(std::uint8_t a, std::uint8_t b) noexcept {
    return static_cast<std::uint8_t>(a > b ? a - b : b - a);
}

std::uint8_t maxDelta(Rgba8 a, Rgba8 b) noexcept {
    return std::max({channelDelta(a.r, b.r), channelDelta(a.g, b.g), channelDelta(a.b, b.b),
                     channelDelta(a.a, b.a)});
}

Rgba8 dimmedGray(Rgba8 color) noexcept {
    // Integer Rec. 601 luma, then 25 % intensity.
    const unsigned luma = ((299U * color.r) + (587U * color.g) + (114U * color.b)) / 1000U;
    const auto value = static_cast<std::uint8_t>(luma / 4U);
    return Rgba8{.r = value, .g = value, .b = value, .a = 255};
}

} // namespace

ImageComparison compareImages(const Image& actual, const Image& expected,
                              const ImageCompareOptions& options) {
    ImageComparison result;
    result.sizeMatches = actual.size() == expected.size();
    if (!result.sizeMatches) {
        return result;
    }

    for (std::uint32_t y = 0; y < expected.height(); ++y) {
        for (std::uint32_t x = 0; x < expected.width(); ++x) {
            const std::uint8_t delta = maxDelta(actual.pixel(x, y), expected.pixel(x, y));
            result.maxChannelDelta = std::max(result.maxChannelDelta, delta);
            if (delta > options.channelTolerance) {
                ++result.differingPixels;
            }
        }
    }

    const std::uint64_t total = expected.size().area();
    const double ratio =
        total == 0 ? 0.0 : static_cast<double>(result.differingPixels) / static_cast<double>(total);
    result.matches = ratio <= options.maxDifferingPixelRatio;

    if (result.differingPixels > 0) {
        result.diff = Image(expected.size());
        for (std::uint32_t y = 0; y < expected.height(); ++y) {
            for (std::uint32_t x = 0; x < expected.width(); ++x) {
                const Rgba8 want = expected.pixel(x, y);
                const bool differs = maxDelta(actual.pixel(x, y), want) > options.channelTolerance;
                result.diff.setPixel(
                    x, y, differs ? Rgba8{.r = 255, .g = 0, .b = 0, .a = 255} : dimmedGray(want));
            }
        }
    }
    return result;
}

} // namespace ezmu::foundation

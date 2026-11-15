#pragma once

#include <ezmu/foundation/image.hpp>

#include <cstdint>

// Image comparison for reference-image tests (ADR 0020).

namespace ezmu::foundation {

struct ImageCompareOptions {
    /// A pixel differs when any channel differs by more than this.
    std::uint8_t channelTolerance = 0;
    /// The images match when at most this fraction (0..1) of pixels differ.
    double maxDifferingPixelRatio = 0.0;
};

struct ImageComparison {
    bool matches = false;
    bool sizeMatches = false;
    std::uint64_t differingPixels = 0;
    std::uint8_t maxChannelDelta = 0;
    /// Visualization, filled when sizes match and some pixel differs: differing pixels are
    /// opaque red; equal pixels are the expected image dimmed to grayscale at 25 % intensity.
    Image diff;
};

/// Compares `actual` with `expected`. Images of different sizes never match. Pure function,
/// thread-safe; allocates `diff` only when some pixel differs.
[[nodiscard]] ImageComparison compareImages(const Image& actual, const Image& expected,
                                            const ImageCompareOptions& options = {});

} // namespace ezmu::foundation

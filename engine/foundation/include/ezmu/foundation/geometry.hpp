#pragma once

#include <cstdint>

// Small geometric value types shared across modules (e.g. platform window sizes and RHI
// swapchain extents). Plain aggregates: trivially copyable, no invariants, thread-agnostic.

namespace ezmu::foundation {

/// A 2D size in integer units (pixels or logical units, as documented by each API).
struct Extent2D {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    /// True when either dimension is zero (e.g. a minimized window).
    [[nodiscard]] constexpr bool isEmpty() const noexcept { return width == 0 || height == 0; }

    /// Number of cells (width × height), without overflow.
    [[nodiscard]] constexpr std::uint64_t area() const noexcept {
        return std::uint64_t{width} * std::uint64_t{height};
    }

    friend constexpr bool operator==(const Extent2D&, const Extent2D&) = default;
};

/// A 2D position in integer units; may be negative.
struct Offset2D {
    std::int32_t x = 0;
    std::int32_t y = 0;

    friend constexpr bool operator==(const Offset2D&, const Offset2D&) = default;
};

/// An axis-aligned rectangle: top-left `offset` plus `extent`.
struct Rect2D {
    Offset2D offset{};
    Extent2D extent{};

    friend constexpr bool operator==(const Rect2D&, const Rect2D&) = default;
};

} // namespace ezmu::foundation

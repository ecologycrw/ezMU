#pragma once

#include <ezmu/foundation/geometry.hpp>
#include <ezmu/foundation/result.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ezmu::foundation {

/// One RGBA pixel, 8 bits per channel.
struct Rgba8 {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;

    friend constexpr bool operator==(const Rgba8&, const Rgba8&) = default;
};

/// A CPU image: RGBA8, tightly packed rows (stride = width × 4 bytes), top-left origin.
/// Used for GPU readback, frame capture and reference images; not a texture asset format.
///
/// Value type: copyable and movable; owns its pixels. Not synchronized: share across threads
/// only as const or with external synchronization.
class Image {
public:
    static constexpr std::size_t kBytesPerPixel = 4;

    /// An empty image (0 × 0).
    Image() = default;

    /// An image of `size` filled with `fill`. Allocates width × height × 4 bytes.
    explicit Image(Extent2D size, Rgba8 fill = {});

    /// Takes ownership of tightly packed RGBA8 `bytes`. Fails with kInvalidArgument when the
    /// byte count does not match `size`.
    [[nodiscard]] static Result<Image> fromBytes(Extent2D size, std::vector<std::uint8_t> bytes);

    [[nodiscard]] Extent2D size() const noexcept { return m_size; }
    [[nodiscard]] std::uint32_t width() const noexcept { return m_size.width; }
    [[nodiscard]] std::uint32_t height() const noexcept { return m_size.height; }
    [[nodiscard]] bool isEmpty() const noexcept { return m_size.isEmpty(); }

    /// Raw pixel bytes, row-major RGBA8.
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return m_bytes; }
    [[nodiscard]] std::span<std::uint8_t> bytes() noexcept { return m_bytes; }

    /// Pixel at (x, y). Out-of-range coordinates are a programming error (EZMU_ASSERT).
    [[nodiscard]] Rgba8 pixel(std::uint32_t x, std::uint32_t y) const noexcept;
    void setPixel(std::uint32_t x, std::uint32_t y, Rgba8 value) noexcept;

    void fill(Rgba8 value) noexcept;

    friend bool operator==(const Image&, const Image&) = default;

private:
    [[nodiscard]] std::size_t offsetOf(std::uint32_t x, std::uint32_t y) const noexcept;

    Extent2D m_size;
    std::vector<std::uint8_t> m_bytes;
};

} // namespace ezmu::foundation

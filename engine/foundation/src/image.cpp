#include <ezmu/foundation/assert.hpp>
#include <ezmu/foundation/image.hpp>

#include <cstring>
#include <format>
#include <utility>

namespace ezmu::foundation {

static_assert(sizeof(Rgba8) == Image::kBytesPerPixel, "Rgba8 must map 1:1 onto RGBA8 bytes");

Image::Image(Extent2D size, Rgba8 fill)
    : m_size(size), m_bytes(static_cast<std::size_t>(size.area()) * kBytesPerPixel) {
    this->fill(fill);
}

Result<Image> Image::fromBytes(Extent2D size, std::vector<std::uint8_t> bytes) {
    const auto expected = static_cast<std::size_t>(size.area()) * kBytesPerPixel;
    if (bytes.size() != expected) {
        return makeError(ErrorCode::kInvalidArgument,
                         std::format("Image::fromBytes: {}x{} RGBA8 needs {} bytes, got {}",
                                     size.width, size.height, expected, bytes.size()));
    }
    Image image;
    image.m_size = size;
    image.m_bytes = std::move(bytes);
    return image;
}

Rgba8 Image::pixel(std::uint32_t x, std::uint32_t y) const noexcept {
    Rgba8 value;
    std::memcpy(&value, m_bytes.data() + offsetOf(x, y), kBytesPerPixel);
    return value;
}

void Image::setPixel(std::uint32_t x, std::uint32_t y, Rgba8 value) noexcept {
    std::memcpy(m_bytes.data() + offsetOf(x, y), &value, kBytesPerPixel);
}

void Image::fill(Rgba8 value) noexcept {
    for (std::size_t offset = 0; offset < m_bytes.size(); offset += kBytesPerPixel) {
        std::memcpy(m_bytes.data() + offset, &value, kBytesPerPixel);
    }
}

std::size_t Image::offsetOf(std::uint32_t x, std::uint32_t y) const noexcept {
    EZMU_ASSERT_MSG(x < m_size.width, "Image pixel x out of range");
    EZMU_ASSERT_MSG(y < m_size.height, "Image pixel y out of range");
    return ((std::size_t{y} * m_size.width) + x) * kBytesPerPixel;
}

} // namespace ezmu::foundation

#pragma once

#include <ezmu/foundation/geometry.hpp>

#include <cstdint>
#include <string_view>

// Resource descriptions: textures and buffers created by Device. Value types only; no Vulkan type
// appears here. Spec: docs/specs/rhi.md.

namespace ezmu::rhi {

/// Pixel formats supported in M1. `*Srgb` formats store sRGB-encoded values (the GPU converts
/// on write and read); `kBgra8*` exist mainly for swapchains.
enum class Format : std::uint8_t {
    kUndefined,
    kRgba8Unorm,
    kRgba8Srgb,
    kBgra8Unorm,
    kBgra8Srgb,
    kRgba16Float,
    kDepth32Float,
};

/// Stable name of a format (e.g. "Rgba8Unorm").
[[nodiscard]] std::string_view toString(Format format) noexcept;

/// Size of one texel in bytes (0 for kUndefined).
[[nodiscard]] constexpr std::uint32_t bytesPerPixel(Format format) noexcept {
    switch (format) {
    case Format::kRgba8Unorm:
    case Format::kRgba8Srgb:
    case Format::kBgra8Unorm:
    case Format::kBgra8Srgb:
    case Format::kDepth32Float:
        return 4;
    case Format::kRgba16Float:
        return 8;
    case Format::kUndefined:
        return 0;
    }
    return 0;
}

/// True for depth (and depth/stencil) formats.
[[nodiscard]] constexpr bool isDepthFormat(Format format) noexcept {
    return format == Format::kDepth32Float;
}

/// How a texture may be used. Combine with `|`; at least one flag is required.
enum class TextureUsage : std::uint8_t {
    kNone = 0,
    kRenderTarget = 1U << 0U, ///< Color attachment (color formats only).
    kDepthStencil = 1U << 1U, ///< Depth attachment (depth formats only).
    kSampled = 1U << 2U,      ///< Read by shaders.
    kTransferSrc = 1U << 3U,  ///< Source of copies (readback, capture).
    kTransferDst = 1U << 4U,  ///< Destination of copies (uploads).
};

[[nodiscard]] constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) noexcept {
    return static_cast<TextureUsage>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

[[nodiscard]] constexpr TextureUsage operator&(TextureUsage a, TextureUsage b) noexcept {
    return static_cast<TextureUsage>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}

/// True if any flag of `flags` is set in `usage`.
[[nodiscard]] constexpr bool hasAny(TextureUsage usage, TextureUsage flags) noexcept {
    return (usage & flags) != TextureUsage::kNone;
}

/// A 2D texture with one mip level and one layer (M1).
struct TextureDesc {
    foundation::Extent2D size{};
    Format format = Format::kRgba8Unorm;
    TextureUsage usage = TextureUsage::kNone;
    /// Name shown by validation messages and GPU debuggers. Copied; may be empty.
    std::string_view debugName{};
};

/// Where a buffer's memory lives.
enum class MemoryLocation : std::uint8_t {
    kGpu,      ///< Device-local; not CPU-accessible.
    kUpload,   ///< CPU-writable, GPU-readable (host-visible, coherent, persistently mapped).
    kReadback, ///< GPU-writable, CPU-readable (host-visible, coherent, cached when available).
};

/// A linear buffer. In M1 buffers are only used as copy sources/destinations; vertex, index,
/// uniform and storage usages arrive with pipelines.
struct BufferDesc {
    std::uint64_t size = 0; ///< In bytes; must be > 0.
    MemoryLocation location = MemoryLocation::kGpu;
    /// Name shown by validation messages and GPU debuggers. Copied; may be empty.
    std::string_view debugName{};
};

} // namespace ezmu::rhi

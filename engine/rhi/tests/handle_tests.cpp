#include <ezmu/rhi/command_list.hpp>
#include <ezmu/rhi/handles.hpp>
#include <ezmu/rhi/resources.hpp>

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

// No-GPU tests: handle values, format and usage helpers, descriptor defaults.

namespace {

using namespace ezmu::rhi;

TEST_CASE("Default-constructed handles are invalid", "[handles]") {
    STATIC_CHECK_FALSE(TextureHandle{}.isValid());
    STATIC_CHECK_FALSE(BufferHandle{}.isValid());
    STATIC_CHECK(TextureHandle{} == TextureHandle{});
}

TEST_CASE("Handles compare index and generation", "[handles]") {
    constexpr TextureHandle a(3, 1);
    constexpr TextureHandle sameSlotNewGeneration(3, 2);
    STATIC_CHECK(a.isValid());
    STATIC_CHECK(a.index() == 3);
    STATIC_CHECK(a.generation() == 1);
    STATIC_CHECK(a == TextureHandle(3, 1));
    STATIC_CHECK(a != sameSlotNewGeneration);
    STATIC_CHECK_FALSE(TextureHandle(3, 0).isValid()); // Generation 0 is never issued.
}

TEST_CASE("Texture and buffer handles are distinct types", "[handles]") {
    STATIC_CHECK_FALSE(std::is_same_v<TextureHandle, BufferHandle>);
    STATIC_CHECK_FALSE(std::is_convertible_v<TextureHandle, BufferHandle>);
    STATIC_CHECK(std::is_trivially_copyable_v<TextureHandle>);
}

TEST_CASE("Formats report their texel size and kind", "[resources]") {
    STATIC_CHECK(bytesPerPixel(Format::kUndefined) == 0);
    STATIC_CHECK(bytesPerPixel(Format::kRgba8Unorm) == 4);
    STATIC_CHECK(bytesPerPixel(Format::kBgra8Srgb) == 4);
    STATIC_CHECK(bytesPerPixel(Format::kRgba16Float) == 8);
    STATIC_CHECK(bytesPerPixel(Format::kDepth32Float) == 4);
    STATIC_CHECK(isDepthFormat(Format::kDepth32Float));
    STATIC_CHECK_FALSE(isDepthFormat(Format::kRgba8Unorm));
    CHECK(toString(Format::kRgba8Unorm) == "Rgba8Unorm");
    CHECK(toString(Format::kBgra8Srgb) == "Bgra8Srgb");
    CHECK(toString(Format::kDepth32Float) == "Depth32Float");
}

TEST_CASE("Texture usage flags combine", "[resources]") {
    constexpr TextureUsage usage = TextureUsage::kRenderTarget | TextureUsage::kTransferSrc;
    STATIC_CHECK(hasAny(usage, TextureUsage::kRenderTarget));
    STATIC_CHECK(hasAny(usage, TextureUsage::kTransferSrc | TextureUsage::kSampled));
    STATIC_CHECK_FALSE(hasAny(usage, TextureUsage::kSampled));
    STATIC_CHECK_FALSE(hasAny(usage, TextureUsage::kNone));
    STATIC_CHECK((usage & TextureUsage::kTransferSrc) == TextureUsage::kTransferSrc);
}

TEST_CASE("Descriptors have safe defaults", "[resources]") {
    constexpr TextureDesc texture{};
    STATIC_CHECK(texture.size.isEmpty());
    STATIC_CHECK(texture.format == Format::kRgba8Unorm);
    STATIC_CHECK(texture.usage == TextureUsage::kNone);
    constexpr BufferDesc buffer{};
    STATIC_CHECK(buffer.size == 0);
    STATIC_CHECK(buffer.location == MemoryLocation::kGpu);
    constexpr ColorAttachment color{};
    STATIC_CHECK_FALSE(color.texture.isValid());
    STATIC_CHECK(color.load == LoadOp::kClear);
    STATIC_CHECK(color.store == StoreOp::kStore);
    STATIC_CHECK(color.clearColor[3] == 1.0F);
    constexpr RenderingDesc rendering{};
    STATIC_CHECK(rendering.area.extent.isEmpty()); // Whole first attachment.
}

} // namespace

#include <ezmu/foundation/image.hpp>
#include <ezmu/rhi/command_list.hpp>
#include <ezmu/rhi/device.hpp>
#include <ezmu/rhi/resources.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "rhi_test_support.hpp"

namespace {

using namespace ezmu::rhi;
using ezmu::foundation::ErrorCode;
using ezmu::foundation::Extent2D;
using ezmu::foundation::Image;
using ezmu::foundation::Rect2D;
using ezmu::foundation::Rgba8;
using ezmu::rhi::test::checkReferenceImage;
using ezmu::rhi::test::makeTestDevice;
using ezmu::rhi::test::ValidationGuard;

constexpr TextureUsage kTargetUsage = TextureUsage::kRenderTarget | TextureUsage::kTransferSrc;

TextureHandle createTarget(Device& device, Extent2D size, Format format = Format::kRgba8Unorm) {
    auto texture = device.createTexture(
        {.size = size, .format = format, .usage = kTargetUsage, .debugName = "test target"});
    REQUIRE(texture.has_value());
    return *texture;
}

/// Clears `area` of `texture` (whole texture when empty) to `color`.
void clear(CommandList& cmd, TextureHandle texture, std::array<float, 4> color, Rect2D area = {}) {
    cmd.transition(texture, ResourceState::kRenderTarget);
    const ColorAttachment attachment{.texture = texture, .clearColor = color};
    cmd.beginRendering({.colorAttachments = {&attachment, 1}, .area = area});
    cmd.endRendering();
}

/// True when every pixel of `image` equals `expected`.
bool allPixelsEqual(const Image& image, Rgba8 expected) {
    for (std::uint32_t y = 0; y < image.height(); ++y) {
        for (std::uint32_t x = 0; x < image.width(); ++x) {
            if (image.pixel(x, y) != expected) {
                UNSCOPED_INFO("pixel (" << x << ", " << y << ") = " << int{image.pixel(x, y).r}
                                        << ", " << int{image.pixel(x, y).g} << ", "
                                        << int{image.pixel(x, y).b} << ", "
                                        << int{image.pixel(x, y).a});
                return false;
            }
        }
    }
    return true;
}

TEST_CASE("A cleared texture reads back exactly", "[command_list][gpu]") {
    ValidationGuard guard;
    auto device = makeTestDevice(guard);
    const TextureHandle target = createTarget(*device, {.width = 32, .height = 16});

    // 0, 51, 102, 255 are exactly representable as 0.0, 0.2, 0.4, 1.0 in UNORM8.
    REQUIRE(device
                ->executeImmediate([&](CommandList& cmd) {
                    cmd.beginDebugLabel("clear");
                    clear(cmd, target, {0.0F, 0.2F, 0.4F, 1.0F});
                    cmd.endDebugLabel();
                })
                .has_value());

    auto image = device->readTexture(target);
    REQUIRE(image.has_value());
    CHECK(image->size() == Extent2D{.width = 32, .height = 16});
    CHECK(allPixelsEqual(*image, {.r = 0, .g = 51, .b = 102, .a = 255}));

    // Reading back restores the tracked state: the texture can be rendered to again.
    REQUIRE(device
                ->executeImmediate(
                    [&](CommandList& cmd) { clear(cmd, target, {1.0F, 1.0F, 1.0F, 1.0F}); })
                .has_value());
    image = device->readTexture(target);
    REQUIRE(image.has_value());
    CHECK(allPixelsEqual(*image, {.r = 255, .g = 255, .b = 255, .a = 255}));
}

TEST_CASE("BGRA textures read back as RGBA", "[command_list][gpu]") {
    ValidationGuard guard;
    auto device = makeTestDevice(guard);
    const TextureHandle target =
        createTarget(*device, {.width = 8, .height = 8}, Format::kBgra8Unorm);
    REQUIRE(device
                ->executeImmediate(
                    [&](CommandList& cmd) { clear(cmd, target, {1.0F, 0.2F, 0.0F, 0.4F}); })
                .has_value());
    auto image = device->readTexture(target);
    REQUIRE(image.has_value());
    CHECK(allPixelsEqual(*image, {.r = 255, .g = 51, .b = 0, .a = 102}));
}

TEST_CASE("sRGB textures read back sRGB-encoded values", "[command_list][gpu]") {
    ValidationGuard guard;
    auto device = makeTestDevice(guard);
    const TextureHandle target =
        createTarget(*device, {.width = 4, .height = 4}, Format::kRgba8Srgb);
    REQUIRE(device
                ->executeImmediate([&](CommandList& cmd) {
                    clear(cmd, target, {0.5F, 0.0F, 1.0F, 1.0F}); // Linear values.
                })
                .has_value());
    auto image = device->readTexture(target);
    REQUIRE(image.has_value());
    const Rgba8 pixel = image->pixel(0, 0);
    CHECK(pixel.r >= 187); // Linear 0.5 encodes to ~187.5 in sRGB.
    CHECK(pixel.r <= 188);
    CHECK(pixel.g == 0);
    CHECK(pixel.b == 255);
    CHECK(pixel.a == 255); // Alpha is always linear.
}

TEST_CASE("Clearing regions in several passes matches the reference image", "[command_list][gpu]") {
    ValidationGuard guard;
    auto device = makeTestDevice(guard);
    const TextureHandle target = createTarget(*device, {.width = 64, .height = 64});

    REQUIRE(device
                ->executeImmediate([&](CommandList& cmd) {
                    clear(cmd, target, {0.2F, 0.2F, 0.4F, 1.0F});
                    clear(cmd, target, {1.0F, 0.6F, 0.0F, 1.0F},
                          {.offset = {.x = 8, .y = 8}, .extent = {.width = 24, .height = 24}});
                    clear(cmd, target, {0.0F, 0.8F, 0.4F, 1.0F},
                          {.offset = {.x = 24, .y = 40}, .extent = {.width = 40, .height = 16}});
                    // A pass that loads keeps the previous contents.
                    cmd.transition(target, ResourceState::kRenderTarget);
                    const ColorAttachment keep{.texture = target, .load = LoadOp::kLoad};
                    cmd.beginRendering({.colorAttachments = {&keep, 1}});
                    cmd.endRendering();
                })
                .has_value());

    auto image = device->readTexture(target);
    REQUIRE(image.has_value());
    CHECK(image->pixel(0, 0) == Rgba8{.r = 51, .g = 51, .b = 102, .a = 255});
    CHECK(image->pixel(8, 8) == Rgba8{.r = 255, .g = 153, .b = 0, .a = 255});
    CHECK(image->pixel(63, 55) == Rgba8{.r = 0, .g = 204, .b = 102, .a = 255});
    checkReferenceImage(*image, "clear_regions", {.channelTolerance = 1});
}

TEST_CASE("Textures can be copied into a readback buffer", "[command_list][gpu]") {
    ValidationGuard guard;
    auto device = makeTestDevice(guard);
    const TextureHandle target = createTarget(*device, {.width = 4, .height = 2});
    auto buffer = device->createBuffer({.size = 4 * 2 * 4,
                                        .location = MemoryLocation::kReadback,
                                        .debugName = "copy destination"});
    REQUIRE(buffer.has_value());

    REQUIRE(device
                ->executeImmediate([&](CommandList& cmd) {
                    clear(cmd, target, {0.2F, 0.4F, 0.6F, 0.8F});
                    cmd.transition(target, ResourceState::kTransferSrc);
                    cmd.transition(target, ResourceState::kTransferSrc); // Read-only: no-op.
                    cmd.copyTextureToBuffer(target, *buffer);
                })
                .has_value());

    const auto data = device->mappedData(*buffer);
    REQUIRE(data.size() == 32);
    std::array<std::uint8_t, 4> first{};
    std::memcpy(first.data(), data.data(), first.size());
    CHECK(first == std::array<std::uint8_t, 4>{51, 102, 153, 204});
    std::array<std::uint8_t, 4> last{};
    std::memcpy(last.data(), data.data() + 28, last.size());
    CHECK(last == first);
}

TEST_CASE("Readback rejects unsupported textures", "[command_list][gpu]") {
    ValidationGuard guard;
    auto device = makeTestDevice(guard);

    const TextureHandle unwritten = createTarget(*device, {.width = 4, .height = 4});
    auto result = device->readTexture(unwritten);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::kInvalidArgument);

    auto noTransfer = device->createTexture(
        {.size = {.width = 4, .height = 4}, .usage = TextureUsage::kRenderTarget});
    REQUIRE(noTransfer.has_value());
    result = device->readTexture(*noTransfer);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::kInvalidArgument);

    auto wide = device->createTexture(
        {.size = {.width = 4, .height = 4}, .format = Format::kRgba16Float, .usage = kTargetUsage});
    REQUIRE(wide.has_value());
    result = device->readTexture(*wide);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::kUnsupported);
}

} // namespace

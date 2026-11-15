#include <ezmu/rhi/device.hpp>
#include <ezmu/rhi/swapchain.hpp>

#include <catch2/catch_test_macros.hpp>

#include "rhi_test_support.hpp"

// Swapchains need a window surface; the ezmu_clear sample (samples.clear) covers presentation.
// Headless tests check descriptions and the argument checks that need no surface.

namespace {

using namespace ezmu::rhi;
using ezmu::foundation::ErrorCode;
using ezmu::rhi::test::makeTestDevice;
using ezmu::rhi::test::ValidationGuard;

TEST_CASE("Swapchain descriptions default to vsync, sRGB and capture", "[swapchain]") {
    constexpr SwapchainDesc kDesc{};
    STATIC_CHECK(kDesc.presentMode == PresentMode::kFifo);
    STATIC_CHECK(kDesc.srgb);
    STATIC_CHECK(kDesc.allowCapture);
    STATIC_CHECK(kDesc.pixelSize.isEmpty());
}

TEST_CASE("Present modes have stable names", "[swapchain]") {
    CHECK(toString(PresentMode::kFifo) == "Fifo");
    CHECK(toString(PresentMode::kMailbox) == "Mailbox");
    CHECK(toString(PresentMode::kImmediate) == "Immediate");
}

TEST_CASE("Swapchains need a surface and a presenting device", "[swapchain][gpu]") {
    ValidationGuard guard;
    auto device = makeTestDevice(guard); // Headless: no surface extensions.

    auto noSurface = device->createSwapchain(0, {.pixelSize = {.width = 64, .height = 64}});
    REQUIRE_FALSE(noSurface.has_value());
    CHECK(noSurface.error().code() == ErrorCode::kInvalidArgument);

    // A headless device has no VK_KHR_surface, so no surface can belong to it; the value is
    // rejected before it is used.
    auto headless = device->createSwapchain(0x1234, {.pixelSize = {.width = 64, .height = 64}});
    REQUIRE_FALSE(headless.has_value());
    CHECK(headless.error().code() == ErrorCode::kUnsupported);
}

} // namespace

#include <ezmu/platform/platform.hpp>

#include <catch2/catch_test_macros.hpp>

#include "test_support.hpp"

namespace {

using namespace ezmu::platform;
using ezmu::foundation::ErrorCode;
using ezmu::foundation::Extent2D;
using ezmu::platform::test::makeHeadlessPlatform;
using ezmu::platform::test::makeWindow;

TEST_CASE("A headless window reports its id and sizes", "[window]") {
    auto platform = makeHeadlessPlatform();
    auto window = makeWindow(*platform);

    CHECK(window->id() != WindowId::kNone);
    CHECK(window->logicalSize() == Extent2D{.width = 320, .height = 200});
    CHECK_FALSE(window->pixelSize().isEmpty());
    CHECK(window->displayScale() > 0.0F);
    CHECK_FALSE(window->isMinimized());
    CHECK(window->mode() == WindowMode::kWindowed);
}

TEST_CASE("Two windows have distinct ids", "[window]") {
    auto platform = makeHeadlessPlatform();
    auto first = makeWindow(*platform);
    auto second = makeWindow(*platform);
    CHECK(first->id() != second->id());
}

TEST_CASE("A window rejects an empty size", "[window]") {
    auto platform = makeHeadlessPlatform();
    auto window = platform->createWindow(WindowDesc{.logicalSize = {.width = 0, .height = 100}});
    REQUIRE_FALSE(window.has_value());
    CHECK(window.error().code() == ErrorCode::kInvalidArgument);
}

TEST_CASE("A window can change title, size and mode", "[window]") {
    auto platform = makeHeadlessPlatform();
    auto window = makeWindow(*platform);

    window->setTitle("ação — 日本");
    REQUIRE(window->setLogicalSize(Extent2D{.width = 640, .height = 480}).has_value());
    CHECK(window->logicalSize() == Extent2D{.width = 640, .height = 480});
    CHECK_FALSE(window->setLogicalSize(Extent2D{}).has_value());

    REQUIRE(window->setMode(WindowMode::kWindowed).has_value());
    CHECK(window->mode() == WindowMode::kWindowed);
}

TEST_CASE("Text input is off by default and can be toggled", "[window][text]") {
    auto platform = makeHeadlessPlatform();
    auto window = makeWindow(*platform);

    CHECK_FALSE(window->isTextInputActive());
    window->startTextInput();
    window->setTextInputArea(
        {.offset = {.x = 10, .y = 20}, .extent = {.width = 200, .height = 24}});
    CHECK(window->isTextInputActive());
    window->stopTextInput();
    CHECK_FALSE(window->isTextInputActive());
}

TEST_CASE("Headless windows cannot host a Vulkan surface", "[window][vulkan]") {
    auto platform = makeHeadlessPlatform();
    auto window = makeWindow(*platform);

    CHECK(window->vulkanInstanceExtensions().empty());
    CHECK(window->vulkanGetInstanceProcAddr() == nullptr);
    int fakeInstance = 0;
    auto surface = window->createVulkanSurface(&fakeInstance);
    REQUIRE_FALSE(surface.has_value());
    CHECK(surface.error().code() == ErrorCode::kUnsupported);
}

} // namespace

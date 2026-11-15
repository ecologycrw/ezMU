#pragma once

// Shared helpers for platform tests. Tests always use the headless backend so they run in CI
// without a display.

#include <ezmu/platform/platform.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace ezmu::platform::test {

inline std::unique_ptr<Platform> makeHeadlessPlatform() {
    auto platform = Platform::create(
        PlatformDesc{.appName = "ezMU tests", .videoBackend = VideoBackend::kHeadless});
    if (!platform) {
        FAIL("headless Platform::create failed: " << platform.error().message());
    }
    return std::move(*platform);
}

inline std::unique_ptr<Window> makeWindow(Platform& platform) {
    auto window = platform.createWindow(
        WindowDesc{.title = "test window", .logicalSize = {.width = 320, .height = 200}});
    if (!window) {
        FAIL("headless createWindow failed: " << window.error().message());
    }
    return std::move(*window);
}

/// Copies every event of type T from `events`.
template <typename T>
std::vector<T> eventsOf(std::span<const Event> events) {
    std::vector<T> result;
    for (const Event& event : events) {
        if (const T* typed = std::get_if<T>(&event)) {
            result.push_back(*typed);
        }
    }
    return result;
}

} // namespace ezmu::platform::test

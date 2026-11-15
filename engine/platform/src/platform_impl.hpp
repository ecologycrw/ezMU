#pragma once

// Private implementation details shared by the platform sources. Never included from public
// headers.

#include <ezmu/foundation/result.hpp>
#include <ezmu/platform/platform.hpp>
#include <ezmu/platform/window.hpp>

#include <SDL3/SDL.h>

#include <string_view>
#include <thread>
#include <vector>

namespace ezmu::platform {

struct Platform::Impl {
    VideoBackend backend = VideoBackend::kDefault;
    std::thread::id mainThread;
    std::vector<Event> events;   ///< Returned by pollEvents(); reused between calls.
    std::vector<Event> injected; ///< Pending injected events; reused between calls.
    Clipboard clipboard;
    Clock clock;
    int windowCount = 0;
};

struct Window::Impl {
    Platform::Impl* platform = nullptr;
    SDL_Window* window = nullptr;
    bool vulkan = false;
    WindowMode mode = WindowMode::kWindowed;
};

namespace detail {

/// Error carrying SDL_GetError() for the failed operation `what`.
[[nodiscard]] foundation::Error sdlError(std::string_view what);

void assertMainThread(const Platform::Impl& platform) noexcept;

/// Translates one SDL event into zero or more ezMU events appended to `out`.
void translateEvent(const SDL_Event& event, std::vector<Event>& out);

[[nodiscard]] KeyMod translateKeyMod(SDL_Keymod mod) noexcept;

} // namespace detail

} // namespace ezmu::platform

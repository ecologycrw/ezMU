#include <ezmu/foundation/assert.hpp>

#include <algorithm>
#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <utility>

#include "platform_impl.hpp"

namespace ezmu::platform {

namespace detail {

foundation::Error sdlError(std::string_view what) {
    return {foundation::ErrorCode::kInternal, std::format("{}: {}", what, SDL_GetError())};
}

void assertMainThread(const Platform::Impl& platform) noexcept {
    EZMU_ASSERT_MSG(std::this_thread::get_id() == platform.mainThread,
                    "ezmu::platform must be used from the thread that created the Platform");
    static_cast<void>(platform);
}

} // namespace detail

namespace {

constexpr SDL_InitFlags kSubsystems = SDL_INIT_VIDEO | SDL_INIT_EVENTS;

/// Moves pending injected events into the output buffer (both keep their capacity).
void beginPoll(Platform::Impl& impl) {
    impl.events.clear();
    impl.events.insert(impl.events.end(), impl.injected.begin(), impl.injected.end());
    impl.injected.clear();
}

void drainOsEvents(Platform::Impl& impl) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        detail::translateEvent(event, impl.events);
    }
}

} // namespace

foundation::Result<std::unique_ptr<Platform>> Platform::create(const PlatformDesc& desc) {
    if (SDL_WasInit(kSubsystems) != 0) {
        return foundation::makeError(foundation::ErrorCode::kInvalidArgument,
                                     "Platform::create: a Platform already exists (SDL is "
                                     "process-global, only one Platform may exist at a time)");
    }

    const std::string appName(desc.appName);
    static_cast<void>(SDL_SetAppMetadata(appName.c_str(), nullptr, nullptr));
    if (desc.videoBackend == VideoBackend::kHeadless) {
        static_cast<void>(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen,dummy"));
    } else {
        static_cast<void>(SDL_ResetHint(SDL_HINT_VIDEO_DRIVER));
    }

    if (!SDL_Init(kSubsystems)) {
        auto error = detail::sdlError("Platform::create: SDL_Init failed");
        static_cast<void>(SDL_ResetHint(SDL_HINT_VIDEO_DRIVER));
        return std::unexpected(std::move(error));
    }

    auto impl = std::make_unique<Impl>();
    impl->backend = desc.videoBackend;
    impl->mainThread = std::this_thread::get_id();
    // NOLINTNEXTLINE(modernize-make-unique): the constructor is private.
    return std::unique_ptr<Platform>(new Platform(std::move(impl)));
}

Platform::Platform(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {}

Platform::~Platform() {
    detail::assertMainThread(*m_impl);
    EZMU_ASSERT_MSG(m_impl->windowCount == 0, "Destroy every Window before its Platform");
    SDL_Quit();
    static_cast<void>(SDL_ResetHint(SDL_HINT_VIDEO_DRIVER));
}

VideoBackend Platform::videoBackend() const noexcept {
    return m_impl->backend;
}

foundation::Result<std::unique_ptr<Window>> Platform::createWindow(const WindowDesc& desc) {
    detail::assertMainThread(*m_impl);
    constexpr auto kMaxSize = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
    if (desc.logicalSize.isEmpty() || desc.logicalSize.width > kMaxSize ||
        desc.logicalSize.height > kMaxSize) {
        return foundation::makeError(foundation::ErrorCode::kInvalidArgument,
                                     "Platform::createWindow: invalid window size");
    }

    const bool vulkan = desc.vulkan && m_impl->backend != VideoBackend::kHeadless;
    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (desc.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (vulkan) {
        flags |= SDL_WINDOW_VULKAN;
    }

    SDL_Window* native =
        SDL_CreateWindow(desc.title.c_str(), static_cast<int>(desc.logicalSize.width),
                         static_cast<int>(desc.logicalSize.height), flags);
    if (native == nullptr) {
        return std::unexpected(detail::sdlError("Platform::createWindow: SDL_CreateWindow failed"));
    }

    auto impl = std::make_unique<Window::Impl>();
    impl->platform = m_impl.get();
    impl->window = native;
    impl->vulkan = vulkan;
    ++m_impl->windowCount;
    // NOLINTNEXTLINE(modernize-make-unique): the constructor is private.
    std::unique_ptr<Window> window(new Window(std::move(impl)));

    if (desc.mode != WindowMode::kWindowed) {
        if (auto result = window->setMode(desc.mode); !result) {
            return std::unexpected(std::move(result.error()));
        }
    }
    return window;
}

std::span<const Event> Platform::pollEvents() {
    detail::assertMainThread(*m_impl);
    beginPoll(*m_impl);
    drainOsEvents(*m_impl);
    return m_impl->events;
}

std::span<const Event> Platform::waitEvents(std::chrono::milliseconds timeout) {
    detail::assertMainThread(*m_impl);
    beginPoll(*m_impl);
    if (m_impl->events.empty()) {
        const auto clamped = std::clamp<std::chrono::milliseconds::rep>(
            timeout.count(), 0, std::numeric_limits<std::int32_t>::max());
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, static_cast<std::int32_t>(clamped))) {
            detail::translateEvent(event, m_impl->events);
        }
    }
    drainOsEvents(*m_impl);
    return m_impl->events;
}

void Platform::injectEvent(const Event& event) {
    detail::assertMainThread(*m_impl);
    m_impl->injected.push_back(event);
}

Clipboard& Platform::clipboard() noexcept {
    return m_impl->clipboard;
}

const Clock& Platform::clock() const noexcept {
    return m_impl->clock;
}

} // namespace ezmu::platform

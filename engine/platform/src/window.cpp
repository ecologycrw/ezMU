#include <SDL3/SDL_vulkan.h>

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

#include "platform_impl.hpp"

namespace ezmu::platform {

namespace {

foundation::Extent2D extentOf(int width, int height) noexcept {
    return foundation::Extent2D{.width = static_cast<std::uint32_t>(width > 0 ? width : 0),
                                .height = static_cast<std::uint32_t>(height > 0 ? height : 0)};
}

foundation::Result<> check(bool ok, std::string_view what) {
    if (!ok) {
        return std::unexpected(detail::sdlError(what));
    }
    return {};
}

/// Raw 64-bit value of a non-dispatchable Vulkan handle (a pointer on 64-bit platforms, an
/// integer on 32-bit ones).
template <typename Handle>
std::uint64_t toRawHandle(Handle handle) noexcept {
    if constexpr (std::is_pointer_v<Handle>) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): opaque handle interop.
        return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(handle));
    } else {
        return static_cast<std::uint64_t>(handle);
    }
}

} // namespace

Window::Window(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {}

Window::~Window() {
    detail::assertMainThread(*m_impl->platform);
    SDL_DestroyWindow(m_impl->window);
    --m_impl->platform->windowCount;
}

WindowId Window::id() const noexcept {
    return static_cast<WindowId>(SDL_GetWindowID(m_impl->window));
}

foundation::Extent2D Window::logicalSize() const noexcept {
    int width = 0;
    int height = 0;
    static_cast<void>(SDL_GetWindowSize(m_impl->window, &width, &height));
    return extentOf(width, height);
}

foundation::Extent2D Window::pixelSize() const noexcept {
    int width = 0;
    int height = 0;
    static_cast<void>(SDL_GetWindowSizeInPixels(m_impl->window, &width, &height));
    return extentOf(width, height);
}

float Window::displayScale() const noexcept {
    const float scale = SDL_GetWindowDisplayScale(m_impl->window);
    return scale > 0.0F ? scale : 1.0F;
}

bool Window::isMinimized() const noexcept {
    return (SDL_GetWindowFlags(m_impl->window) & SDL_WINDOW_MINIMIZED) != 0;
}

WindowMode Window::mode() const noexcept {
    return m_impl->mode;
}

void Window::setTitle(std::string_view utf8) {
    detail::assertMainThread(*m_impl->platform);
    const std::string title(utf8);
    static_cast<void>(SDL_SetWindowTitle(m_impl->window, title.c_str()));
}

foundation::Result<> Window::setMode(WindowMode mode) {
    detail::assertMainThread(*m_impl->platform);
    SDL_Window* window = m_impl->window;
    foundation::Result<> result;
    switch (mode) {
    case WindowMode::kWindowed:
        result = check(SDL_SetWindowFullscreen(window, false), "Window::setMode(windowed)");
        break;
    case WindowMode::kBorderlessFullscreen:
        result = check(SDL_SetWindowFullscreenMode(window, nullptr) &&
                           SDL_SetWindowFullscreen(window, true),
                       "Window::setMode(borderless)");
        break;
    case WindowMode::kExclusiveFullscreen: {
        const auto size = logicalSize();
        SDL_DisplayMode displayMode{};
        result = check(SDL_GetClosestFullscreenDisplayMode(
                           SDL_GetDisplayForWindow(window), static_cast<int>(size.width),
                           static_cast<int>(size.height), 0.0F, true, &displayMode) &&
                           SDL_SetWindowFullscreenMode(window, &displayMode) &&
                           SDL_SetWindowFullscreen(window, true),
                       "Window::setMode(exclusive)");
        break;
    }
    }
    if (result) {
        m_impl->mode = mode;
    }
    return result;
}

foundation::Result<> Window::setLogicalSize(foundation::Extent2D size) {
    detail::assertMainThread(*m_impl->platform);
    if (size.isEmpty()) {
        return foundation::makeError(foundation::ErrorCode::kInvalidArgument,
                                     "Window::setLogicalSize: empty size");
    }
    return check(SDL_SetWindowSize(m_impl->window, static_cast<int>(size.width),
                                   static_cast<int>(size.height)),
                 "Window::setLogicalSize");
}

void Window::startTextInput() {
    detail::assertMainThread(*m_impl->platform);
    static_cast<void>(SDL_StartTextInput(m_impl->window));
}

void Window::stopTextInput() {
    detail::assertMainThread(*m_impl->platform);
    static_cast<void>(SDL_StopTextInput(m_impl->window));
}

bool Window::isTextInputActive() const noexcept {
    return SDL_TextInputActive(m_impl->window);
}

void Window::setTextInputArea(const foundation::Rect2D& area, std::int32_t cursorOffset) {
    detail::assertMainThread(*m_impl->platform);
    const SDL_Rect rect{.x = area.offset.x,
                        .y = area.offset.y,
                        .w = static_cast<int>(area.extent.width),
                        .h = static_cast<int>(area.extent.height)};
    static_cast<void>(SDL_SetTextInputArea(m_impl->window, &rect, cursorOffset));
}

std::span<const char* const> Window::vulkanInstanceExtensions() const {
    if (!m_impl->vulkan) {
        return {};
    }
    Uint32 count = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&count);
    if (extensions == nullptr) {
        return {};
    }
    return {extensions, count};
}

foundation::Result<std::uint64_t> Window::createVulkanSurface(void* vkInstance) const {
    detail::assertMainThread(*m_impl->platform);
    if (!m_impl->vulkan) {
        return foundation::makeError(foundation::ErrorCode::kUnsupported,
                                     "Window::createVulkanSurface: not a Vulkan window "
                                     "(headless backend or WindowDesc::vulkan = false)");
    }
    if (vkInstance == nullptr) {
        return foundation::makeError(foundation::ErrorCode::kInvalidArgument,
                                     "Window::createVulkanSurface: null instance");
    }
    VkSurfaceKHR surface{};
    if (!SDL_Vulkan_CreateSurface(m_impl->window, static_cast<VkInstance>(vkInstance), nullptr,
                                  &surface)) {
        return std::unexpected(detail::sdlError("Window::createVulkanSurface"));
    }
    return toRawHandle(surface);
}

VulkanVoidFunction Window::vulkanGetInstanceProcAddr() const noexcept {
    if (!m_impl->vulkan) {
        return nullptr;
    }
    return SDL_Vulkan_GetVkGetInstanceProcAddr();
}

} // namespace ezmu::platform

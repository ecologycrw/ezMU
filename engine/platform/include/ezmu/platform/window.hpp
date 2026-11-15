#pragma once

#include <ezmu/foundation/geometry.hpp>
#include <ezmu/foundation/result.hpp>
#include <ezmu/platform/event.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace ezmu::platform {

class Platform;

enum class WindowMode : std::uint8_t {
    kWindowed,
    kBorderlessFullscreen, ///< Desktop resolution, no mode switch.
    kExclusiveFullscreen,  ///< Closest display mode to the window size.
};

struct WindowDesc {
    std::string title = "ezMU";
    foundation::Extent2D logicalSize{.width = 1280, .height = 720};
    WindowMode mode = WindowMode::kWindowed;
    bool resizable = true;
    /// The window will host a Vulkan swapchain. Ignored by the headless backend.
    bool vulkan = true;
};

/// Opaque function pointer type used to pass Vulkan entry points across the module boundary.
using VulkanVoidFunction = void (*)();

/// A native window, created by Platform::createWindow.
///
/// Lifetime: destroy every Window before its Platform, and after any Vulkan surface created for
/// it (the RHI swapchain owns the surface). Thread-safety: main thread only.
class Window {
public:
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;
    ~Window();

    [[nodiscard]] WindowId id() const noexcept;
    /// Size in logical units (what the OS reports as the window size).
    [[nodiscard]] foundation::Extent2D logicalSize() const noexcept;
    /// Size of the drawable area in pixels. Use this for the swapchain.
    [[nodiscard]] foundation::Extent2D pixelSize() const noexcept;
    /// Content scale of the display the window is on (e.g. 1.5 at 150 %).
    [[nodiscard]] float displayScale() const noexcept;
    [[nodiscard]] bool isMinimized() const noexcept;
    [[nodiscard]] WindowMode mode() const noexcept;

    void setTitle(std::string_view utf8);
    [[nodiscard]] foundation::Result<> setMode(WindowMode mode);
    [[nodiscard]] foundation::Result<> setLogicalSize(foundation::Extent2D size);

    /// Text input (IME) is off by default so key presses do not open IME candidate windows.
    /// Start it while a text field has focus.
    void startTextInput();
    void stopTextInput();
    [[nodiscard]] bool isTextInputActive() const noexcept;
    /// Where the IME should place its candidate window: the text field area (logical units).
    void setTextInputArea(const foundation::Rect2D& area, std::int32_t cursorOffset = 0);

    // --- Vulkan integration (ADR 0007): no Vulkan or SDL types cross this boundary. ---

    /// Instance extensions needed to present to this window (static lifetime strings). Empty for
    /// non-Vulkan or headless windows.
    [[nodiscard]] std::span<const char* const> vulkanInstanceExtensions() const;

    /// Creates a VkSurfaceKHR for `vkInstance` (a VkInstance as an opaque pointer) and returns
    /// its raw 64-bit handle. The caller owns the surface (the RHI swapchain destroys it).
    /// Fails with kUnsupported for non-Vulkan or headless windows.
    [[nodiscard]] foundation::Result<std::uint64_t> createVulkanSurface(void* vkInstance) const;

    /// vkGetInstanceProcAddr from the Vulkan library loaded for this window, so the RHI uses the
    /// same library. nullptr for non-Vulkan or headless windows.
    [[nodiscard]] VulkanVoidFunction vulkanGetInstanceProcAddr() const noexcept;

    struct Impl;

private:
    friend class Platform;
    explicit Window(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace ezmu::platform

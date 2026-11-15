#pragma once

#include <ezmu/foundation/geometry.hpp>
#include <ezmu/foundation/result.hpp>
#include <ezmu/rhi/handles.hpp>
#include <ezmu/rhi/resources.hpp>

#include <cstdint>
#include <memory>
#include <string_view>

// Presentation to a window surface. Spec: docs/specs/rhi.md ("swapchain.hpp, frame.hpp").

namespace ezmu::rhi {

class Device;
class Frame;

enum class PresentMode : std::uint8_t {
    kFifo,      ///< Vsync, never tears. Always supported; the default.
    kMailbox,   ///< Vsync with the newest image replacing queued ones (low latency).
    kImmediate, ///< No vsync; may tear.
};

/// Stable name of a present mode (e.g. "Fifo").
[[nodiscard]] std::string_view toString(PresentMode mode) noexcept;

struct SwapchainDesc {
    /// Size in pixels (from platform::Window::pixelSize()). Used when the surface does not
    /// dictate its own size (e.g. Wayland); an empty size means "nothing to present".
    foundation::Extent2D pixelSize{};
    /// Requested mode; falls back to kFifo when the surface does not support it.
    PresentMode presentMode = PresentMode::kFifo;
    /// Prefer *Srgb surface formats (the GPU encodes linear colors on write).
    bool srgb = true;
    /// Adds transfer-source usage to the images (frame capture) when the surface supports it.
    bool allowCapture = true;
    std::string_view debugName{};
};

/// Swapchain on a window surface, created by Device::createSwapchain(). Its images are textures
/// owned by the swapchain: render to the handle acquire() returns, transition it to
/// ResourceState::kPresent, and Device::endFrame() presents it. Never Device::destroy() them.
///
/// Recreated internally after resize(), VK_ERROR_OUT_OF_DATE_KHR or VK_SUBOPTIMAL_KHR; image
/// handles from before a recreation are stale. Recreation waits for the queue to go idle (rare,
/// not per frame).
///
/// Lifetime: the Device and the window must outlive the swapchain. Destroying it waits for the
/// GPU, then destroys its images and the surface. Must not be destroyed while a frame that
/// acquired it is being recorded.
///
/// Thread-safety: same thread as the owning Device (the render thread).
class Swapchain {
public:
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    Swapchain(Swapchain&&) = delete;
    Swapchain& operator=(Swapchain&&) = delete;
    ~Swapchain();

    /// Records the new pixel size (e.g. after platform's window-resized event); the swapchain is
    /// recreated on the next acquire(). An empty size skips presentation (minimized window).
    void resize(foundation::Extent2D pixelSize);

    /// Acquires the next image for `frame` (at most once per frame) and returns its texture, in
    /// ResourceState::kUndefined with undefined contents. Returns an invalid handle when this
    /// frame must not present (minimized window, empty size, swapchain out of date); the frame
    /// is still ended normally. Fails (kInternal) when the surface or device is lost. May block
    /// until an image is available (vsync).
    [[nodiscard]] foundation::Result<TextureHandle> acquire(Frame& frame);

    /// Format of the images (kBgra8Srgb, kRgba8Srgb, kBgra8Unorm or kRgba8Unorm).
    [[nodiscard]] Format format() const noexcept;
    /// Size of the images; empty while there is nothing to present.
    [[nodiscard]] foundation::Extent2D extent() const noexcept;
    /// Present mode in use (after the kFifo fallback).
    [[nodiscard]] PresentMode presentMode() const noexcept;
    /// Usage of the images: kRenderTarget, plus kTransferSrc when capture is allowed and
    /// supported.
    [[nodiscard]] TextureUsage imageUsage() const noexcept;

    struct Impl;

private:
    friend class Device;
    explicit Swapchain(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace ezmu::rhi

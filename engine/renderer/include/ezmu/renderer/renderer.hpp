#pragma once

#include <ezmu/foundation/result.hpp>
#include <ezmu/renderer/feedback.hpp>
#include <ezmu/renderer/snapshot.hpp>
#include <ezmu/rhi/handles.hpp>
#include <ezmu/rhi/resources.hpp>
#include <ezmu/rhi/swapchain.hpp>

#include <memory>
#include <optional>

// The renderer (docs/specs/renderer.md, part 1): consumes render snapshots on the render thread.
// The only renderer header that names rhi types.

namespace ezmu::foundation {
class Logger;
}

namespace ezmu::rhi {
class Device;
}

namespace ezmu::renderer {

struct RendererDesc {
    rhi::Device* device = nullptr; ///< Required. Must outlive the Renderer.
    /// Window output. Null: render into an offscreen texture of OutputState::pixelSize
    /// (headless runs and tests). The Renderer owns and destroys it.
    std::unique_ptr<rhi::Swapchain> swapchain{};
    /// Offscreen target format: an 8-bit color format (kRgba8*, kBgra8*). Ignored with a
    /// swapchain.
    rhi::Format offscreenFormat = rhi::Format::kRgba8Srgb;
    foundation::Logger* logger = nullptr; ///< Optional; must outlive the Renderer.
};

struct FrameResult {
    FrameReport report{};
    std::optional<CaptureResult> capture{}; ///< Set when a requested capture completed.
};

/// Records and submits the GPU work for one snapshot per call.
///
/// Thread-safety: same contract as rhi::Device — created on the thread that owns the device at
/// that moment, then used and destroyed only on the device's (render) thread.
/// Lifetime: destroy before the Device; the destructor waits for the GPU and destroys the
/// swapchain (and its surface) and every renderer resource.
/// Allocation: none per frame in steady state; output size changes and captures allocate.
class Renderer {
public:
    /// Fails with kInvalidArgument for a null device or an offscreen format that is not an
    /// 8-bit color format.
    [[nodiscard]] static foundation::Result<std::unique_ptr<Renderer>> create(RendererDesc desc);

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;
    ~Renderer();

    /// Renders `snapshot`: applies the output state (swapchain resize / offscreen target
    /// recreation), clears, copies the output for a capture, ends the frame and presents.
    /// Skips GPU work when the output is empty or not presentable (report.presented = false).
    ///
    /// Captures: a capture requested on a frame that is not rendered (skipped, swapchain out of
    /// date) is carried over to the next rendered frame; a newer request replaces a pending one
    /// (the replaced id gets no result). Capture problems (unsupported format or usage, buffer
    /// allocation) are reported in the CaptureResult, not as errors of render().
    ///
    /// Errors are fatal for the renderer (device or surface lost, out of memory for the target):
    /// stop calling render() and destroy it.
    [[nodiscard]] foundation::Result<FrameResult> render(const RenderSnapshot& snapshot);

    /// Fails the pending capture, if any, with kUnsupported and returns it (call before
    /// destroying the renderer so the requester gets an answer).
    [[nodiscard]] std::optional<CaptureResult> cancelPendingCapture();

    /// Offscreen mode only: the current target (kRenderTarget | kTransferSrc usage, recreated
    /// when the output size changes), for tests. Invalid before the first rendered frame and in
    /// swapchain mode.
    [[nodiscard]] rhi::TextureHandle offscreenTarget() const noexcept;

private:
    struct Impl;
    explicit Renderer(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace ezmu::renderer

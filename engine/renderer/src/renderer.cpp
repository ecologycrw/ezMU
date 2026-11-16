#include <ezmu/foundation/image.hpp>
#include <ezmu/foundation/log.hpp>
#include <ezmu/renderer/renderer.hpp>
#include <ezmu/rhi/command_list.hpp>
#include <ezmu/rhi/device.hpp>
#include <ezmu/rhi/frame.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Part 1 rendering is the M1 ezmu_clear render loop moved into the module (renderer spec,
// "Implementation notes"): begin frame, acquire (or use the offscreen target), clear with
// dynamic rendering, optional capture copy into a readback buffer, present, end frame.

namespace ezmu::renderer {

namespace {

constexpr std::string_view kCategory = "renderer";

[[nodiscard]] constexpr bool isCapturable8BitColor(rhi::Format format) noexcept {
    return format == rhi::Format::kRgba8Unorm || format == rhi::Format::kRgba8Srgb ||
           format == rhi::Format::kBgra8Unorm || format == rhi::Format::kBgra8Srgb;
}

[[nodiscard]] constexpr bool isBgra(rhi::Format format) noexcept {
    return format == rhi::Format::kBgra8Unorm || format == rhi::Format::kBgra8Srgb;
}

[[nodiscard]] constexpr bool isSrgb(rhi::Format format) noexcept {
    return format == rhi::Format::kRgba8Srgb || format == rhi::Format::kBgra8Srgb;
}

[[nodiscard]] CaptureResult failedCapture(std::uint64_t id, foundation::ErrorCode code,
                                          std::string message) {
    return CaptureResult{.captureId = id, .image = foundation::makeError(code, std::move(message))};
}

/// What one frame draws into.
struct FrameOutput {
    rhi::TextureHandle image{};
    rhi::Format format = rhi::Format::kUndefined;
    foundation::Extent2D extent{};
    bool canCapture = false;
};

} // namespace

struct Renderer::Impl {
    rhi::Device* device = nullptr;
    std::unique_ptr<rhi::Swapchain> swapchain;
    rhi::Format offscreenFormat = rhi::Format::kRgba8Srgb;
    foundation::Logger* logger = nullptr;

    foundation::Extent2D swapchainSize{}; ///< Last size passed to Swapchain::resize().
    rhi::TextureHandle target{};          ///< Offscreen mode only.
    foundation::Extent2D targetSize{};

    rhi::BufferHandle captureBuffer{};
    std::uint64_t captureBufferSize = 0;
    std::uint64_t pendingCapture = 0; ///< Requested capture id not delivered yet.

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    ~Impl() {
        device->waitIdle();
        device->destroy(captureBuffer);
        device->destroy(target);
        // The swapchain waits for the GPU and destroys its surface; the window must still exist.
        swapchain.reset();
        device->waitIdle(); // Frees the deferred destructions.
    }

    /// Resizes the swapchain or (re)creates the offscreen target for `size` (not empty).
    [[nodiscard]] foundation::Result<> applyOutput(foundation::Extent2D size) {
        if (swapchain) {
            if (size != swapchainSize) {
                swapchain->resize(size);
                swapchainSize = size;
            }
            return {};
        }
        if (target.isValid() && size == targetSize) {
            return {};
        }
        device->destroy(target); // Deferred: frames in flight may still use it.
        target = {};
        auto created = device->createTexture(
            {.size = size,
             .format = offscreenFormat,
             .usage = rhi::TextureUsage::kRenderTarget | rhi::TextureUsage::kTransferSrc,
             .debugName = "renderer offscreen target"});
        if (!created) {
            return std::unexpected(std::move(created.error()));
        }
        target = *created;
        targetSize = size;
        if (logger != nullptr) {
            logger->log(foundation::LogLevel::kDebug, kCategory, "offscreen target {}x{} {}",
                        size.width, size.height, rhi::toString(offscreenFormat));
        }
        return {};
    }

    /// The image this frame draws into; invalid when presentation must be skipped.
    [[nodiscard]] foundation::Result<FrameOutput> acquire(rhi::Frame& frame) {
        if (!swapchain) {
            return FrameOutput{.image = target,
                               .format = offscreenFormat,
                               .extent = targetSize,
                               .canCapture = true};
        }
        auto image = swapchain->acquire(frame);
        if (!image) {
            return std::unexpected(std::move(image.error()));
        }
        return FrameOutput{
            .image = *image,
            .format = swapchain->format(),
            .extent = swapchain->extent(),
            .canCapture = rhi::hasAny(swapchain->imageUsage(), rhi::TextureUsage::kTransferSrc)};
    }

    /// Records the capture copy of `output` (in kRenderTarget) and leaves it in kTransferSrc.
    /// Returns a failed capture instead when the capture cannot be made.
    [[nodiscard]] std::optional<CaptureResult> recordCapture(rhi::CommandList& cmd,
                                                             const FrameOutput& output) {
        if (!output.canCapture) {
            return failedCapture(pendingCapture, foundation::ErrorCode::kUnsupported,
                                 "capture: the surface does not support transfer-source "
                                 "swapchain images");
        }
        if (!isCapturable8BitColor(output.format)) {
            return failedCapture(pendingCapture, foundation::ErrorCode::kUnsupported,
                                 std::format("capture: format {} is not an 8-bit color format",
                                             rhi::toString(output.format)));
        }
        const std::uint64_t size = output.extent.area() * rhi::bytesPerPixel(output.format);
        if (!captureBuffer.isValid() || size != captureBufferSize) {
            device->destroy(captureBuffer); // Deferred: an earlier frame may still use it.
            captureBuffer = {};
            auto buffer = device->createBuffer({.size = size,
                                                .location = rhi::MemoryLocation::kReadback,
                                                .debugName = "renderer capture"});
            if (!buffer) {
                return CaptureResult{.captureId = pendingCapture,
                                     .image = std::unexpected(std::move(buffer.error()))};
            }
            captureBuffer = *buffer;
            captureBufferSize = size;
        }
        cmd.transition(output.image, rhi::ResourceState::kTransferSrc);
        cmd.copyTextureToBuffer(output.image, captureBuffer);
        return std::nullopt;
    }

    /// Reads the capture copy of a completed frame. Waits for the GPU (captures only).
    [[nodiscard]] CaptureResult readCapture(const FrameOutput& output) {
        device->waitIdle();
        const std::span<const std::byte> data = device->mappedData(captureBuffer);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(captureBufferSize));
        std::ranges::transform(data.first(bytes.size()), bytes.begin(),
                               [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
        auto image = foundation::Image::fromBytes(output.extent, std::move(bytes));
        if (image && isBgra(output.format)) {
            for (std::uint32_t y = 0; y < image->height(); ++y) {
                for (std::uint32_t x = 0; x < image->width(); ++x) {
                    const foundation::Rgba8 p = image->pixel(x, y);
                    image->setPixel(x, y, {.r = p.b, .g = p.g, .b = p.r, .a = p.a});
                }
            }
        }
        return CaptureResult{.captureId = pendingCapture,
                             .image = std::move(image),
                             .srgbEncoded = isSrgb(output.format)};
    }

    /// Records the frame's commands into an acquired, valid `output.image`. Sets `capture` to a
    /// failed capture when one was requested but cannot be made; returns whether a capture copy
    /// was recorded.
    bool record(rhi::CommandList& cmd, const FrameOutput& output, const RenderSnapshot& snapshot,
                std::optional<CaptureResult>& capture) {
        cmd.beginDebugLabel("renderer clear");
        cmd.transition(output.image, rhi::ResourceState::kRenderTarget);
        const rhi::ColorAttachment color{.texture = output.image,
                                         .clearColor = snapshot.clearColor};
        cmd.beginRendering({.colorAttachments = {&color, 1}, .area = {}});
        cmd.endRendering();
        bool copied = false;
        if (pendingCapture != 0) {
            capture = recordCapture(cmd, output);
            copied = !capture.has_value();
            if (capture) {
                pendingCapture = 0; // Delivered as a failure.
            }
        }
        if (swapchain) {
            cmd.transition(output.image, rhi::ResourceState::kPresent);
        }
        cmd.endDebugLabel();
        return copied;
    }

    /// Renders one frame for a non-empty, presentable output.
    [[nodiscard]] foundation::Result<> renderFrame(const RenderSnapshot& snapshot,
                                                   FrameResult& result) {
        if (auto applied = applyOutput(snapshot.output.pixelSize); !applied) {
            return applied;
        }
        auto frame = device->beginFrame();
        if (!frame) {
            return std::unexpected(std::move(frame.error()));
        }
        rhi::Frame& current = **frame;
        const std::uint64_t frameIndex = current.index();
        auto output = acquire(current);
        bool captureCopied = false;
        if (output && output->image.isValid()) {
            captureCopied = record(current.commandList(), *output, snapshot, result.capture);
        }
        // The frame is ended even after an error, so the device stays consistent.
        auto ended = device->endFrame(current);
        if (!output) {
            return std::unexpected(std::move(output.error()));
        }
        if (!ended) {
            return ended;
        }
        if (!output->image.isValid()) {
            return {}; // Skipped (out of date / nothing to present); a capture stays pending.
        }
        result.report.presented = true;
        result.report.renderFrameIndex = frameIndex;
        if (captureCopied) {
            result.capture = readCapture(*output);
            pendingCapture = 0;
        }
        return {};
    }
};

Renderer::Renderer(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {}

Renderer::~Renderer() = default;

foundation::Result<std::unique_ptr<Renderer>> Renderer::create(RendererDesc desc) {
    if (desc.device == nullptr) {
        return foundation::makeError(foundation::ErrorCode::kInvalidArgument,
                                     "RendererDesc::device is required");
    }
    if (!desc.swapchain && !isCapturable8BitColor(desc.offscreenFormat)) {
        return foundation::makeError(foundation::ErrorCode::kInvalidArgument,
                                     std::format("offscreen format {} is not an 8-bit color format",
                                                 rhi::toString(desc.offscreenFormat)));
    }
    auto impl = std::make_unique<Impl>();
    impl->device = desc.device;
    impl->swapchain = std::move(desc.swapchain);
    impl->offscreenFormat = desc.offscreenFormat;
    impl->logger = desc.logger;
    // NOLINTNEXTLINE(modernize-make-unique): the constructor is private.
    return std::unique_ptr<Renderer>(new Renderer(std::move(impl)));
}

foundation::Result<FrameResult> Renderer::render(const RenderSnapshot& snapshot) {
    const auto start = std::chrono::steady_clock::now();
    FrameResult result;
    result.report.snapshotFrameIndex = snapshot.frameIndex;
    if (snapshot.captureId != 0) {
        m_impl->pendingCapture = snapshot.captureId; // A newer request replaces a pending one.
    }

    const OutputState& output = snapshot.output;
    if (!output.pixelSize.isEmpty() && output.presentable) {
        if (auto rendered = m_impl->renderFrame(snapshot, result); !rendered) {
            return std::unexpected(std::move(rendered.error()));
        }
    }
    result.report.renderCpuTime = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start);
    return result;
}

std::optional<CaptureResult> Renderer::cancelPendingCapture() {
    if (m_impl->pendingCapture == 0) {
        return std::nullopt;
    }
    return failedCapture(std::exchange(m_impl->pendingCapture, 0),
                         foundation::ErrorCode::kUnsupported,
                         "capture: the renderer stopped before the frame was rendered");
}

rhi::TextureHandle Renderer::offscreenTarget() const noexcept {
    return m_impl->target;
}

} // namespace ezmu::renderer

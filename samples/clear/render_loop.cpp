#include "render_loop.hpp"

#include <ezmu/foundation/image.hpp>
#include <ezmu/foundation/image_io.hpp>
#include <ezmu/rhi/command_list.hpp>
#include <ezmu/rhi/frame.hpp>
#include <ezmu/rhi/handles.hpp>
#include <ezmu/rhi/resources.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "mailbox.hpp"

namespace ezmu::samples::clear {

namespace {

constexpr std::string_view kCategory = "clear";

/// Copy of one swapchain image, recorded inside a frame and read once that frame completed.
/// Presented images cannot go through Device::readTexture() (it runs outside frames, after the
/// image was handed to the presentation engine), so the copy is made before kPresent.
class FrameCapture {
public:
    explicit FrameCapture(rhi::Device& device) noexcept : m_device(&device) {}
    FrameCapture(const FrameCapture&) = delete;
    FrameCapture& operator=(const FrameCapture&) = delete;
    FrameCapture(FrameCapture&&) = delete;
    FrameCapture& operator=(FrameCapture&&) = delete;
    ~FrameCapture() { m_device->destroy(m_buffer); }

    /// Records the copy of `image` (in kRenderTarget state) into the readback buffer and leaves
    /// the image in kTransferSrc. Must be called inside the frame that acquired `image`.
    [[nodiscard]] foundation::Result<> record(rhi::CommandList& cmd, rhi::TextureHandle image,
                                              foundation::Extent2D extent, rhi::Format format) {
        if (extent != m_extent || !m_buffer.isValid()) {
            m_device->destroy(m_buffer); // Deferred: an earlier frame may still use it.
            m_buffer = {};
            auto buffer =
                m_device->createBuffer({.size = extent.area() * rhi::bytesPerPixel(format),
                                        .location = rhi::MemoryLocation::kReadback,
                                        .debugName = "ezmu_clear capture"});
            if (!buffer) {
                return std::unexpected(std::move(buffer.error()));
            }
            m_buffer = *buffer;
            m_extent = extent;
        }
        m_format = format;
        cmd.transition(image, rhi::ResourceState::kTransferSrc);
        cmd.copyTextureToBuffer(image, m_buffer);
        return {};
    }

    /// Waits for the GPU (the frame with the copy must have been ended) and writes the copy as
    /// an RGBA8 PNG, values as stored (sRGB-encoded for *Srgb formats).
    [[nodiscard]] foundation::Result<> write(const std::filesystem::path& path,
                                             foundation::Logger& logger) {
        m_device->waitIdle(); // Rare (captures only), never per frame.
        const std::span<const std::byte> data = m_device->mappedData(m_buffer);
        std::vector<std::uint8_t> bytes(data.size());
        std::ranges::transform(data, bytes.begin(),
                               [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
        auto image = foundation::Image::fromBytes(m_extent, std::move(bytes));
        if (!image) {
            return std::unexpected(std::move(image.error()));
        }
        if (m_format == rhi::Format::kBgra8Unorm || m_format == rhi::Format::kBgra8Srgb) {
            for (std::uint32_t y = 0; y < image->height(); ++y) {
                for (std::uint32_t x = 0; x < image->width(); ++x) {
                    const foundation::Rgba8 p = image->pixel(x, y);
                    image->setPixel(x, y, {.r = p.b, .g = p.g, .b = p.r, .a = p.a});
                }
            }
        }
        if (auto written = foundation::writePng(path, *image); !written) {
            return written;
        }
        // tests/run_clear_test.cmake parses this line.
        logger.info(kCategory, "capture: {}x{} {} -> {}", m_extent.width, m_extent.height,
                    rhi::toString(m_format), path.string());
        return {};
    }

private:
    rhi::Device* m_device;
    rhi::BufferHandle m_buffer{};
    foundation::Extent2D m_extent{};
    rhi::Format m_format = rhi::Format::kUndefined;
};

/// Records one frame for `image` (a freshly acquired swapchain image).
[[nodiscard]] foundation::Result<> recordClear(rhi::CommandList& cmd, rhi::TextureHandle image,
                                               const rhi::Swapchain& swapchain,
                                               const RenderConfig& config, FrameCapture* capture) {
    cmd.beginDebugLabel("clear");
    cmd.transition(image, rhi::ResourceState::kRenderTarget);
    const rhi::ColorAttachment color{.texture = image, .clearColor = config.clearColor};
    cmd.beginRendering({.colorAttachments = {&color, 1}, .area = {}});
    cmd.endRendering();
    foundation::Result<> result;
    if (capture != nullptr) {
        result = capture->record(cmd, image, swapchain.extent(), swapchain.format());
    }
    cmd.transition(image, rhi::ResourceState::kPresent);
    cmd.endDebugLabel();
    return result;
}

/// Renders one frame: begin, acquire, clear (+ capture copy), end. Returns whether an image
/// was presented (false when presentation was skipped, e.g. swapchain out of date).
[[nodiscard]] foundation::Result<bool> renderFrame(rhi::Device& device, rhi::Swapchain& swapchain,
                                                   const RenderConfig& config,
                                                   FrameCapture* capture) {
    auto frame = device.beginFrame();
    if (!frame) {
        return std::unexpected(std::move(frame.error()));
    }
    auto image = swapchain.acquire(**frame);
    foundation::Result<> recorded;
    if (!image) {
        recorded = std::unexpected(std::move(image.error()));
    } else if (image->isValid()) {
        recorded = recordClear((*frame)->commandList(), *image, swapchain, config, capture);
    }
    // The frame is ended even after an error, so the device stays consistent.
    auto ended = device.endFrame(**frame);
    if (!recorded) {
        return std::unexpected(std::move(recorded.error()));
    }
    if (!ended) {
        return std::unexpected(std::move(ended.error()));
    }
    return image->isValid();
}

/// The loop itself; the caller tears down the swapchain afterwards.
[[nodiscard]] foundation::Result<> renderFrames(const std::stop_token& stop, rhi::Device& device,
                                                rhi::Swapchain& swapchain,
                                                const RenderConfig& config, Mailbox& mailbox,
                                                foundation::Logger& logger) {
    const bool canCapture = rhi::hasAny(swapchain.imageUsage(), rhi::TextureUsage::kTransferSrc);
    FrameCapture capture(device);
    std::uint64_t sizeVersion = 0;
    std::uint64_t presented = 0;
    bool captureWanted = false;

    while (config.frameLimit == 0 || presented < config.frameLimit) {
        const std::optional<MailboxSnapshot> snapshot = mailbox.take(stop);
        if (!snapshot) {
            return {};
        }
        if (snapshot->sizeVersion != sizeVersion) {
            sizeVersion = snapshot->sizeVersion;
            swapchain.resize(snapshot->pixelSize);
        }
        captureWanted = captureWanted || snapshot->captureRequested ||
                        (config.captureLastFrame && presented + 1 == config.frameLimit);
        if (captureWanted && !canCapture) {
            return foundation::makeError(
                foundation::ErrorCode::kUnsupported,
                "capture: the surface does not support transfer-source swapchain images");
        }

        auto presentedNow =
            renderFrame(device, swapchain, config, captureWanted ? &capture : nullptr);
        if (!presentedNow) {
            return std::unexpected(std::move(presentedNow.error()));
        }
        if (*presentedNow) {
            ++presented;
            if (captureWanted) {
                captureWanted = false;
                if (auto written = capture.write(config.capturePath, logger); !written) {
                    return written;
                }
            }
        }
    }
    logger.info(kCategory, "presented {} frames", presented);
    return {};
}

} // namespace

foundation::Result<> runRenderLoop(const std::stop_token& stop, rhi::Device& device,
                                   std::unique_ptr<rhi::Swapchain> swapchain,
                                   const RenderConfig& config, Mailbox& mailbox,
                                   foundation::Logger& logger) {
    // The capture buffer is destroyed inside renderFrames(), before the swapchain and surface.
    foundation::Result<> result = renderFrames(stop, device, *swapchain, config, mailbox, logger);
    device.waitIdle();
    swapchain.reset();
    device.waitIdle(); // Frees the deferred destructions.
    return result;
}

} // namespace ezmu::samples::clear

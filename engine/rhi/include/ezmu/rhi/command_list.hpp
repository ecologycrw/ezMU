#pragma once

#include <ezmu/foundation/geometry.hpp>
#include <ezmu/rhi/device.hpp>
#include <ezmu/rhi/handles.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// Command recording. Spec: docs/specs/rhi.md (barrier model: decision D4).

namespace ezmu::rhi {

/// Coarse resource states. Each maps internally to an image layout plus synchronization2 stage
/// and access masks. The RHI tracks the current state of every texture (whole image), so a
/// transition only names the target state.
enum class ResourceState : std::uint8_t {
    kUndefined,    ///< Contents undefined (new texture). Never a transition target.
    kRenderTarget, ///< Color attachment, read/write.
    kDepthWrite,   ///< Depth attachment, read/write.
    kShaderRead,   ///< Sampled by shaders.
    kTransferSrc,  ///< Source of copies.
    kTransferDst,  ///< Destination of copies.
    kPresent,      ///< Ready for presentation (swapchain images).
};

enum class LoadOp : std::uint8_t { kLoad, kClear, kDontCare };
enum class StoreOp : std::uint8_t { kStore, kDontCare };

struct ColorAttachment {
    TextureHandle texture{}; ///< Must be in ResourceState::kRenderTarget.
    LoadOp load = LoadOp::kClear;
    StoreOp store = StoreOp::kStore;
    /// Linear RGBA; for *Srgb formats the GPU encodes it when clearing.
    std::array<float, 4> clearColor{0.0F, 0.0F, 0.0F, 1.0F};
};

/// Maximum number of color attachments in one rendering pass.
inline constexpr std::size_t kMaxColorAttachments = 8;

struct RenderingDesc {
    std::span<const ColorAttachment> colorAttachments{}; ///< 1..kMaxColorAttachments.
    /// Pixels affected by load/store operations. An empty extent means the whole of the first
    /// attachment. Must lie inside every attachment.
    foundation::Rect2D area{};
};

/// Records GPU commands. Obtained from Device::executeImmediate() (and, from the frames step on,
/// from a frame); valid only while that recording lasts. Same thread as the owning Device.
///
/// Programming errors are EZMU_ASSERTs and the offending command is skipped: stale handles,
/// a transition inside a rendering pass, an attachment that is not a render target in
/// kRenderTarget state, unbalanced begin/end calls. Recording never allocates.
class CommandList {
public:
    CommandList(const CommandList&) = delete;
    CommandList& operator=(const CommandList&) = delete;
    CommandList(CommandList&&) = delete;
    CommandList& operator=(CommandList&&) = delete;
    ~CommandList() = default;

    /// Records a barrier from the texture's tracked state to `newState` and updates the tracked
    /// state. Read-only states transition to themselves as a no-op; writable states (render
    /// target, depth, transfer destination) get a barrier that orders the previous writes.
    void transition(TextureHandle texture, ResourceState newState);

    /// Starts a dynamic rendering pass (no render pass objects). Must be paired with
    /// endRendering(); passes do not nest.
    void beginRendering(const RenderingDesc& desc);
    void endRendering();

    /// Copies the whole texture (tightly packed rows, top row first) to the start of `dst`.
    /// `src` must be in kTransferSrc state and `dst` at least width × height × bytesPerPixel
    /// bytes. For a kReadback buffer the copy is made visible to the CPU once the submission
    /// has completed.
    void copyTextureToBuffer(TextureHandle src, BufferHandle dst);

    /// Named region shown by RenderDoc / Nsight (no-op without debug utils). Names longer than
    /// 127 bytes are truncated. Labels must be balanced within a recording.
    void beginDebugLabel(std::string_view name);
    void endDebugLabel();

private:
    friend struct Device::Impl;
    CommandList(Device::Impl& device, void* commandBuffer) noexcept;

    Device::Impl* m_device = nullptr;
    void* m_commandBuffer = nullptr; ///< VkCommandBuffer.
    bool m_insideRendering = false;
    std::uint32_t m_labelDepth = 0;
};

} // namespace ezmu::rhi

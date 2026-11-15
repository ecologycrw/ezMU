#pragma once

#include <ezmu/rhi/command_list.hpp>
#include <ezmu/rhi/device.hpp>

#include <cstdint>

// Frames in flight. Spec: docs/specs/rhi.md ("Frame pacing").

namespace ezmu::rhi {

/// One frame being recorded, obtained from Device::beginFrame() and valid until the matching
/// Device::endFrame(). Owned by the Device; never store the pointer across frames.
///
/// Thread-safety: same thread as the owning Device.
class Frame {
public:
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
    Frame(Frame&&) = delete;
    Frame& operator=(Frame&&) = delete;
    ~Frame() = default;

    /// Command list of this frame, submitted by Device::endFrame(). Commands recorded here run
    /// after everything submitted earlier (previous frames, executeImmediate()).
    [[nodiscard]] CommandList& commandList() noexcept { return m_commandList; }

    /// Monotonic frame counter: 0 for the first frame of the Device, then +1 per beginFrame().
    [[nodiscard]] std::uint64_t index() const noexcept { return m_index; }

    /// Frame slot, `index() % Device::framesInFlight()`. Per-slot resources (e.g. a game's
    /// upload buffers) may be indexed by it: a slot is reused only after the GPU finished it.
    [[nodiscard]] std::uint32_t slot() const noexcept { return m_slot; }

private:
    friend struct Device::Impl;
    Frame(Device::Impl& device, void* commandBuffer, std::uint32_t slot) noexcept;

    CommandList m_commandList;
    std::uint64_t m_index = 0;
    std::uint32_t m_slot = 0;
};

} // namespace ezmu::rhi

#pragma once

#include <ezmu/foundation/geometry.hpp>

#include <array>
#include <chrono>
#include <cstdint>

// Render snapshots (docs/specs/renderer.md, ADR 0012): immutable, self-contained descriptions of
// one frame, written by the game thread during extraction and read by the render thread. This
// header depends only on foundation, so producers never see rhi types.

namespace ezmu::renderer {

/// What the renderer must draw into. Written by the game thread from window events.
struct OutputState {
    foundation::Extent2D pixelSize{}; ///< Swapchain / offscreen size. Empty = nothing to draw.
    float displayScale = 1.0F;        ///< Pixels per logical unit (e.g. 1.5 at 150 %).
    bool presentable = true;          ///< false while minimized: GPU work is skipped.

    friend bool operator==(const OutputState&, const OutputState&) = default;
};

/// One frame, as seen by the render thread. Plain data, owned by a SnapshotRing slot (or by a
/// test feeding a Renderer directly).
/// Allocation: reset() keeps any allocated capacity, so a warmed-up ring does not allocate per
/// frame (part 1 has no allocating members yet).
/// Thread-safety: none of its own; the SnapshotRing hands it to one thread at a time.
struct RenderSnapshot {
    std::uint64_t frameIndex = 0;              ///< Game frame that produced it (monotonic).
    std::chrono::nanoseconds simulationTime{}; ///< Game time at extraction.
    OutputState output{};
    std::array<float, 4> clearColor{0.0F, 0.0F, 0.0F, 1.0F}; ///< Linear RGBA.
    std::uint64_t captureId = 0; ///< Non-zero: capture this frame's output.

    /// Restores every member to its default value; keeps allocated capacity.
    void reset() noexcept;
};

} // namespace ezmu::renderer

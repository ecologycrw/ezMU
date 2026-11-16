#pragma once

#include <ezmu/foundation/image.hpp>
#include <ezmu/foundation/result.hpp>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

// Render → game channel (docs/specs/renderer.md). Depends only on foundation.

namespace ezmu::renderer {

/// Outcome of one Renderer::render call. Carries both timelines (ADR 0012).
struct FrameReport {
    std::uint64_t snapshotFrameIndex = 0;     ///< Game frame rendered (timeline 1).
    std::uint64_t renderFrameIndex = 0;       ///< rhi Frame::index() (timeline 2), if presented.
    std::chrono::nanoseconds renderCpuTime{}; ///< Time spent in Renderer::render.
    /// Render thread idle, waiting for a snapshot. Measured and filled by the caller of
    /// Renderer::render (the render loop), which owns the wait; the Renderer leaves it 0.
    std::chrono::nanoseconds waitTime{};
    bool presented = false; ///< false: skipped (minimized, out of date, empty output).
};

/// A frame capture: the output of the frame whose snapshot set `captureId`.
struct CaptureResult {
    std::uint64_t captureId = 0;
    /// Tightly packed RGBA8 pixels as stored (BGRA swizzled), top row first, or why it failed.
    foundation::Result<foundation::Image> image{};
    bool srgbEncoded = false; ///< Values are sRGB-encoded (sRGB output format).
};

/// Render → game channel. The render thread pushes; the game thread drains once per frame.
/// Frame reports use a fixed ring: when the game thread falls behind, the oldest reports are
/// dropped and counted in Drained::droppedReports. The first fatal error is kept until drained.
///
/// Thread-safety: push* from the render thread, drain from the game thread (any threads work;
/// every call takes one short lock).
/// Allocation: the constructor allocates the report ring; pushReport never allocates;
/// pushCapture may (rare); drain allocates only while `out` grows to its steady-state size.
class RenderFeedbackQueue {
public:
    static constexpr std::uint32_t kDefaultReportCapacity = 8;

    /// `reportCapacity` reports are kept between drains (at least 1).
    explicit RenderFeedbackQueue(std::uint32_t reportCapacity = kDefaultReportCapacity);

    void pushReport(const FrameReport& report);
    void pushCapture(CaptureResult result);
    /// The render thread is stopping. Later fatal errors are ignored until drained.
    void pushFatalError(foundation::Error error);

    struct Drained {
        std::vector<FrameReport> reports{}; ///< Oldest first. Reuse `Drained` between drains.
        std::vector<CaptureResult> captures{};
        std::optional<foundation::Error> fatalError{};
        std::uint64_t droppedReports = 0; ///< Reports dropped since the previous drain.
    };
    /// Clears `out`, then moves everything pushed since the previous drain into it.
    void drain(Drained& out);

private:
    std::mutex m_mutex;
    std::vector<FrameReport> m_reports; ///< Ring of fixed capacity.
    std::uint32_t m_reportHead = 0;     ///< Oldest report.
    std::uint32_t m_reportCount = 0;
    std::uint64_t m_droppedReports = 0;
    std::vector<CaptureResult> m_captures;
    std::optional<foundation::Error> m_fatalError;
};

} // namespace ezmu::renderer

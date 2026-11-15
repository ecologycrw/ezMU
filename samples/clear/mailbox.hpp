#pragma once

#include <ezmu/foundation/geometry.hpp>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>

// The only channel from the main (window) thread to the render thread in ezmu_clear
// (docs/specs/rhi.md, "Threading"). "Quit" is the render thread's std::stop_token. From M2 the
// runtime replaces this with render snapshots.

namespace ezmu::samples::clear {

/// Latest window state published by the main thread, as seen by the render thread.
struct MailboxSnapshot {
    foundation::Extent2D pixelSize{};
    /// Incremented on every publishPixelSize(), so the render thread resizes only on change.
    std::uint64_t sizeVersion = 0;
    /// A capture was requested since the previous take() (consumed by take()).
    bool captureRequested = false;
};

/// Thread-safety: publish* / set* / request* from the main thread, take() from the render
/// thread. Every call locks one mutex for a few instructions; nothing allocates.
class Mailbox {
public:
    explicit Mailbox(foundation::Extent2D pixelSize) noexcept : m_pixelSize(pixelSize) {}

    void publishPixelSize(foundation::Extent2D pixelSize) {
        const std::scoped_lock lock(m_mutex);
        m_pixelSize = pixelSize;
        ++m_sizeVersion;
    }

    /// While minimized, take() blocks so the render thread does not spin on empty frames.
    void setMinimized(bool minimized) {
        {
            const std::scoped_lock lock(m_mutex);
            m_minimized = minimized;
        }
        m_wake.notify_all();
    }

    void requestCapture() {
        const std::scoped_lock lock(m_mutex);
        m_captureRequested = true;
    }

    /// Render thread: waits while the window is minimized, then returns the current state and
    /// consumes the capture request. Returns nullopt once `stop` has been requested.
    [[nodiscard]] std::optional<MailboxSnapshot> take(const std::stop_token& stop) {
        std::unique_lock lock(m_mutex);
        m_wake.wait(lock, stop, [this] { return !m_minimized; });
        if (stop.stop_requested()) {
            return std::nullopt;
        }
        return MailboxSnapshot{.pixelSize = m_pixelSize,
                               .sizeVersion = m_sizeVersion,
                               .captureRequested = std::exchange(m_captureRequested, false)};
    }

private:
    std::mutex m_mutex;
    std::condition_variable_any m_wake;
    foundation::Extent2D m_pixelSize{};
    std::uint64_t m_sizeVersion = 0;
    bool m_minimized = false;
    bool m_captureRequested = false;
};

} // namespace ezmu::samples::clear

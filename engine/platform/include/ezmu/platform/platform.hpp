#pragma once

#include <ezmu/foundation/result.hpp>
#include <ezmu/platform/clipboard.hpp>
#include <ezmu/platform/clock.hpp>
#include <ezmu/platform/event.hpp>
#include <ezmu/platform/window.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace ezmu::platform {

enum class VideoBackend : std::uint8_t {
    kDefault,  ///< Native backend (Win32; Wayland or X11 on Linux).
    kHeadless, ///< No display: windows are virtual and cannot host a Vulkan surface. For tests.
};

struct PlatformDesc {
    std::string_view appName = "ezMU"; ///< Application name / id reported to the OS.
    VideoBackend videoBackend = VideoBackend::kDefault;
};

/// Owns the OS platform layer (SDL3, ADR 0007): video and event subsystems, windows, clipboard.
///
/// SDL is process-global, so at most one Platform may exist at a time; create() fails with
/// kInvalidArgument otherwise. Thread-safety: create, use and destroy on the main thread
/// (checked by EZMU_ASSERT); Clock may be used from any thread.
class Platform {
public:
    [[nodiscard]] static foundation::Result<std::unique_ptr<Platform>>
    create(const PlatformDesc& desc = {});

    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;
    Platform(Platform&&) = delete;
    Platform& operator=(Platform&&) = delete;
    /// Every Window must have been destroyed (EZMU_ASSERT).
    ~Platform();

    [[nodiscard]] VideoBackend videoBackend() const noexcept;

    [[nodiscard]] foundation::Result<std::unique_ptr<Window>> createWindow(const WindowDesc& desc);

    /// Drains pending events (injected ones first, then OS events) without blocking. The span is
    /// valid until the next pollEvents()/waitEvents(). No allocation once the internal buffer
    /// has grown to the peak event count.
    [[nodiscard]] std::span<const Event> pollEvents();

    /// Like pollEvents(), but blocks up to `timeout` when no event is pending.
    [[nodiscard]] std::span<const Event> waitEvents(std::chrono::milliseconds timeout);

    /// Queues a synthetic event, delivered by the next poll (tests, automation, input replay).
    void injectEvent(const Event& event);

    [[nodiscard]] Clipboard& clipboard() noexcept;
    [[nodiscard]] const Clock& clock() const noexcept;

    struct Impl;

private:
    explicit Platform(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace ezmu::platform

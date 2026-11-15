#pragma once

#include <ezmu/foundation/geometry.hpp>
#include <ezmu/platform/keys.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>

// Raw platform events. Every event is a small, trivially copyable struct; `Event` is a closed
// variant of them. Timestamps use the same base as `Clock::now()`.

namespace ezmu::platform {

/// Identifies a window in events. Zero means "no window".
// NOLINTNEXTLINE(performance-enum-size): carries the full 32-bit native window id.
enum class WindowId : std::uint32_t { kNone = 0 };

/// A 2D position or delta in logical (window) units, with sub-pixel precision.
struct PointF {
    float x = 0.0F;
    float y = 0.0F;

    friend constexpr bool operator==(const PointF&, const PointF&) = default;
};

/// Maximum UTF-8 bytes carried by one text event.
inline constexpr std::size_t kMaxEventText = 64;

/// UTF-8 text stored inline, so events stay trivially copyable and polling does not allocate.
/// Always holds complete code points.
class EventText {
public:
    constexpr EventText() = default;

    /// Copies the longest prefix of `utf8` that fits and ends on a code point boundary; returns
    /// the number of bytes consumed.
    std::size_t assignPrefix(std::string_view utf8) noexcept;

    [[nodiscard]] std::string_view view() const noexcept { return {m_bytes.data(), m_size}; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }

    friend bool operator==(const EventText& a, const EventText& b) noexcept {
        return a.view() == b.view();
    }

private:
    std::array<char, kMaxEventText> m_bytes{};
    std::size_t m_size = 0;
};

struct WindowCloseRequested {
    std::chrono::nanoseconds timestamp{};
    WindowId window = WindowId::kNone;
};

/// The window changed size. Use `pixelSize` for the swapchain.
struct WindowResized {
    std::chrono::nanoseconds timestamp{};
    WindowId window = WindowId::kNone;
    foundation::Extent2D logicalSize;
    foundation::Extent2D pixelSize;
};

struct WindowMinimized {
    std::chrono::nanoseconds timestamp{};
    WindowId window = WindowId::kNone;
};

struct WindowRestored {
    std::chrono::nanoseconds timestamp{};
    WindowId window = WindowId::kNone;
};

struct WindowFocusChanged {
    std::chrono::nanoseconds timestamp{};
    WindowId window = WindowId::kNone;
    bool focused = false;
};

struct WindowDisplayScaleChanged {
    std::chrono::nanoseconds timestamp{};
    WindowId window = WindowId::kNone;
    float scale = 1.0F;
};

struct KeyDown {
    std::chrono::nanoseconds timestamp{};
    WindowId window = WindowId::kNone;
    Scancode scancode = Scancode::kUnknown;
    Keycode keycode = Keycode::kUnknown;
    KeyMod modifiers = KeyMod::kNone;
    bool repeat = false;
};

struct KeyUp {
    std::chrono::nanoseconds timestamp{};
    WindowId window = WindowId::kNone;
    Scancode scancode = Scancode::kUnknown;
    Keycode keycode = Keycode::kUnknown;
    KeyMod modifiers = KeyMod::kNone;
};

struct MouseMoved {
    std::chrono::nanoseconds timestamp{};
    WindowId window = WindowId::kNone;
    PointF position;
    PointF delta;
};

struct MouseButtonDown {
    std::chrono::nanoseconds timestamp{};
    WindowId window = WindowId::kNone;
    MouseButton button = MouseButton::kUnknown;
    PointF position;
    std::uint8_t clicks = 1; ///< 2 for a double click, etc.
};

struct MouseButtonUp {
    std::chrono::nanoseconds timestamp{};
    WindowId window = WindowId::kNone;
    MouseButton button = MouseButton::kUnknown;
    PointF position;
    std::uint8_t clicks = 1;
};

/// Wheel movement; positive y scrolls away from the user, positive x to the right.
struct MouseWheel {
    std::chrono::nanoseconds timestamp{};
    WindowId window = WindowId::kNone;
    PointF delta;
    PointF position;
};

/// Committed text (after IME composition, dead keys, etc.). Long text arrives as several
/// consecutive events, split on code point boundaries.
struct TextInput {
    std::chrono::nanoseconds timestamp{};
    WindowId window = WindowId::kNone;
    EventText text;
};

/// IME composition in progress. `cursor` and `selectionLength` are in code points (-1 when the
/// IME does not report them). Composition longer than kMaxEventText is truncated.
struct TextEditing {
    std::chrono::nanoseconds timestamp{};
    WindowId window = WindowId::kNone;
    EventText text;
    std::int32_t cursor = -1;
    std::int32_t selectionLength = -1;
};

/// The OS asked the application to quit (e.g. logout, SIGTERM, last window closed by the OS).
struct QuitRequested {
    std::chrono::nanoseconds timestamp{};
};

using Event =
    std::variant<WindowCloseRequested, WindowResized, WindowMinimized, WindowRestored,
                 WindowFocusChanged, WindowDisplayScaleChanged, KeyDown, KeyUp, MouseMoved,
                 MouseButtonDown, MouseButtonUp, MouseWheel, TextInput, TextEditing, QuitRequested>;

} // namespace ezmu::platform

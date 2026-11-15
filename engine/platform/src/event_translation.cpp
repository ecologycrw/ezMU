#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "platform_impl.hpp"

namespace ezmu::platform {

// ezMU scancodes and keycodes use the same encoding as SDL3 (USB HID usage IDs; Unicode code
// points or scancode | mask), so translation is a cast. These checks keep that true.
static_assert(static_cast<int>(SDL_SCANCODE_A) == static_cast<int>(Scancode::kA));
static_assert(static_cast<int>(SDL_SCANCODE_RETURN) == static_cast<int>(Scancode::kReturn));
static_assert(static_cast<int>(SDL_SCANCODE_KP_PERIOD) ==
              static_cast<int>(Scancode::kKeypadPeriod));
static_assert(static_cast<int>(SDL_SCANCODE_RGUI) == static_cast<int>(Scancode::kRightGui));
static_assert(SDL_SCANCODE_COUNT <= 0xFFFF);
static_assert(SDLK_SCANCODE_MASK == kKeycodeScancodeMask);
static_assert(SDLK_RETURN == static_cast<std::uint32_t>(Keycode::kReturn));
static_assert(SDLK_ESCAPE == static_cast<std::uint32_t>(Keycode::kEscape));
static_assert(SDLK_DELETE == static_cast<std::uint32_t>(Keycode::kDelete));
static_assert(SDLK_F1 == static_cast<std::uint32_t>(keycodeFromScancode(Scancode::kF1)));
static_assert(SDLK_A == static_cast<std::uint32_t>(keycodeFromChar(U'a')));

namespace {

bool isUtf8Continuation(char c) noexcept {
    return (static_cast<unsigned char>(c) & 0xC0U) == 0x80U;
}

std::chrono::nanoseconds timestampOf(const SDL_Event& event) noexcept {
    return std::chrono::nanoseconds(static_cast<std::int64_t>(event.common.timestamp));
}

WindowId windowIdOf(SDL_WindowID id) noexcept {
    return static_cast<WindowId>(id);
}

foundation::Extent2D extentOf(int width, int height) noexcept {
    return foundation::Extent2D{.width = static_cast<std::uint32_t>(std::max(width, 0)),
                                .height = static_cast<std::uint32_t>(std::max(height, 0))};
}

foundation::Extent2D queryLogicalSize(SDL_WindowID id, foundation::Extent2D fallback) noexcept {
    int width = 0;
    int height = 0;
    SDL_Window* window = SDL_GetWindowFromID(id);
    if (window == nullptr || !SDL_GetWindowSize(window, &width, &height)) {
        return fallback;
    }
    return extentOf(width, height);
}

foundation::Extent2D queryPixelSize(SDL_WindowID id, foundation::Extent2D fallback) noexcept {
    int width = 0;
    int height = 0;
    SDL_Window* window = SDL_GetWindowFromID(id);
    if (window == nullptr || !SDL_GetWindowSizeInPixels(window, &width, &height)) {
        return fallback;
    }
    return extentOf(width, height);
}

void pushResized(std::vector<Event>& out, const WindowResized& resized) {
    // SDL reports logical and pixel size changes separately; collapse identical neighbors.
    if (!out.empty()) {
        if (const auto* previous = std::get_if<WindowResized>(&out.back());
            previous != nullptr && previous->window == resized.window &&
            previous->logicalSize == resized.logicalSize &&
            previous->pixelSize == resized.pixelSize) {
            return;
        }
    }
    out.emplace_back(resized);
}

void translateWindowEvent(const SDL_Event& event, std::vector<Event>& out) {
    const auto timestamp = timestampOf(event);
    const auto& data = event.window;
    const WindowId window = windowIdOf(data.windowID);
    switch (event.type) {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        out.emplace_back(WindowCloseRequested{.timestamp = timestamp, .window = window});
        break;
    case SDL_EVENT_WINDOW_RESIZED: {
        const auto logical = extentOf(data.data1, data.data2);
        pushResized(out, WindowResized{.timestamp = timestamp,
                                       .window = window,
                                       .logicalSize = logical,
                                       .pixelSize = queryPixelSize(data.windowID, logical)});
        break;
    }
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
        const auto pixels = extentOf(data.data1, data.data2);
        pushResized(out, WindowResized{.timestamp = timestamp,
                                       .window = window,
                                       .logicalSize = queryLogicalSize(data.windowID, pixels),
                                       .pixelSize = pixels});
        break;
    }
    case SDL_EVENT_WINDOW_MINIMIZED:
        out.emplace_back(WindowMinimized{.timestamp = timestamp, .window = window});
        break;
    case SDL_EVENT_WINDOW_RESTORED:
        out.emplace_back(WindowRestored{.timestamp = timestamp, .window = window});
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        out.emplace_back(
            WindowFocusChanged{.timestamp = timestamp,
                               .window = window,
                               .focused = event.type == SDL_EVENT_WINDOW_FOCUS_GAINED});
        break;
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: {
        SDL_Window* native = SDL_GetWindowFromID(data.windowID);
        const float scale = native != nullptr ? SDL_GetWindowDisplayScale(native) : 0.0F;
        out.emplace_back(WindowDisplayScaleChanged{
            .timestamp = timestamp, .window = window, .scale = scale > 0.0F ? scale : 1.0F});
        break;
    }
    default:
        break;
    }
}

void translateTextInput(const SDL_Event& event, std::vector<Event>& out) {
    if (event.text.text == nullptr) {
        return;
    }
    std::string_view rest(event.text.text);
    while (!rest.empty()) {
        TextInput input{
            .timestamp = timestampOf(event), .window = windowIdOf(event.text.windowID), .text = {}};
        const std::size_t consumed = input.text.assignPrefix(rest);
        if (consumed == 0) {
            break;
        }
        out.emplace_back(input);
        rest.remove_prefix(consumed);
    }
}

void translateTextEditing(const SDL_Event& event, std::vector<Event>& out) {
    TextEditing editing{.timestamp = timestampOf(event),
                        .window = windowIdOf(event.edit.windowID),
                        .text = {},
                        .cursor = event.edit.start,
                        .selectionLength = event.edit.length};
    if (event.edit.text != nullptr) {
        static_cast<void>(editing.text.assignPrefix(event.edit.text));
    }
    out.emplace_back(editing);
}

PointF pointOf(float x, float y) noexcept {
    return PointF{.x = x, .y = y};
}

} // namespace

std::size_t EventText::assignPrefix(std::string_view utf8) noexcept {
    std::size_t length = std::min(utf8.size(), kMaxEventText);
    while (length > 0 && length < utf8.size() && isUtf8Continuation(*(utf8.data() + length))) {
        --length;
    }
    std::memcpy(m_bytes.data(), utf8.data(), length);
    m_size = length;
    return length;
}

namespace detail {

KeyMod translateKeyMod(SDL_Keymod mod) noexcept {
    struct Mapping {
        SDL_Keymod sdl;
        KeyMod ezmu;
    };
    static constexpr std::array kMappings{
        Mapping{.sdl = SDL_KMOD_LSHIFT, .ezmu = KeyMod::kLeftShift},
        Mapping{.sdl = SDL_KMOD_RSHIFT, .ezmu = KeyMod::kRightShift},
        Mapping{.sdl = SDL_KMOD_LCTRL, .ezmu = KeyMod::kLeftCtrl},
        Mapping{.sdl = SDL_KMOD_RCTRL, .ezmu = KeyMod::kRightCtrl},
        Mapping{.sdl = SDL_KMOD_LALT, .ezmu = KeyMod::kLeftAlt},
        Mapping{.sdl = SDL_KMOD_RALT, .ezmu = KeyMod::kRightAlt},
        Mapping{.sdl = SDL_KMOD_LGUI, .ezmu = KeyMod::kLeftGui},
        Mapping{.sdl = SDL_KMOD_RGUI, .ezmu = KeyMod::kRightGui},
        Mapping{.sdl = SDL_KMOD_NUM, .ezmu = KeyMod::kNumLock},
        Mapping{.sdl = SDL_KMOD_CAPS, .ezmu = KeyMod::kCapsLock},
        Mapping{.sdl = SDL_KMOD_SCROLL, .ezmu = KeyMod::kScrollLock},
        Mapping{.sdl = SDL_KMOD_MODE, .ezmu = KeyMod::kAltGr},
    };
    KeyMod result = KeyMod::kNone;
    for (const auto& mapping : kMappings) {
        if ((mod & mapping.sdl) != 0) {
            result = result | mapping.ezmu;
        }
    }
    return result;
}

void translateEvent(const SDL_Event& event, std::vector<Event>& out) {
    const auto timestamp = timestampOf(event);
    switch (event.type) {
    case SDL_EVENT_QUIT:
        out.emplace_back(QuitRequested{.timestamp = timestamp});
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const auto& key = event.key;
        const auto scancode = static_cast<Scancode>(static_cast<std::uint16_t>(key.scancode));
        const auto keycode = static_cast<Keycode>(key.key);
        const auto modifiers = translateKeyMod(key.mod);
        if (event.type == SDL_EVENT_KEY_DOWN) {
            out.emplace_back(KeyDown{.timestamp = timestamp,
                                     .window = windowIdOf(key.windowID),
                                     .scancode = scancode,
                                     .keycode = keycode,
                                     .modifiers = modifiers,
                                     .repeat = key.repeat});
        } else {
            out.emplace_back(KeyUp{.timestamp = timestamp,
                                   .window = windowIdOf(key.windowID),
                                   .scancode = scancode,
                                   .keycode = keycode,
                                   .modifiers = modifiers});
        }
        break;
    }
    case SDL_EVENT_MOUSE_MOTION: {
        const auto& motion = event.motion;
        out.emplace_back(MouseMoved{.timestamp = timestamp,
                                    .window = windowIdOf(motion.windowID),
                                    .position = pointOf(motion.x, motion.y),
                                    .delta = pointOf(motion.xrel, motion.yrel)});
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        const auto& button = event.button;
        const auto id =
            button.button <= 5 ? static_cast<MouseButton>(button.button) : MouseButton::kUnknown;
        if (button.down) {
            out.emplace_back(MouseButtonDown{.timestamp = timestamp,
                                             .window = windowIdOf(button.windowID),
                                             .button = id,
                                             .position = pointOf(button.x, button.y),
                                             .clicks = button.clicks});
        } else {
            out.emplace_back(MouseButtonUp{.timestamp = timestamp,
                                           .window = windowIdOf(button.windowID),
                                           .button = id,
                                           .position = pointOf(button.x, button.y),
                                           .clicks = button.clicks});
        }
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        const auto& wheel = event.wheel;
        const float sign = wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
        out.emplace_back(MouseWheel{.timestamp = timestamp,
                                    .window = windowIdOf(wheel.windowID),
                                    .delta = pointOf(wheel.x * sign, wheel.y * sign),
                                    .position = pointOf(wheel.mouse_x, wheel.mouse_y)});
        break;
    }
    case SDL_EVENT_TEXT_INPUT:
        translateTextInput(event, out);
        break;
    case SDL_EVENT_TEXT_EDITING:
        translateTextEditing(event, out);
        break;
    default:
        if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
            translateWindowEvent(event, out);
        }
        break;
    }
}

} // namespace detail

} // namespace ezmu::platform

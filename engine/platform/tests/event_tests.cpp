// Event translation: these tests push synthetic SDL events (allowed: tests of the platform
// module may include SDL) and check the ezMU events returned by Platform::pollEvents().

#include <ezmu/platform/event.hpp>
#include <ezmu/platform/keys.hpp>
#include <ezmu/platform/platform.hpp>

#include <SDL3/SDL.h>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "test_support.hpp"

namespace {

using namespace ezmu::platform;
using ezmu::foundation::Extent2D;
using ezmu::platform::test::eventsOf;
using ezmu::platform::test::makeHeadlessPlatform;
using ezmu::platform::test::makeWindow;

void push(SDL_Event event) {
    REQUIRE(SDL_PushEvent(&event));
}

SDL_WindowID sdlId(const Window& window) {
    return static_cast<SDL_WindowID>(window.id());
}

struct Fixture {
    Fixture() : platform(makeHeadlessPlatform()), window(makeWindow(*platform)) {
        static_cast<void>(platform->pollEvents()); // Drop window creation events.
    }

    std::unique_ptr<Platform> platform;
    std::unique_ptr<Window> window;
};

TEST_CASE("Key events carry scancode, keycode, modifiers and repeat", "[event][keyboard]") {
    Fixture f;
    SDL_Event down{};
    down.type = SDL_EVENT_KEY_DOWN;
    down.key.windowID = sdlId(*f.window);
    down.key.scancode = SDL_SCANCODE_A;
    down.key.key = SDLK_A;
    down.key.mod = SDL_KMOD_LSHIFT | SDL_KMOD_RCTRL | SDL_KMOD_CAPS;
    down.key.down = true;
    down.key.repeat = true;
    push(down);

    SDL_Event up = down;
    up.type = SDL_EVENT_KEY_UP;
    up.key.down = false;
    up.key.repeat = false;
    up.key.scancode = SDL_SCANCODE_F5;
    up.key.key = SDLK_F5;
    up.key.mod = SDL_KMOD_NONE;
    push(up);

    const auto events = f.platform->pollEvents();
    const auto downs = eventsOf<KeyDown>(events);
    const auto ups = eventsOf<KeyUp>(events);
    REQUIRE(downs.size() == 1);
    REQUIRE(ups.size() == 1);

    CHECK(downs[0].window == f.window->id());
    CHECK(downs[0].scancode == Scancode::kA);
    CHECK(downs[0].keycode == keycodeFromChar(U'a'));
    CHECK(downs[0].modifiers == (KeyMod::kLeftShift | KeyMod::kRightCtrl | KeyMod::kCapsLock));
    CHECK(hasAny(downs[0].modifiers, KeyMod::kCtrl));
    CHECK_FALSE(hasAny(downs[0].modifiers, KeyMod::kAlt));
    CHECK(downs[0].repeat);
    CHECK(downs[0].timestamp.count() > 0);

    CHECK(ups[0].scancode == Scancode::kF5);
    CHECK(ups[0].keycode == keycodeFromScancode(Scancode::kF5));
    CHECK(ups[0].modifiers == KeyMod::kNone);
}

TEST_CASE("Mouse events carry position, buttons and wheel direction", "[event][mouse]") {
    Fixture f;
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.windowID = sdlId(*f.window);
    motion.motion.x = 10.5F;
    motion.motion.y = 20.25F;
    motion.motion.xrel = 1.0F;
    motion.motion.yrel = -2.0F;
    push(motion);

    SDL_Event button{};
    button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    button.button.windowID = sdlId(*f.window);
    button.button.button = SDL_BUTTON_RIGHT;
    button.button.down = true;
    button.button.clicks = 2;
    button.button.x = 5.0F;
    button.button.y = 6.0F;
    push(button);

    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.windowID = sdlId(*f.window);
    wheel.wheel.x = 0.0F;
    wheel.wheel.y = 1.0F;
    wheel.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    wheel.wheel.mouse_x = 7.0F;
    wheel.wheel.mouse_y = 8.0F;
    push(wheel);

    const auto events = f.platform->pollEvents();
    const auto moves = eventsOf<MouseMoved>(events);
    const auto buttons = eventsOf<MouseButtonDown>(events);
    const auto wheels = eventsOf<MouseWheel>(events);
    REQUIRE(moves.size() == 1);
    REQUIRE(buttons.size() == 1);
    REQUIRE(wheels.size() == 1);

    CHECK(moves[0].position == PointF{.x = 10.5F, .y = 20.25F});
    CHECK(moves[0].delta == PointF{.x = 1.0F, .y = -2.0F});
    CHECK(buttons[0].button == MouseButton::kRight);
    CHECK(buttons[0].clicks == 2);
    CHECK(buttons[0].position == PointF{.x = 5.0F, .y = 6.0F});
    CHECK(wheels[0].delta == PointF{.x = -0.0F, .y = -1.0F});
    CHECK(wheels[0].position == PointF{.x = 7.0F, .y = 8.0F});
}

TEST_CASE("Text input preserves multi-byte UTF-8", "[event][text]") {
    Fixture f;
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.windowID = sdlId(*f.window);
    text.text.text = "ação 日本";
    push(text);

    const auto inputs = eventsOf<TextInput>(f.platform->pollEvents());
    REQUIRE(inputs.size() == 1);
    CHECK(inputs[0].text.view() == "ação 日本");
    CHECK(inputs[0].window == f.window->id());
}

TEST_CASE("Long text input is split on code point boundaries", "[event][text]") {
    Fixture f;
    std::string longText;
    for (int i = 0; i < 50; ++i) {
        longText += "é日"; // 2 + 3 bytes
    }
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.windowID = sdlId(*f.window);
    text.text.text = longText.c_str();
    push(text);

    const auto inputs = eventsOf<TextInput>(f.platform->pollEvents());
    REQUIRE(inputs.size() > 1);
    std::string joined;
    for (const auto& input : inputs) {
        CHECK(input.text.view().size() <= kMaxEventText);
        joined += input.text.view();
    }
    CHECK(joined == longText);
}

TEST_CASE("IME composition is reported with its cursor", "[event][text]") {
    Fixture f;
    SDL_Event edit{};
    edit.type = SDL_EVENT_TEXT_EDITING;
    edit.edit.windowID = sdlId(*f.window);
    edit.edit.text = "にほん";
    edit.edit.start = 2;
    edit.edit.length = 1;
    push(edit);

    const auto edits = eventsOf<TextEditing>(f.platform->pollEvents());
    REQUIRE(edits.size() == 1);
    CHECK(edits[0].text.view() == "にほん");
    CHECK(edits[0].cursor == 2);
    CHECK(edits[0].selectionLength == 1);
}

TEST_CASE("Window lifecycle events are translated", "[event][window]") {
    Fixture f;
    const auto pushWindow = [&f](SDL_EventType type, int data1 = 0, int data2 = 0) {
        SDL_Event event{};
        event.type = type;
        event.window.windowID = sdlId(*f.window);
        event.window.data1 = data1;
        event.window.data2 = data2;
        REQUIRE(SDL_PushEvent(&event));
    };
    pushWindow(SDL_EVENT_WINDOW_CLOSE_REQUESTED);
    pushWindow(SDL_EVENT_WINDOW_RESIZED, 800, 600);
    pushWindow(SDL_EVENT_WINDOW_MINIMIZED);
    pushWindow(SDL_EVENT_WINDOW_RESTORED);
    pushWindow(SDL_EVENT_WINDOW_FOCUS_GAINED);

    const auto events = f.platform->pollEvents();
    CHECK(eventsOf<WindowCloseRequested>(events).size() == 1);
    CHECK(eventsOf<WindowMinimized>(events).size() == 1);
    CHECK(eventsOf<WindowRestored>(events).size() == 1);

    const auto resized = eventsOf<WindowResized>(events);
    REQUIRE(resized.size() == 1);
    CHECK(resized[0].window == f.window->id());
    CHECK(resized[0].logicalSize == Extent2D{.width = 800, .height = 600});
    CHECK_FALSE(resized[0].pixelSize.isEmpty());

    const auto focus = eventsOf<WindowFocusChanged>(events);
    REQUIRE(focus.size() == 1);
    CHECK(focus[0].focused);
}

TEST_CASE("An OS quit request is translated", "[event]") {
    Fixture f;
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    push(quit);
    CHECK(eventsOf<QuitRequested>(f.platform->pollEvents()).size() == 1);
}

TEST_CASE("Event text keeps whole code points", "[event][text]") {
    EventText text;
    const std::string ascii(kMaxEventText - 1, 'x');
    const std::string input = ascii + "é"; // the 2-byte code point does not fit
    CHECK(text.assignPrefix(input) == kMaxEventText - 1);
    CHECK(text.view() == ascii);

    CHECK(text.assignPrefix("") == 0);
    CHECK(text.empty());
}

TEST_CASE("Keycodes follow the Unicode / scancode-mask encoding", "[event][keyboard]") {
    STATIC_CHECK(static_cast<std::uint32_t>(keycodeFromChar(U'a')) == 0x61);
    STATIC_CHECK(static_cast<std::uint32_t>(keycodeFromScancode(Scancode::kF1)) ==
                 (kKeycodeScancodeMask | 58U));
    STATIC_CHECK((KeyMod::kLeftAlt | KeyMod::kRightAlt) == KeyMod::kAlt);
}

} // namespace

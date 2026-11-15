# ezmu::platform (L1)

The only module that talks to the OS for windows and input. Wraps **SDL3** (ADR 0007), which is
private: no SDL header, type or value appears in `include/`. Spec: [docs/specs/platform.md](../../docs/specs/platform.md).

## Responsibility

Platform context, windows (size, DPI scale, mode, title), raw events (window, keyboard, mouse,
text input, IME composition, quit), text-input control, clipboard, message boxes, monotonic
clock, headless backend for tests, and the type-erased Vulkan surface boundary with `rhi`.

Not here: action mapping (→ `input`), UI focus/widgets (→ `ui`), game loop and threads
(→ `runtime`), graphics (→ `rhi`). Gamepads, file dialogs, cursors and display enumeration come
later, in this module, with a spec update.

## Public API

| Header | Contents |
|---|---|
| `platform.hpp` | `VideoBackend`, `PlatformDesc`, `Platform` (create, createWindow, pollEvents, waitEvents, injectEvent, clipboard, clock) |
| `window.hpp` | `WindowMode`, `WindowDesc`, `Window`, `VulkanVoidFunction` |
| `event.hpp` | `WindowId`, `PointF`, `EventText`, every event struct, `Event` variant |
| `keys.hpp` | `Scancode` (USB HID), `Keycode`, `KeyMod` flags, `MouseButton` |
| `clipboard.hpp`, `clock.hpp`, `message_box.hpp` | `Clipboard`, `Clock`, `showMessageBox` |

## Allowed dependencies

`ezmu::foundation` (public), `SDL3::SDL3` (private). **Never** `ezmu::rhi`: the Vulkan boundary
is type-erased (`void*` instance, `uint64_t` surface) and wired by the composition root.

## Pitfalls

- Main thread only (asserted), except `Clock`. One `Platform` at a time (`create` fails).
- Destroy windows before the Platform, and Vulkan surfaces (RHI swapchain) before their window.
- `Scancode` values equal SDL's (both USB HID) and `Keycode` uses SDL3's encoding: translation is
  a cast, guarded by `static_assert`s in `src/event_translation.cpp`. Keep them in sync.
- `pollEvents()` must not allocate in steady state; events are trivially copyable with inline
  UTF-8 text (`kMaxEventText`).
- SDL reports logical and pixel resizes separately; translation collapses identical neighbors.
- SDL3 is built without `ibus` (no D-Bus): Linux IME via IBus/Fcitx is not supported yet (see
  the spec's implementation notes).
- Tests use `VideoBackend::kHeadless` (SDL offscreen/dummy driver); headless windows never load
  Vulkan, so tests pass on machines without a Vulkan loader.

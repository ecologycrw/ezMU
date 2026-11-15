# 0007. SDL3 for the platform layer

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

The engine needs windows, input, timers and OS integration on Windows and Linux. An MMORPG
client has a requirement that many lightweight libraries handle poorly: **text input** for
chat — accents, dead keys, IME composition for Asian languages, clipboard — in addition to
keyboard, mouse and gamepad.

## Decision

- We will use **SDL3** for windowing, raw input, text input/IME, clipboard, message boxes,
  file dialogs and high-resolution timers.
- SDL is wrapped by the `engine/platform` module. No other module includes SDL headers.
- Vulkan surface creation is the single, explicit integration point between platform and RHI.

## Consequences

- Robust text input for chat and UI from the start.
- Gamepad support comes for free.
- Including SDL headers outside `engine/platform` is forbidden and checked by
  `scripts/checks/check_rules.cmake`.

## Alternatives considered

- **GLFW** — simpler, but weak IME/text-input support, which is essential for chat.
- **Native Win32/X11/Wayland code** — maximum control, multiplied maintenance.

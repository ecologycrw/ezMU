---
paths:
  - "engine/platform/**"
---

# platform module

Source: ADR 0007, ADR 0012, [platform spec](../../docs/specs/platform.md).

- SDL3 is used **only** here. No SDL header, type, enum value or handle in `include/`; translate
  to ezMU types (`Scancode`, `Keycode`, `KeyMod`, `WindowId`, `Event`). Use pimpl (`Impl`) for
  native handles.
- No dependency on `rhi`. The Vulkan boundary stays type-erased: instance extensions as strings,
  `createVulkanSurface(void*) → uint64_t`, `VulkanVoidFunction` for `vkGetInstanceProcAddr`.
- Everything except `Clock` and `showMessageBox` is main-thread only; new entry points call
  `detail::assertMainThread`.
- `pollEvents()`/`waitEvents()` must not allocate in steady state. Event structs stay small and
  trivially copyable; text is UTF-8 inside `EventText`, split or truncated on code point
  boundaries.
- `Scancode` values are USB HID usage IDs and are stable (keybind files depend on them). Never
  renumber; add enumerators with their HID value.
- Every SDL call that can fail is checked; failures become `foundation::Result` errors with
  `SDL_GetError()` in the message (`detail::sdlError`).
- Tests use `VideoBackend::kHeadless` and may include SDL to push synthetic events; assert on
  ezMU events from `pollEvents()`, filtering by type (window creation emits extra events).

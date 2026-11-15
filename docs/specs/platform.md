# Spec: platform

- **Status:** Approved (2026-11-15, all proposals accepted)
- **Layer:** L1
- **Owner:** ecologycrw
- **Related ADRs:** [0005](../adr/0005-target-platforms-and-compilers.md),
  [0007](../adr/0007-sdl3-platform-layer.md), [0008](../adr/0008-third-party-libraries.md),
  [0011](../adr/0011-layered-architecture-engine-and-framework.md),
  [0012](../adr/0012-dedicated-render-thread.md), [0019](../adr/0019-ai-assisted-development-workflow.md)
- **Milestone:** M1 "First pixels" (together with [rhi](rhi.md))

## Purpose

`ezmu::platform` is the only place in ezMU that talks to the operating system for windows,
input and OS services. It wraps **SDL3** and exposes ezMU types: a platform context, windows,
a raw event stream (keyboard, mouse, text input/IME, window lifecycle), clipboard, message
boxes and a monotonic clock. It also provides the single integration point with the RHI:
the Vulkan instance extensions a window needs and the creation of a Vulkan surface, both
type-erased so that neither module includes the other's headers.

It is **not** responsible for: action mapping and rebinding (→ `input`, L2), UI focus and text
editing widgets (→ `ui`, L2), the game loop and thread orchestration (→ `runtime`, L3),
graphics (→ `rhi`), gamepads, file dialogs and audio devices (later, see *Scope*).

## Scope for M1

| In M1 | Later (same module, own spec update) |
|---|---|
| Platform context (SDL init/shutdown) | Gamepads / haptics |
| One or more windows: create, title, size, mode, close request | File open/save dialogs |
| Pixel size vs. logical size, display scale (DPI) | Display/monitor enumeration and video modes |
| Raw events: window, keyboard, mouse, wheel, text input, IME composition | Cursor shapes, relative mouse mode |
| Text input start/stop + IME candidate area | Drag-and-drop |
| Clipboard text get/set | Power/battery, system locale |
| Message box (fatal errors) | |
| Monotonic high-resolution clock | |
| Vulkan instance extensions + surface creation (type-erased) | |
| Headless mode for tests and CI | |

## Use cases

1. **Sample / runtime** creates a `Platform`, then a `Window`, pumps events every frame on the
   main thread, and quits on `WindowCloseRequested`.
2. **RHI bootstrap** (wired by the sample now, by `runtime` in M2) asks the window for the Vulkan
   instance extensions, creates the RHI device, then asks the window to create a `VkSurfaceKHR`
   for that instance and hands the raw surface to the RHI.
3. **Chat UI** (later, `ui`/`gameui`) starts text input when a chat box gains focus, receives
   `TextInput` (committed UTF-8) and `TextEditing` (IME composition) events, and sets the IME
   candidate rectangle under the caret.
4. **Input** (later, `input`) consumes raw key/mouse events and keeps per-frame state for action
   mapping. Keys are reported by **scancode** (physical position, layout independent) and by
   **keycode** (layout-dependent symbol).
5. **Tests / CI / AI agents** run with a headless video backend (no display) and inject events
   through the public API to exercise code that consumes input deterministically.
6. **Fatal error path** in `main` shows a message box with the error before exiting.

## Public API

Headers under `engine/platform/include/ezmu/platform/`. Namespace `ezmu::platform`.
Sketch only — names and exact signatures are refined during implementation and reviewed in the PR.

### `platform.hpp` — context

```cpp
enum class VideoBackend : std::uint8_t {
    kDefault,  ///< Native backend (Win32; Wayland or X11 on Linux).
    kHeadless, ///< No display. Windows are virtual; no Vulkan surface. For tests and CI.
};

struct PlatformDesc {
    std::string_view appName = "ezMU";   ///< Used for window class / app id.
    VideoBackend videoBackend = VideoBackend::kDefault;
};

/// Owns the SDL library state. Exactly one may exist at a time (EZMU_ASSERT); SDL itself is
/// process-global, this object makes its lifetime explicit and passable as a service.
/// Thread-safety: create, use and destroy on the main thread only.
class Platform {
public:
    [[nodiscard]] static foundation::Result<std::unique_ptr<Platform>> create(const PlatformDesc&);
    ~Platform();

    [[nodiscard]] foundation::Result<std::unique_ptr<Window>> createWindow(const WindowDesc&);

    /// Drains the OS event queue and returns events received since the last call.
    /// The span is valid until the next pollEvents() call. Does not block.
    /// Allocation: reuses an internal buffer; no allocation in steady state.
    [[nodiscard]] std::span<const Event> pollEvents();

    /// Blocks until an event arrives or the timeout expires (for tools/idle apps).
    [[nodiscard]] std::span<const Event> waitEvents(std::chrono::milliseconds timeout);

    /// Appends a synthetic event to the queue, delivered by the next pollEvents().
    /// For tests, automation and input replay. Main thread only.
    void injectEvent(const Event& event);

    [[nodiscard]] Clipboard& clipboard() noexcept;
    [[nodiscard]] const Clock& clock() const noexcept;
};
```

### `window.hpp`

```cpp
enum class WindowMode : std::uint8_t { kWindowed, kBorderlessFullscreen, kExclusiveFullscreen };

struct WindowDesc {
    std::string title = "ezMU";
    Extent2D logicalSize{1280, 720};      ///< In logical units (scaled by display scale).
    WindowMode mode = WindowMode::kWindowed;
    bool resizable = true;
    bool vulkan = true;                   ///< Window will host a Vulkan swapchain.
};

/// Strongly typed id used in events to tell windows apart.
enum class WindowId : std::uint32_t {};

/// A native window. Must be destroyed before its Platform, and after any RHI swapchain/surface
/// created for it. Thread-safety: main thread only, except the const getters of cached state
/// (id, pixelSize) which are safe to read from any thread after the last main-thread update
/// has been published by the caller (e.g. through the render snapshot).
class Window {
public:
    ~Window();
    [[nodiscard]] WindowId id() const noexcept;
    [[nodiscard]] Extent2D logicalSize() const noexcept;
    [[nodiscard]] Extent2D pixelSize() const noexcept;   ///< Use this for the swapchain.
    [[nodiscard]] float displayScale() const noexcept;   ///< pixel / logical, e.g. 1.5 at 150%.
    [[nodiscard]] bool isMinimized() const noexcept;
    void setTitle(std::string_view utf8);
    [[nodiscard]] foundation::Result<> setMode(WindowMode mode);

    // Text input (IME). Off by default so key presses do not open IME candidate windows.
    void startTextInput();
    void stopTextInput();
    [[nodiscard]] bool isTextInputActive() const noexcept;
    void setTextInputArea(const Rect2D& caretRectLogical);

    // --- Vulkan integration (ADR 0007): type-erased, no Vulkan or SDL types. ---

    /// Instance extensions required to present to this window (e.g. "VK_KHR_surface",
    /// "VK_KHR_win32_surface"). Strings have static lifetime.
    [[nodiscard]] std::span<const char* const> vulkanInstanceExtensions() const;

    /// Creates a VkSurfaceKHR for `vkInstance` (a VkInstance passed as an opaque pointer) and
    /// returns it as its raw 64-bit handle. Ownership of the surface goes to the caller (the RHI
    /// swapchain destroys it). Fails with kUnsupported in headless mode or for non-Vulkan windows.
    [[nodiscard]] foundation::Result<std::uint64_t> createVulkanSurface(void* vkInstance) const;

    /// SDL's vkGetInstanceProcAddr, as an opaque function pointer, so the RHI can initialize its
    /// loader from the same Vulkan library SDL loaded (see rhi spec, "Loader").
    [[nodiscard]] static void* vulkanGetInstanceProcAddr();
};
```

### `event.hpp`

A closed `std::variant` of small trivially copyable structs. Every event carries a timestamp
(`Clock` nanoseconds) and, where applicable, a `WindowId`.

| Event | Fields |
|---|---|
| `WindowCloseRequested` | window |
| `WindowResized` | window, logicalSize, pixelSize |
| `WindowMinimized` / `WindowRestored` | window |
| `WindowFocusChanged` | window, focused |
| `WindowDisplayScaleChanged` | window, scale |
| `KeyDown` / `KeyUp` | window, scancode (`Scancode`), keycode (`Keycode`), modifiers (`KeyMod` flags), repeat |
| `MouseMoved` | window, position (logical, float), delta |
| `MouseButtonDown` / `MouseButtonUp` | window, button (`MouseButton`), position, clicks |
| `MouseWheel` | window, delta (float, x/y), position |
| `TextInput` | window, committed text (UTF-8, inline fixed buffer, see below) |
| `TextEditing` | window, composition text (UTF-8), cursor start, length |
| `QuitRequested` | — (OS-level quit, e.g. logout or SIGTERM) |

- `Scancode` follows USB HID usage IDs (as SDL does), so the numeric values are stable and
  serializable for keybind files. `Keycode` is a Unicode code point for printable keys or a
  named enumerator otherwise. Both are ezMU enums; SDL values never escape the module.
- Text payloads use a fixed inline buffer (`kMaxEventText = 64` bytes, UTF-8, split on code
  point boundaries into several events if longer) so `Event` stays trivially copyable and
  `pollEvents()` does not allocate.

### `clipboard.hpp`, `clock.hpp`, `message_box.hpp`

```cpp
class Clipboard {   // main thread only
public:
    [[nodiscard]] foundation::Result<std::string> text() const;
    [[nodiscard]] foundation::Result<> setText(std::string_view utf8);
};

/// Monotonic, high-resolution. Thread-safe (stateless reads).
class Clock {
public:
    [[nodiscard]] std::chrono::nanoseconds now() const noexcept;
};

enum class MessageBoxKind : std::uint8_t { kInfo, kWarning, kError };
/// Shows a modal message box. Works without a Platform instance (for fatal errors in main).
foundation::Result<> showMessageBox(MessageBoxKind, std::string_view title, std::string_view text);
```

Small shared value types (`Extent2D`, `Rect2D`) come from `foundation` (minimal math value
types added in M1 step 2, see the [M1 implementation plan](rhi.md#m1-implementation-plan)), so
`platform` and `rhi` share them without depending on each other.

### Errors

All fallible operations return `foundation::Result<T>`. SDL error strings are copied into the
`Error` message. New `ErrorCode`s are not needed for M1 (`kUnsupported`, `kInternal`,
`kInvalidArgument` cover it).

## Threading

Follows [ADR 0012](../adr/0012-dedicated-render-thread.md) and SDL3's constraints:

- **Main thread** owns `Platform`, every `Window`, event pumping, text input, clipboard and
  message boxes. SDL requires window and event functions on the main thread on several
  platforms; the API makes this a documented contract and debug builds assert it
  (`Platform` records the creating thread id).
- **Render thread** never calls `platform`. It receives what it needs (new pixel size,
  minimized state) through the render snapshot / a frame mailbox prepared by the main thread.
- **Surface creation** happens on the main thread during startup (or when a window is
  recreated); the resulting raw surface handle is then owned by the RHI swapchain on the render
  thread.
- **Resizing:** the main thread receives `WindowResized` and forwards the new pixel size; the
  render thread recreates the swapchain. On Windows, interactive resizing/moving blocks the
  main thread in a modal loop; because rendering runs on its own thread, the window keeps
  presenting (it may present at a stale size until the next event pump).
- `Clock::now()` and `showMessageBox()` may be called from any thread.

## Dependencies

- ezMU modules: `ezmu::foundation` (public).
- Third-party: **SDL3** (`find_package(SDL3 CONFIG REQUIRED)`, target `SDL3::SDL3`), private to
  the module. Added to `vcpkg.json` in the same commit that creates the module (ADR 0008).
  Linux CI needs the X11/Wayland development packages that the vcpkg SDL3 port requires.
- No dependency on `rhi`, and `rhi` has none on `platform` (no `ALLOW_SAME_LAYER`). The two are
  wired by the composition root (the sample in M1, `runtime` from M2).

## Extension points

- **Configure:** `PlatformDesc`, `WindowDesc` (mode, size, resizable), text-input on/off.
- **Extend:** `injectEvent()` lets games/tools feed synthetic or recorded input (replay,
  automation, AI-driven tests).
- **Replace:** the module is the abstraction; a game that does not want SDL can skip
  `platform` and drive `rhi` with its own surface (the RHI only needs a raw `VkSurfaceKHR` and
  the instance extensions). An `IPlatform` interface is **not** introduced in M1 (YAGNI); the
  type-erased Vulkan boundary is what keeps replacement possible.

## Data formats / messages

None persisted. `Scancode` values are stable (USB HID) for future keybind files.

## Testing strategy

All tests run in CI on Windows and Linux without a display, using `VideoBackend::kHeadless`
(SDL's offscreen/dummy video driver).

- **Context:** create/destroy `Platform`; a second concurrent `Platform` trips the assert
  (tested with an RAII assertion-handler guard).
- **Window:** create in headless mode, query sizes and scale, change title and mode; Vulkan
  surface creation returns `kUnsupported` in headless mode.
- **Event translation:** module tests (under `engine/platform/tests/`, which may include SDL)
  push synthetic SDL events (`SDL_PushEvent`) and verify the translated ezMU events through
  `pollEvents()`: key scancode/keycode/modifiers/repeat, mouse buttons and wheel, text input
  with multi-byte UTF-8 (`"ação"`, `"日本"`), splitting of long text on code point boundaries,
  IME composition, resize with pixel vs. logical size.
- **Injection:** `injectEvent()` events come back from `pollEvents()` in order.
- **No allocation in steady state:** a test pumps many events and checks the internal buffer
  capacity stops growing (allocation counting arrives with foundation memory).
- **Clipboard:** round-trip where the headless driver supports it; otherwise skipped with a
  reason.
- **Manual/visual (owner):** the `ezmu_clear` sample on a real desktop — resize, minimize,
  fullscreen toggle, 150 % DPI, IME (e.g. Japanese/Chinese input) in a later UI milestone.

## Done when

- [ ] `engine/platform` module exists (`ezmu_add_module(NAME platform LAYER 1 …)`), SDL3 added
      to `vcpkg.json`, ADR 0008 table unchanged (SDL3 already listed).
- [ ] Public API above implemented and documented (`///` comments with thread rules).
- [ ] Headless tests pass on all CI presets (MSVC debug/release, Clang 19 ASan, GCC 14 ASan).
- [ ] `architecture.rules` still passes (SDL included only under `engine/platform`).
- [ ] The `ezmu_clear` sample opens a resizable window, reacts to close/resize/minimize, and
      the screen is cleared by the RHI (see [rhi spec](rhi.md#done-when)).
- [ ] `engine/platform/CLAUDE.md` and path-scoped rule `.claude/rules/platform.md` written.
- [ ] Owner checked the sample on their machine (resize, minimize, DPI).

## Implementation notes (M1, differences from the sketch)

- A second concurrent `Platform::create` returns `kInvalidArgument` instead of asserting, so the
  rule is testable and safe in release builds.
- `Window` getters query SDL and are **main thread only** (no cached cross-thread state); the
  composition root forwards sizes to the render thread.
- `vulkanGetInstanceProcAddr()` is a member returning `VulkanVoidFunction` (a function pointer,
  not `void*`), and is `nullptr` for non-Vulkan or headless windows.
- Added `Window::mode()` and `Window::setLogicalSize()`; digit scancodes are `kDigit0`–`kDigit9`
  (clang-tidy flags `k0`/`kO` and `k1`/`kI` as confusable).
- SDL3 comes from vcpkg with the `vulkan` feature.

## Implementation plan

The M1 plan for both modules (including the `ezmu::engine` aggregator and the path-scoped
rules `.claude/rules/platform.md` / `rhi-vulkan.md`) lives in the
[rhi spec](rhi.md#m1-implementation-plan). The cross-module decisions (D1 surface
integration, D6 render thread in M1) are listed there too.

## Decisions (approved 2026-11-15: every proposal below was accepted)

1. **Shared small types** (`Extent2D`, `Rect2D`): *proposal:* minimal `foundation` math value
   types now (no glm yet). *Alternative:* define them in `platform` and `rhi` separately until
   the glm wrapper exists (duplication, conversions in the composition root).
2. **Main-thread enforcement:** assert in debug only (proposal), or `EZMU_VERIFY` always?
3. **Text in events:** fixed 64-byte inline buffer with splitting (proposal) vs. a per-frame
   string arena referenced by events.
4. **Gamepads in M1?** *Proposal:* no — later, with `input` action mapping.

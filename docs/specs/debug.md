# Spec: debug (overlay)

- **Status:** Approved (M2 batch B, 2026-11-15; every proposal accepted)
- **Layer:** L2
- **Owner:** ecologycrw
- **Related ADRs:** [0008](../adr/0008-third-party-libraries.md),
  [0011](../adr/0011-layered-architecture-engine-and-framework.md),
  [0012](../adr/0012-dedicated-render-thread.md),
  [0016](../adr/0016-tools-in-repository-public-api-only.md),
  [0022](../adr/0022-dear-imgui-integration.md)
- **Milestone:** M2 "Runtime and render thread" (batch B, with the
  [rhi M2 update](rhi.md#m2-update-pipelines-uploads-and-bindless-textures) and
  [renderer part 2](renderer.md))

## Purpose

`ezmu::debug` provides the in-game **debug overlay**: a Dear ImGui context with in-house
backends over `platform` events and `renderer` 2D draw lists
([ADR 0022](../adr/0022-dear-imgui-integration.md)), built-in panels for the engine (frame
timings on both timelines, jobs, log) and a way for games to add their own panels written with
the ImGui API.

It is **not** responsible for: the game UI (→ `ui` / `gameui`, retained-mode, data-driven);
editors (→ `tools`, which reuse this module's backends); profiling (Tracy, later); drawing
(→ `renderer`, which draws generic 2D lists); deciding when frames happen (→ `runtime`, which
installs the overlay as a frame hook).

## Scope for M2

| In M2 | Later |
|---|---|
| `DebugOverlay`: ImGui context, platform backend, renderer backend | Docking, multi-viewports (tools) |
| Toggle key, visibility, input capture rules | Persisted layout (`imgui.ini` through the VFS) |
| Built-in panels: Frame (both timelines), Jobs, Log | Entity/component inspector (with `world`), asset browser, network panel |
| Game panels: `addPanel(name, draw)` with ImGui code | Console commands / cvars |
| Display scale (DPI) with dynamic fonts | Custom fonts, FreeType rasterizer |
| `runtime` integration (frame hook, `EZMU_DEBUG_TOOLS`) | Gamepad navigation |

## Use cases

1. **Engine developer** presses the toggle key (default: the key left of `1`, scancode
   `kGrave`) and sees the Frame panel: game frame time, time blocked on the render thread, render
   CPU time, presented / skipped frames, game frame index vs. render frame index, validation
   messages, job statistics.
2. **Game developer** adds a panel from a game state:
   `ctx.debugOverlay()->addPanel("Inventory", [this] { ImGui::Text("%d items", count); })`.
3. **While the overlay is visible**, mouse and keyboard input that ImGui wants (hover over a
   window, typing in a text field) does not reach the game; everything else passes through.
   When hidden, the overlay consumes nothing except its toggle key.
4. **Text input** in an ImGui field uses the platform's text input and IME (accents, composition),
   like chat will.
5. **High DPI:** at 150 % the overlay is rendered at the window's pixel size with fonts
   rasterized for that scale (sharp text, not scaled bitmaps).
6. **Log panel** shows the most recent log records from every thread, filterable by level and
   category.
7. **Tools** (later) create their own `DebugOverlay`-like contexts with the same backends.

## Public API

Headers under `engine/debug/include/ezmu/debug/`. Namespace `ezmu::debug`. Sketch only — names
and exact signatures are refined during implementation and reviewed in the PR. Public headers
do not include `imgui.h` (ADR 0022); panel bodies include it themselves.

### `overlay.hpp`

```cpp
struct OverlayDesc {
    platform::Scancode toggleKey = platform::Scancode::kGrave;
    bool startVisible = false;
    float fontSizeLogical = 13.0F;         ///< Before display scaling.
};

/// Per-frame input from the host (runtime): window state and timing.
struct OverlayFrameInput {
    std::chrono::nanoseconds delta{};
    foundation::Extent2D logicalSize{};    ///< Window size in logical units.
    float displayScale = 1.0F;
    bool focused = true;
};

/// Samples shown by the built-in Frame panel; plain data pushed by the host each frame, so
/// `debug` does not depend on `runtime`.
struct FrameStatsSample {
    std::uint64_t gameFrameIndex = 0;
    std::uint64_t renderFrameIndex = 0;          ///< Latest presented rhi frame.
    std::chrono::nanoseconds gameFrameTime{};
    std::chrono::nanoseconds waitForRender{};
    std::chrono::nanoseconds renderCpuTime{};
    std::uint32_t presentedFrames = 0;           ///< Reports received this frame.
    std::uint32_t skippedFrames = 0;
    std::uint32_t validationMessages = 0;
    foundation::JobStats jobs{};
};

using PanelId = std::uint32_t;

/// The debug overlay: one ImGui context and its backends.
///
/// Thread-safety: main thread only (asserted in debug builds); ImGui is not thread-safe.
/// Lifetime: destroy before the platform window and before RenderResources; textures it
/// created are destroyed through RenderResources in the destructor (or in shutdown()).
/// Allocation: steady-state frames reuse buffers; panels and the log ring allocate once.
class DebugOverlay {
public:
    [[nodiscard]] static foundation::Result<std::unique_ptr<DebugOverlay>>
    create(const OverlayDesc& desc, platform::Platform& platform, platform::Window* window,
           foundation::Logger& logger);
    ~DebugOverlay();

    /// Feeds one platform event. Returns true when the overlay consumed it (toggle key, or ImGui
    /// wants mouse/keyboard while visible). Window lifecycle events are never consumed.
    [[nodiscard]] bool handleEvent(const platform::Event& event);

    void beginFrame(const OverlayFrameInput& input);     ///< ImGui::NewFrame + built-in panels.
    void submitStats(const FrameStatsSample& sample);    ///< Before endFrame; kept in a history.
    /// ImGui::Render, then texture requests → `resources`, draw data → one DrawList2D added
    /// to `snapshot`. Adds nothing when hidden.
    void endFrame(renderer::RenderSnapshot& snapshot, renderer::RenderResources& resources);

    /// Panels are drawn between beginFrame and endFrame while visible, in insertion order, each
    /// inside its own ImGui window titled `name`. `draw` runs on the main thread.
    PanelId addPanel(std::string name, std::function<void()> draw);
    void removePanel(PanelId id);

    void setVisible(bool visible);
    [[nodiscard]] bool isVisible() const noexcept;

    /// Log sink feeding the Log panel (thread-safe ring of recent records). The host adds it to
    /// its Logger and keeps it alive while the overlay exists.
    [[nodiscard]] std::unique_ptr<foundation::ILogSink> createLogSink(std::size_t capacity = 512);

    /// Releases the overlay's textures through `resources` (queued into the next snapshot).
    void shutdown(renderer::RenderResources& resources);
};
```

### Example: a game panel

```cpp
// In a game state's onEnter (game code includes <imgui.h> itself)
if (debug::DebugOverlay* overlay = ctx.debugOverlay()) {   // nullptr when debug tools are off
    m_panel = overlay->addPanel("Login", [this] {
        ImGui::Text("Server: %s", m_server.c_str());
        ImGui::Checkbox("Auto-retry", &m_autoRetry);
    });
}
```

## Backends (implementation notes)

### Platform backend

- Keys by **scancode** → `ImGuiKey` (physical layout; same choice as `platform`), modifiers from
  `KeyMod`; `TextInput` → `AddInputCharactersUTF8`; `TextEditing` is not forwarded (ImGui has no
  composition preview; the OS IME window shows it).
- `io.WantTextInput` rising/falling → `Window::startTextInput` / `stopTextInput`;
  `PlatformIO.Platform_SetImeDataFn` → `Window::setTextInputArea` (caret rectangle in logical
  units).
- Clipboard through `PlatformIO.Platform_GetClipboardTextFn` / `SetClipboardTextFn` →
  `platform::Clipboard`. `Platform_OpenInShellFn` is cleared (no shell access from the overlay).
- Mouse cursor shapes are ignored until `platform` exposes cursors (later item there).
- Headless (no window): keyboard and mouse events still work (tests inject them); clipboard and
  IME calls are no-ops.

### Renderer backend

- `io.BackendFlags |= RendererHasVtxOffset | RendererHasTextures`;
  `io.DisplaySize = logicalSize`, `io.DisplayFramebufferScale = {scale, scale}`,
  `style.FontScaleDpi = scale` (dynamic fonts, ImGui ≥ 1.92).
- Texture requests in `ImDrawData::Textures`: `WantCreate` → `RenderResources::createTexture`
  (RGBA8 UNORM; ImGui converts its alpha atlas when the backend asks for RGBA32),
  `WantUpdates` → `updateTexture` with each dirty rectangle, `WantDestroy` → `destroyTexture`.
  `ImTextureID` carries the packed `renderer::TextureId`.
- Draw data: all `ImDrawList`s are concatenated into **one** `DrawList2D` (vertices appended,
  16-bit indices widened to 32 bits, `DrawCommand2D::vertexOffset = cmd.VtxOffset + base`,
  clip rectangles copied in logical units). `ImDrawCmd` user callbacks are not supported (an
  assertion; nothing in ImGui core uses them except `ResetRenderState`, which is a no-op here).
- `IM_COL32` packs R in the low byte, matching `Vertex2D::color`; colors are sRGB-encoded and
  linearized by the renderer's 2D shader.

## Changes to other specs (batch B)

- **renderer (part 2):** `TextureUpdate` in `ResourceCommand` and
  `RenderResources::updateTexture(id, region, pixels)` — already written into the
  [renderer spec](renderer.md), because ADR 0022 picks ImGui 1.93 with dynamic textures.
- **runtime:**
  - `AppDesc::debugOverlay` (`OverlayDesc`, plus `bool enabled = true`); when the build has
    `EZMU_DEBUG_TOOLS=ON`, `create` makes a `DebugOverlay` after the window and adds its log sink
    to the logger;
  - a private frame hook (order `INT32_MAX`: sees events first, extracts last) calls
    `handleEvent`, `beginFrame` + `submitStats` (from `RuntimeStats`), `endFrame` with the
    snapshot and `ctx.renderResources()`; its `onShutdown` calls `shutdown`;
  - `AppContext::debugOverlay()` returns `debug::DebugOverlay*` (`nullptr` when disabled or
    built without debug tools). Because the declaration names a `debug` type, `runtime` depends
    on `ezmu::debug` only when `EZMU_DEBUG_TOOLS=ON`; otherwise `debug_overlay_fwd.hpp` forward
    declares the class and the function always returns `nullptr`.
- **samples:** `ezmu_clear` gets `--overlay` (start visible) and keeps F12 capture, so captures can
  include the overlay.

## Dependencies

- `ezmu::foundation`, `ezmu::platform` (public: events, window, clipboard), `ezmu::renderer`
  (public, `ALLOW_SAME_LAYER renderer`: snapshot, `DrawList2D`, `RenderResources` — decision R1).
- Third-party: **Dear ImGui** (`find_package(imgui CONFIG REQUIRED)`, target `imgui::imgui`),
  linked **PUBLIC** so games can write panels (ADR 0022); added to `vcpkg.json` without binding
  features in the commit that creates the module.
- Build option `EZMU_DEBUG_TOOLS` (root `CMakeLists.txt`, default `ON`): when `OFF` the module is
  still built and tested in the repository, but `runtime` does not link it.

## Extension points

- **Configure:** `OverlayDesc` (toggle key, initial visibility, font size), `EZMU_DEBUG_TOOLS`.
- **Extend:** game and engine panels through `addPanel` using the ImGui API; the log sink; more
  built-in panels arrive with their modules (world inspector, network).
- **Replace:** disable the overlay (`AppDesc::debugOverlay.enabled = false` or
  `EZMU_DEBUG_TOOLS=OFF`) and produce your own `DrawList2D`s — the renderer path is generic.

## Data formats / messages

None persisted in M2 (`io.IniFilename = nullptr`: no `imgui.ini` written next to the
executable; persisted layouts come with the VFS).

## Testing strategy

**No-GPU tests** (every preset; module tests may include `imgui.h`), headless platform, no
window, events injected:

- Toggle key shows/hides the overlay and is consumed; other keys pass through while hidden.
- With a test panel containing a button at a known position: a mouse click over it is consumed
  and the button reports pressed; a click elsewhere is not consumed.
- Text input: a focused `InputText` receives multi-byte UTF-8 (`"ação"`, `"日本"`) from
  `TextInput` events.
- `endFrame` while visible adds exactly one `DrawList2D` that passes `renderer::validate`, with
  a texture create command for the font atlas on the first frame and update commands when new
  glyphs/sizes are requested; hidden → nothing added.
- Display scale 1.5 → list `scale` 1.5 and a font texture rasterized for that scale.
- Log sink: records from several threads appear in order with their levels (TSan job).
- `addPanel` / `removePanel` ordering; `shutdown` queues destroy commands for every texture.

**GPU test** (label `gpu`): a headless runtime (`kFixed` time, overlay visible with a
deterministic test panel, fixed size 640×360) captures frame 5 and compares it with
`engine/debug/tests/reference/overlay.png` using a tolerance (text rasterization is on the CPU
and deterministic; GPU rasterization of edges may differ by a few values between lavapipe and
hardware).

**Owner check:** `ezmu_clear --overlay` on the desktop at 100 % and 150 % DPI (sharp text,
correct mouse mapping), typing with accents and an IME in an ImGui text field.

## Done when

- [ ] `engine/debug` module (`ezmu_add_module(NAME debug LAYER 2 … ALLOW_SAME_LAYER renderer)`),
      `imgui` in `vcpkg.json` (ADR 0022, accepted 2026-11-15).
- [ ] Renderer part 2 with `TextureUpdate`, and the runtime additions above.
- [ ] Tests above pass on all CI presets; GPU reference test on lavapipe and the owner's GPU;
      no validation messages.
- [ ] `architecture.rules` rule: ImGui headers only under `engine/debug` and `tools/`.
- [ ] `ezmu_clear --overlay` checked by the owner (100 % and 150 % DPI, text input).
- [ ] `engine/debug/CLAUDE.md` (main thread only, context made current per call, no `imgui.h` in
      public headers, overlay is a producer of 2D lists only).

## Implementation plan (M2 batch B)

Starts after batch A is implemented. Each step is a small, verified commit (`git commit -s`).

1. **`build(deps): add shader-slang host tool`** + **`feat(rhi): pipelines, uploads and
   bindless textures`** — rhi M2 update, `cmake/EzmuShaders.cmake`, test shaders, GPU tests;
   ADR 0008 table row for the Slang compiler.
2. **`feat(renderer): 2D draw lists and textures`** — renderer part 2 (with `TextureUpdate`),
   `engine/renderer/shaders/draw_2d.slang`, reference images.
3. **`feat(debug): ImGui overlay`** — module, backends, built-in panels, tests; architecture
   rule for ImGui includes.
4. **`feat(runtime): install the debug overlay`** — `EZMU_DEBUG_TOOLS`, hook, `debugOverlay()`.
5. **`feat(samples): ezmu_clear --overlay`** and **`docs: close milestone M2`** after the owner's
   desktop check.

## Decisions (approved 2026-11-15: every proposal below was accepted)

- **G1 — Toggle key.** *Proposal:* scancode `kGrave` (left of `1`, the usual console key),
  configurable. *Alternatives:* F1 (often the game's help key); a chord such as Ctrl+F12.
- **G2 — Input capture.** *Proposal:* while visible, the overlay consumes events only when ImGui
  wants them (`WantCaptureMouse`, `WantCaptureKeyboard`, `WantTextInput`). *Alternative:* modal —
  a visible overlay consumes all input (simpler, but the game cannot be played with the stats
  open).
- **G3 — Built-in panels in M2.** *Proposal:* Frame, Jobs and Log only. *Alternative:* also a
  render-snapshot inspector (useful, but waits for a snapshot with scene content).
- **G4 — Stats without a runtime dependency.** *Proposal:* the host pushes a plain
  `FrameStatsSample`; `debug` keeps a history for graphs. *Alternative:* `debug` reads runtime
  stats directly (impossible: L2 cannot depend on L3).
- **G5 — One draw list per overlay.** *Proposal:* concatenate every `ImDrawList` into one
  `DrawList2D`. *Alternative:* one `DrawList2D` per `ImDrawList` (more lists and state changes,
  no benefit for the 2D pass).
- **G6 — No `imgui.ini` in M2.** *Proposal:* window positions are not persisted until the VFS
  exists. *Alternative:* let ImGui write `imgui.ini` in the working directory (files appear next
  to tests and samples).

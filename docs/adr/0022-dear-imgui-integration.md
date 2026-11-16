# 0022. Dear ImGui integration: in-house backends over platform and renderer

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

[ADR 0008](0008-third-party-libraries.md) already adopts Dear ImGui for debug UI and tools,
owned by `debug` and `tools`. It does not say **how** ImGui is integrated, and the obvious way
conflicts with other accepted decisions:

- ImGui's official backends `imgui_impl_vulkan` and `imgui_impl_sdl3` include Vulkan and SDL
  headers, which are allowed only inside `engine/rhi` and `engine/platform`
  ([ADR 0006](0006-vulkan-1-3-behind-rhi.md), [ADR 0007](0007-sdl3-platform-layer.md); checked by
  `architecture.rules`).
- The renderer never reads game state and runs on its own thread; it consumes render snapshots
  ([ADR 0012](0012-dedicated-render-thread.md)). ImGui is immediate-mode and runs on the main
  thread, so its output must travel inside the snapshot.
- ImGui keeps its current context in a process-wide pointer (`GImGui`), while ezMU forbids
  mutable globals except justified ones ([ADR 0018](0018-code-commit-and-branch-conventions.md)).
- Editing code never ships in a game's runtime binary
  ([ADR 0016](0016-tools-in-repository-public-api-only.md)), and the architecture overview says
  `debug` is never in release builds of games.
- Game developers expect to write their own debug windows with the ImGui API.
- The vcpkg baseline (`8f363bd`) has `imgui` **1.93.0**, which includes the dynamic texture
  system introduced in 1.92: the renderer backend creates, updates and destroys textures on
  request (fonts are rasterized on demand, at any size, which matters for display scaling).

The M2 [debug spec](../specs/debug.md) needs this settled.

## Decision

1. **No official backends.** `debug` implements both ImGui backends in-house, over ezMU public
   APIs only:
   - *platform side:* `platform::Event` → ImGui IO (keys by scancode, mouse, wheel, UTF-8 text,
     focus); clipboard through `platform::Clipboard`; IME position and text-input start/stop
     through `platform::Window`; display size and scale from the window;
   - *renderer side:* `ImDrawData` → `renderer::DrawList2D` in the render snapshot, and ImGui
     texture requests → `renderer::RenderResources` commands (create, update sub-rectangle,
     destroy). The renderer's generic 2D pass draws them; it knows nothing about ImGui.
2. **Version and source:** the vcpkg `imgui` port at the baseline (1.93.0), **master branch,
   without docking** and with no binding features. The backend sets
   `ImGuiBackendFlags_RendererHasTextures` (dynamic textures) and `RendererHasVtxOffset`.
   Docking and multi-viewports are reconsidered when the tools milestone needs them (they need
   OS windows per viewport, a separate design).
3. **Configuration:** the default `imconfig.h` of the port (`ImTextureID` is 64-bit, enough to
   carry `renderer::TextureId`; 16-bit `ImDrawIdx`, widened to 32 bits during conversion).
   Behavior is overridden at run time through `ImGuiPlatformIO` callbacks, not by compile-time
   defines, so the consumer never disagrees with how the library was built. Fonts use ImGui's
   built-in rasterizer (stb_truetype); the FreeType feature is reconsidered with the renderer's
   text work.
4. **The global context is accepted as a documented exception.** Each `debug::DebugOverlay`
   owns one `ImGuiContext` and makes it current at the start of every call that touches ImGui
   (so several contexts, e.g. tools with several overlays, stay correct). ImGui is used **only on
   the main thread**; this is part of the `debug` API contract and asserted in debug builds.
5. **ImGui is re-exported to games and tools.** `ezmu::debug` links ImGui publicly so games can
   write panels with the ImGui API (this is the explicit permission ADR 0008 requires for
   third-party types). ezMU's own public headers do not include `imgui.h`. Inside the
   repository, only `engine/debug` and `tools/` may include ImGui headers — a new
   `architecture.rules` check enforces it.
6. **Excluded from shipping builds:** a CMake option `EZMU_DEBUG_TOOLS` (default `ON` in every
   current preset) controls whether `runtime` installs the overlay and links `ezmu::debug`. A
   game's shipping configuration sets it to `OFF`; no ImGui code is then linked into the game.
7. **No ImGui Test Engine.** It is distributed under its own license rather than MIT; overlay
   behavior is tested with injected platform events, draw-data checks and reference images.

## Consequences

- ImGui fits the existing boundaries: no Vulkan or SDL outside their modules, no renderer access
  to game state, the debug overlay is just another producer of 2D draw lists.
- Two backends (~a few hundred lines) are ours to maintain across ImGui updates; ImGui version
  bumps are baseline bumps and must be checked against its changelog (backend API changes are
  common between minor versions).
- The renderer gains a `TextureUpdate` (sub-rectangle) resource command for dynamic textures
  (renderer spec, part 2).
- Display scaling is handled by ImGui's dynamic fonts plus the 2D list scale, so the overlay
  stays sharp at 150 % DPI.
- A new rule in `scripts/checks/check_rules.cmake`: `imgui` headers only under `engine/debug`
  and `tools/`.
- Games that want ImGui in shipping builds (e.g. an in-game console) must turn
  `EZMU_DEBUG_TOOLS` on knowingly; ezMU's default policy keeps debug UI out of shipping games.

## Alternatives considered

- **Official `imgui_impl_vulkan` / `imgui_impl_sdl3` inside `rhi` / `platform`** — less code,
  but the RHI and platform would depend on ImGui, the renderer would draw from the main thread's
  ImGui state (violates ADR 0012), and a third-party UI library would sit in L1.
- **Official backends in `debug` with an exception to the include rules** — breaks the
  strongest architecture rules for convenience.
- **Docking branch now** — useful for tools, not needed by the overlay; adds the multi-viewport
  surface to design now.
- **Vendor ImGui into `third_party/` with a custom `imconfig.h`** (e.g. a thread-local context
  pointer) — full control of compile-time options, but leaves the vcpkg-only dependency path
  and must be updated by hand.
- **In-house debug UI** — far more work than integrating the de facto standard.

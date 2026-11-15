# 0006. Vulkan 1.3 behind a thin RHI

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

The engine needs a modern, explicit, cross-platform graphics API. Vulkan is available on
Windows and Linux (and macOS through MoltenVK). Vulkan is verbose; if its types spread across
the codebase, every module becomes coupled to it and the renderer becomes hard to evolve.

Vulkan 1.3 promotes to core features that simplify an engine considerably:
dynamic rendering (no render pass / framebuffer objects), synchronization2 (clearer barriers),
and — already core since 1.2 — descriptor indexing (bindless resources).

## Decision

- We will target **Vulkan 1.3** as the minimum version, requiring dynamic rendering,
  synchronization2 and descriptor indexing.
- All Vulkan usage lives behind a **thin RHI** (Rendering Hardware Interface) module in
  `engine/rhi`: buffers, textures, samplers, pipelines, command lists, queues, swapchain,
  synchronization.
- The RHI is shaped around Vulkan's model; it is not a lowest-common-denominator abstraction
  over many APIs. A second backend is possible later but is not a goal.
- Loader: **volk**. Memory: **Vulkan Memory Allocator (VMA)**.
- Validation layers are enabled in debug builds and in tests; a validation error fails tests.

## Consequences

- No module outside `engine/rhi` may include `vulkan.h` or `volk.h`. This is enforced by
  `scripts/checks/check_rules.cmake`.
- Requires GPUs from roughly 2015–2016 onward with up-to-date drivers.
- The renderer is written against RHI handles, which also enables a headless/offscreen
  device for rendering tests.

## Alternatives considered

- **Vulkan 1.2 + optional extensions** — wider hardware support, but multiple code paths in the
  RHI that are harder to test.
- **OpenGL** — simpler, but legacy and poorly suited to a dedicated render thread.
- **Direct3D 12** — Windows only.
- **Thick multi-API RHI from day one** — large cost with no current need.

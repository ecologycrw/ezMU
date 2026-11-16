---
paths:
  - "engine/renderer/**"
---

# renderer module

Source: ADR 0012 (render thread), [renderer spec](../../docs/specs/renderer.md). Threading rules
in `threading.md` also apply.

- The renderer never reads ECS or game state. Everything it draws comes from a
  `RenderSnapshot`; new render features extend the snapshot (plain data), not the renderer's
  inputs.
- Producer headers (`snapshot.hpp`, `feedback.hpp`, `snapshot_ring.hpp`, `draw_2d.hpp`,
  `resources.hpp`) depend only on `foundation`: no `rhi` include. Only `renderer.hpp` names rhi
  types. Vulkan never appears anywhere here (it is private to `rhi`).
- No per-frame allocation once warm: snapshot containers are cleared (never shrunk) by
  `reset()`, feedback reports use a fixed ring, and GPU resources are recreated only when the
  output changes or a capture needs a new buffer.
- Blocking GPU waits (`waitIdle`) only for captures and destruction, never per frame.
- Errors returned by `Renderer::render` are fatal for the renderer; recoverable problems
  (a failed capture, later a failed texture) travel in the `FrameResult`.
- GPU tests: tag `[gpu]`, use `rhi::test::makeTestDevice` with a `ValidationGuard` declared
  first, feed synthetic snapshots to an offscreen `Renderer`, and compare readbacks exactly
  (UNORM8-representable colors) or against reference images in `tests/reference/`.

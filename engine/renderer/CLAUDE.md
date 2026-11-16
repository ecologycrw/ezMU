# ezmu::renderer (L2)

Turns **render snapshots** into GPU work on the render thread (ADR 0012). Spec:
[docs/specs/renderer.md](../../docs/specs/renderer.md) (part 1 implemented in M2 batch A; part 2,
2D draw lists and textures, follows the rhi M2 update in batch B).

## Responsibility

Snapshot types (`RenderSnapshot`, `OutputState`), the game → render exchange (`SnapshotRing`,
bounded FIFO with back-pressure), the render → game channel (`RenderFeedbackQueue`: frame
reports, captures, fatal errors) and the `Renderer` (offscreen target or swapchain, clear,
present, capture to `foundation::Image`). Later: 2D draw lists and textures (part 2), then
scene rendering.

Not here: threads and frame timing (→ `runtime`), windows and events (→ `platform`), Vulkan
(→ `rhi`), ImGui (→ `debug`), writing capture files (the caller, e.g. runtime with a job).

## Public API

| Header | Contents |
|---|---|
| `snapshot.hpp` | `OutputState`, `RenderSnapshot` (`reset`) — foundation types only |
| `snapshot_ring.hpp` | `SnapshotRing` (`beginWrite`/`endWrite` producer, `beginRead`/`endRead` consumer, `close`) |
| `feedback.hpp` | `FrameReport`, `CaptureResult`, `RenderFeedbackQueue` (`pushReport`, `pushCapture`, `pushFatalError`, `drain`) |
| `renderer.hpp` | `RendererDesc`, `FrameResult`, `Renderer` (`create`, `render`, `cancelPendingCapture`, `offscreenTarget`) — the only header naming rhi types |

## Allowed dependencies

`ezmu::foundation` and `ezmu::rhi` (both public). No `platform`: output size and state arrive in
the snapshot. No third-party library.

## Pitfalls

- The renderer never reads ECS/game state; everything it needs is in the snapshot.
- Producer headers (`snapshot.hpp`, `feedback.hpp`, `snapshot_ring.hpp`, later `draw_2d.hpp`,
  `resources.hpp`) must not include rhi headers.
- `Renderer` follows the `rhi::Device` thread rule: after creation, only the render thread uses
  and destroys it, and it must be destroyed before the Device (and the swapchain's window).
- `SnapshotRing`: exactly one producer and one consumer; every non-null `begin*` must be paired
  with its `end*` before the next `begin*`. Call `close()` when the render thread fails so the
  game thread never blocks forever.
- A capture on a skipped frame stays pending until a frame is rendered; call
  `cancelPendingCapture()` before destroying the renderer. Capture waits for the GPU
  (`waitIdle`) — captured frames only, never per frame.
- `FrameReport::waitTime` is filled by the render loop (runtime), not by `Renderer::render`.
- GPU tests reuse `engine/rhi/tests/rhi_test_support.hpp` (include path set in CMake; its
  reference-image macros point to `engine/renderer/tests/reference/`).

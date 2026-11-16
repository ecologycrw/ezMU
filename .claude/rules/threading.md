---
paths:
  - "engine/runtime/**"
  - "engine/renderer/**"
  - "engine/foundation/**/jobs*"
---

# Threading

Source: ADR 0012 (render thread), [jobs spec](../../docs/specs/jobs.md),
[runtime spec](../../docs/specs/runtime.md), [renderer spec](../../docs/specs/renderer.md).

- Every public function states the thread(s) that may call it (`Thread-safety:` in its `///`
  comment). "Any thread" is a promise that needs a test.
- The main (game) thread and the render thread share only the snapshot ring, the feedback
  queue and documented atomics. The renderer never reads ECS/game state.
- Every `std::atomic` operation carries a comment with its memory order and why
  (`// acquire: pairs with …`). `relaxed` only for statistics or values that do not publish
  other data.
- The render thread blocks only on the snapshot ring, vsync (present) and frame captures.
  Waiting on jobs from it is allowed but must stay short (it can stall a frame).
- No mutable globals and no `thread_local` state for services; pass the `JobSystem&` and other
  services explicitly.
- A job must not outlive what it captures: wait for its `JobGroup` (or let the group's owner
  outlive it). Captures larger than `kJobStorageSize` do not compile; capture a pointer.
- Tests synchronize with `std::latch` / `std::barrier` and the APIs under test — never with
  sleeps or timing assumptions.
- ThreadSanitizer must stay clean: the `linux-clang-tsan` CI job runs every non-GPU test. Never
  suppress a TSan report in ezMU code; fix the race.

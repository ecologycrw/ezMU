# 0012. Dedicated render thread

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

Splitting rendering into its own thread after the fact is notoriously hard: it touches every
system that hands data to the renderer. Planning it from day one costs little and shapes the
renderer API correctly.

## Decision

- The engine runs **game logic** on the main thread and **rendering** on a dedicated render
  thread, plus a job system for parallel work.
- Each frame has an **extraction** phase: game-side systems produce an immutable **render
  snapshot** (transforms, meshes, materials, effects, UI draw data, camera).
- The render thread consumes snapshot N while the game thread simulates frame N+1.
- The renderer **never** reads the ECS or game state directly; it only reads snapshots.
- The renderer API is submission-based (record/submit data), not immediate-mode calls from
  gameplay code.

## Consequences

- Clear data ownership between threads; no locks on game state for rendering.
- One frame of additional latency between simulation and display, acceptable for the genre.
- Debug tooling must account for two timelines (game frame vs. render frame).
- The renderer can be tested in isolation by feeding it synthetic snapshots.

## Alternatives considered

- **Single-threaded main loop, split later** — simpler start, very expensive migration.
- **Fully job-based rendering without a dedicated thread** — more complex scheduling for little
  benefit at the genre's scale.

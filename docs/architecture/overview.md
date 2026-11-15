# Architecture overview

This document is the map of ezMU. The reasoning behind each part is in the
[ADRs](../adr/README.md); this page describes the resulting structure.

## Layers

```
┌─────────────────────────────────────────────────────────────┐
│  L6  GAMES           Games built on ezMU  ·  Sample game     │
├─────────────────────────────────────────────────────────────┤
│  L5  TOOLS           Editor framework · Map editor ·         │
│                      Effect editor · Model viewer · Cooker   │
├─────────────────────────────────────────────────────────────┤
│  L4  FRAMEWORK       Genre modules (terrain, character,      │
│                      items, effects, replication, ...)       │
├─────────────────────────────────────────────────────────────┤
│  L3  RUNTIME         Application · Game loop · Game states · │
│                      Composition root · Event bus            │
├─────────────────────────────────────────────────────────────┤
│  L2  SERVICES        Assets · Renderer · Animation · Audio · │
│                      Input · UI · World/ECS · Net · Debug    │
├─────────────────────────────────────────────────────────────┤
│  L1  RHI / PLATFORM  RHI (Vulkan 1.3) · Platform (SDL3)      │
├─────────────────────────────────────────────────────────────┤
│  L0  FOUNDATION      Types · Errors · Memory · Containers ·  │
│                      Log · Math · VFS · Jobs · Reflection    │
└─────────────────────────────────────────────────────────────┘
        Protocol adapters ── plug into ──▶ Net (L2) / Session (L4)
```

- **ezMU Engine** = L0–L3 (`engine/`). Knows nothing about the genre.
- **ezMU Framework** = L4 (`framework/`). Built only on the Engine's public API.

## Dependency rules

1. Dependencies point **downward only**. Same-layer dependencies must be declared explicitly.
   No cycles.
2. Each module is declared with `ezmu_add_module(NAME <n> LAYER <l> ...)`
   ([cmake/EzmuModule.cmake](../../cmake/EzmuModule.cmake)). Configure fails if a module depends
   on a higher layer.
3. Only public headers (`include/ezmu/<module>/`) may be included from outside a module.
4. Vulkan headers only in `engine/rhi`; SDL headers only in `engine/platform`; vcpkg is never
   referenced by module build files. Checked by
   [scripts/checks/check_rules.cmake](../../scripts/checks/check_rules.cmake).
5. Tools and Framework use only public APIs.
6. The INTERFACE target `ezmu::engine` (declared with `ezmu_add_aggregate`) bundles every
   Engine module for executables and external consumers. It is not a module, and modules may
   not depend on it: they declare the specific modules they use.

## Planned modules

| Layer | Module | Responsibility |
|---|---|---|
| L0 | `foundation` | Core types, `Result`, asserts, memory, containers, logging, math, VFS, jobs, reflection, serialization, profiling hooks |
| L1 | `platform` | Window, raw input, text/IME, clipboard, dialogs, timers (SDL3) |
| L1 | `rhi` | Vulkan 1.3 abstraction: devices, resources, pipelines, commands, sync, swapchain |
| L2 | `assets` | Asset database, handles, async loading, importers, native/cooked formats, hot-reload |
| L2 | `renderer` | Render graph, materials, meshes, skinning, terrain, particles/trails, 2D, text |
| L2 | `animation` | Skeletons, clips, blending, attachment points |
| L2 | `audio` | 2D/3D sound, music streaming, buses |
| L2 | `input` | Action mapping and rebinding |
| L2 | `ui` | Retained-mode UI: layout, widgets, themes, focus, text input |
| L2 | `world` | ECS (EnTT) + facade, transforms, system scheduling, scenes |
| L2 | `net` | Transport, connections, framing, protocol adapter interface, dispatch |
| L2 | `debug` | ImGui overlay, inspectors (never in release builds) |
| L3 | `runtime` | Application lifecycle, main loop, game states, composition root, event bus |
| L4 | `terrain`, `navigation`, `camera`, `controller`, `character`, `items`, `effects`, `replication`, `session`, `social`, `gameui` | Genre mechanisms (see [ADR 0011](../adr/0011-layered-architecture-engine-and-framework.md)) |

## Threads

- **Main (game) thread:** input, network dispatch, simulation, extraction of the render
  snapshot.
- **Render thread:** consumes render snapshots, records and submits GPU work.
- **Job system workers:** asset loading, decoding and parallel tasks.

See [ADR 0012](../adr/0012-dedicated-render-thread.md).

## Network flow

```
 Socket (asio)        net (L2)                 Protocol adapter            Framework (L4)
 ┌────────┐     ┌───────────────────┐     ┌────────────────────────┐    ┌──────────────┐
 │ bytes  │ ──▶ │ Transport/framing │ ──▶ │ decode: bytes → domain │ ─▶ │ session      │
 │        │ ◀── │ (IFrameCodec)     │ ◀── │ encode: intent → bytes │ ◀─ │ replication  │
 └────────┘     └───────────────────┘     └────────────────────────┘    │ controller   │
                                              ▲ replaceable              └──────────────┘
```

See [ADR 0014](../adr/0014-ports-and-adapters-networking.md).

## Module layout

```
engine/<module>/
├── include/ezmu/<module>/   # public API
├── src/                     # private implementation
├── tests/                   # unit tests (Catch2)
├── CMakeLists.txt           # ezmu_add_module(...) with declared dependencies
└── CLAUDE.md                # responsibility, public API, allowed deps, pitfalls
```

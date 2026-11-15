# 0011. Layered architecture: Engine and Framework

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

ezMU must be specialized in one genre and, at the same time, agnostic of any particular game.
It must be extensible without forking. Without strict boundaries, genre-specific or
game-specific code leaks into low-level modules, and modules become entangled.

## Decision

We will organize the codebase in **layers with downward-only dependencies**:

| Layer | Name | Contents |
|---|---|---|
| L0 | Foundation | Types, errors, memory, containers, logging, math, VFS, jobs, reflection, serialization |
| L1 | RHI / Platform | Vulkan RHI; SDL3 platform |
| L2 | Engine services | Assets, renderer, animation, audio, input, UI, world (ECS), net, debug |
| L3 | Runtime | Application lifecycle, game loop, game states, composition root, event bus |
| L4 | Genre framework | Terrain, navigation, camera, controller, character, items, effects, replication, session, social, game UI |
| L5 | Tools | Editor framework, map editor, effect editor, model viewer, asset cooker |
| L6 | Games / samples | Games built on ezMU and the official sample game |

Protocol adapters (`protocols/`) plug into `net` (L2) and the framework's session module (L4).

The repository contains two products:

- **ezMU Engine** — L0–L3, under `engine/`. Has no notion of the genre.
- **ezMU Framework** — L4, under `framework/`. Built **only on the Engine's public API**.

### Rules

1. A module may depend only on modules in **lower** layers, plus explicitly declared
   same-layer dependencies. No cycles.
2. Each module is a CMake target `ezmu::<name>` declared with `ezmu_add_module(... LAYER n)`;
   configure fails on a dependency that points to a higher layer.
3. Public headers live in `include/ezmu/<module>/`; implementation in `src/`. Code outside a
   module may include only its public headers.
4. **Mechanism vs. policy:** the Framework provides mechanisms common to the genre (a grid
   inventory exists, items occupy W×H cells); games define policy (which items exist, their
   options, inventory size).
5. Framework modules are **optional and independent**: using one does not force using another.
6. Games customize at three levels: **configure** (data), **extend** (components, systems,
   hooks, widgets) and **replace** (implement an interface or skip a module).

## Consequences

- The Framework proves the Engine is extensible: if it can be built on the public API, so can
  any third party's framework.
- Layer violations are build errors, not review comments.
- Some features need careful splitting between Engine (generic) and Framework (genre).

## Alternatives considered

- **Single monolithic engine library** — simpler at first, entangled quickly.
- **Genre code inside games only** — every developer would rebuild the same genre systems.

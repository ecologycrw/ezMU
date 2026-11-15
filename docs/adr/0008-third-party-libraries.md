# 0008. Third-party libraries

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

"An engine of our own" does not require rewriting solved, low-level problems. Mature,
permissively licensed libraries let effort go into what makes ezMU distinct.

## Decision

We adopt the following libraries, each wrapped by (and only used inside) the module listed:

| Library | Purpose | Owning module(s) |
|---|---|---|
| SDL3 | Window, input, text/IME, clipboard | `platform` |
| volk | Vulkan loader | `rhi` |
| Vulkan Memory Allocator | GPU memory | `rhi` |
| glm | Math types and functions | `foundation` (math wrapper) |
| EnTT | ECS | `world` |
| asio (standalone) | Networking transport | `net` |
| FreeType + HarfBuzz | Font rasterization and text shaping | `renderer` (text) |
| Dear ImGui | Debug UI and tools | `debug`, `tools` |
| Tracy | Profiling | `foundation` (profiling hooks) |
| miniaudio | Audio | `audio` |
| Catch2 v3 | Unit and integration tests | tests only |

Rules:

- Libraries are added to `vcpkg.json` **when the module that owns them is created**, not in
  advance.
- Every new dependency requires an ADR (or an amendment to this one) stating its purpose,
  owning module and license.
- Licenses must be compatible with distributing ezMU under MPL-2.0.
- Third-party types do not appear in public headers of other modules unless the owning
  module's ADR allows it (e.g. glm math types may be re-exported by `foundation`).

## Consequences

- Swapping a library affects only its owning module.
- Public APIs remain ezMU types, keeping the engine's surface stable for game developers.

## Alternatives considered

- **Writing everything in-house** — enormous cost without benefit to users.
- **Large frameworks (e.g. Boost)** — heavier than needed; specific libraries suffice.

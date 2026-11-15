# 0016. Tools live in the repository and use only the public API

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

Content creation needs tools (map/terrain editor, effect editor, model viewer, asset cooker).
A full general-purpose editor is a project of its own and would have to be rewritten whenever
the runtime changes. Tools kept in a separate repository would break whenever the engine API
changes, forcing API stability before the engine is mature.

## Decision

- We will **not** build a full general-purpose editor initially.
- We will build **focused tools** when needed, in priority order: in-game debug tools (ImGui),
  map/terrain editor, effect editor, model/animation viewer, asset cooker (CLI).
- Tools live **in this repository** (`tools/`, layer L5), versioned and released with the
  engine.
- Tools use **only the public API** of Engine and Framework, as an external project would.
  A tool needing internals indicates a gap in the public API, to be fixed there.
- Editing code never ships in a game's runtime binary.

### Architectural prerequisites (from day one)

Component reflection and serialization; headless world creation without the game loop;
runtime/tooling separation; edits as commands (undo/redo); asset hot-reload; source vs. cooked
formats ([ADR 0015](0015-content-agnostic-native-formats.md)).

## Consequences

- Engine and tools evolve together without breakage.
- Tools act as the first real consumers of the public API.
- Tools can later be split into another repository with little effort if needed.

## Alternatives considered

- **Full integrated editor first** — huge scope, constant rework while the runtime matures.
- **Tools in a separate repository** — requires early API stability.
- **No tools at all** — creating maps without an editor would deter most developers.

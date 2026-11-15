# 0015. Content-agnostic engine with native formats

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

ezMU does not ship game content and does not read any third-party proprietary format
([ADR 0002](0002-scope-client-and-network-only.md)). Still, games need models, animations,
terrain, textures, effects and data definitions.

## Decision

- The engine defines its **own native formats** for every content type it consumes (models,
  skeletons, animations, terrain, attribute maps, textures, effects, UI layouts, data
  definitions).
- Each format has two levels:
  - a **source** form — text-based where practical, diffable, versionable;
  - a **cooked** form — binary, optimized for loading at runtime.
- The `assets` module exposes an **`IAssetImporter`** interface. Importers and converters for
  any external format are written by each developer, outside the engine.
- Importers can run **offline** (convert to native/cooked files with the cooker tool) or at
  **runtime** (registered loaders), at the developer's choice.
- General 3D content enters through standard interchange formats (e.g. glTF from a DCC tool
  such as Blender) and is converted to native formats.

## Consequences

- The native format design is one of the most important decisions of the project; each format
  gets its own spec and ADR before implementation.
- The engine stays independent of any content source.
- A cooker tool (L5) and format validators are needed early.

## Alternatives considered

- **Consume third-party formats directly in the engine** — couples the engine to external
  formats and to their owners.
- **Use glTF as the runtime format for everything** — does not cover terrain, attribute maps,
  effects or data definitions, and is not optimal for loading.

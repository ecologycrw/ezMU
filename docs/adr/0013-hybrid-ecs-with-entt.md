# 0013. Hybrid ECS with EnTT and a facade

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

Characters in the genre are composition-heavy: equipment parts, wings, pets, auras, glow by
item level, buffs, mounts. Third parties must add new kinds of entity behavior without forking
the engine. An Entity-Component-System model fits this, but "pure ECS everywhere" makes UI,
networking and assets awkward and raises the learning curve for developers used to OOP code.

## Decision

- The **world** (characters, monsters, NPCs, ground items, projectiles, effects) uses an ECS
  built on **EnTT**, owned by `engine/world`.
- **Not everything is ECS:** UI, networking, assets, audio and scenes are ordinary services.
- A **facade** API over the registry covers common use (e.g. `world.spawn<Archetype>(...)`,
  `entity.equip(...)`, event subscriptions); the raw registry remains available for advanced use.
- Components are plain data and must be **reflectable and serializable** (for tools, debugging
  and networking). Systems hold no hidden state and run in an explicit, documented order.
- Games extend the world by registering their own components and systems.

## Consequences

- New mechanics are added by composition, without engine changes.
- System ordering must be explicit and visible to avoid subtle bugs.
- Performance is not the main argument at the genre's scale (hundreds of entities); the
  benefits are organization and extensibility.
- EnTT's sparse-set storage suits frequent component add/remove (buffs, states).

## Alternatives considered

- **Classic OOP entity hierarchy** — familiar, but rigid composition and deep inheritance.
- **Pure ECS for all engine systems** — unnecessary complexity for UI/net/assets.
- **In-house ECS** — no benefit over EnTT for now; revisit only with evidence.

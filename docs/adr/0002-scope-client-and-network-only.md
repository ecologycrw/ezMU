# 0002. Scope: client-side and network only

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

ezMU aims to be the technological foundation on which many independent developers build their
own classic MMORPG clients in the style of MU Online. Each of them will have their own server,
rules, content and design decisions. An engine that imposed a server, a rule set or a content
pack would conflict with that goal.

## Decision

We will build an engine responsible **exclusively** for:

1. **The client side:** rendering, UI, assets, audio, animation, entities/world, scenes, input,
   visual systems and every other part of the player experience.
2. **The network/protocol bridge:** transport, framing, serialization, deserialization and
   dispatch of messages between the client and an external server, through contracts that any
   server implementation can satisfy (see [ADR 0014](0014-ports-and-adapters-networking.md)).

ezMU is **specialized** in one genre (classic MMORPGs in the style of MU Online) and
**agnostic** of any specific game version, rule set, configuration, protocol or content.

### Non-goals

ezMU explicitly does **not** provide:

- A game server or server-side framework. Developers build their own servers.
- Game rules, balance, progression or content. These belong to each game.
- Readers or converters for any third-party proprietary content format. These belong to each
  developer (see [ADR 0015](0015-content-agnostic-native-formats.md)).
- A physics engine. The genre needs only terrain raycasts, picking and simple spatial queries.
- Anti-cheat. Security comes from server authority (see
  [ADR 0017](0017-client-server-authority-model.md)).
- A general-purpose engine for arbitrary genres.

## Consequences

- Every feature request can be checked against this scope; "does a client need this, for any
  game of the genre?" is the test.
- A development-only mock server may exist under `tools/` for testing, but it is never a
  product and never a reference server.
- The genre framework must express mechanisms, not policy
  (see [ADR 0011](0011-layered-architecture-engine-and-framework.md)).

## Alternatives considered

- **Full stack (client + server)** — would impose a backend architecture on every user and
  double the scope.
- **General-purpose engine** — loses the value of genre specialization; Godot/Unreal already
  exist.

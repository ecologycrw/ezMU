# 0014. Networking with ports and adapters

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

ezMU must talk to servers it does not control. Developers may implement their own protocol or
use one offered by the ecosystem. The genre framework must not depend on any wire format.

## Decision

Networking is split into three layers:

1. **Transport** (`engine/net`, L2): connections, reconnection, TCP via standalone asio,
   framing through an `IFrameCodec` interface, and delivery of received messages to the main
   thread at a defined point of the frame.
2. **Protocol adapters** (`protocols/*`): implement `IProtocolAdapter`, translating between
   bytes and **domain messages**, in both directions (decode server bytes into events; encode
   client intents into bytes). Encryption/compression, if any, belong to the adapter.
3. **Domain** (`framework/session`, L4): typed domain messages such as `EntitySpawned`,
   `ItemDropped`, `DamageReceived` (server → client) and `MoveRequest`, `UseSkill`,
   `MoveItem` (client → server). The framework only sees domain messages.

Additionally:

- Developers may write their own adapter, or use an ecosystem-provided **reference protocol**.
- Protocol definitions should be data (schemas) with generated code where practical, so
  protocol variants become schema changes.
- The client–server contract also covers **content data** (item, skill, map and monster
  definitions) that must match the server; the engine provides a mechanism to verify it
  (e.g. version/hash exchange at login).

## Consequences

- Swapping protocols means swapping an adapter; the framework is untouched.
- Adapters are unit-testable with recorded byte streams.
- Domain message design becomes a key API surface of the Framework and must be versioned.

## Alternatives considered

- **Framework reads packets directly** — couples the genre layer to one protocol.
- **Mandatory engine protocol** — contradicts the goal of server independence.

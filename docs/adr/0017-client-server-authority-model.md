# 0017. Client–server authority model

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

The client must be responsive while the server stays the source of truth. Some game knowledge
is needed on the client (tooltips, requirements, ranges) without the client being trusted.
The engine must support this without imposing one specific policy.

## Decision

The engine follows the industry-standard principle: **the client sends intents, the server
decides facts, the client presents and predicts.**

| Category | Examples | Who decides |
|---|---|---|
| Server-authoritative | Damage, drops, inventory, stats, currency, trades, item creation | Server only |
| Client-predicted, server-validated | Movement/pathing, animation start, cast visuals | Client anticipates; server confirms or corrects |
| Client-computed for display only | Tooltips, requirement checks to enable UI, skill range | Client; the server re-checks |
| Never on the client | Anything with value | — |

The engine/framework provides **mechanisms**; each game chooses its **policy**:

- intent-based client messages ([ADR 0014](0014-ports-and-adapters-networking.md));
- **interpolation** of remote entities via a snapshot buffer;
- **prediction and reconciliation** for the local player, with smooth correction;
- **request tracking** (request → ack → timeout) supporting optimistic or pessimistic UI;
- a **content-data contract** check between client and server.

## Consequences

- Cheating that relies on client authority is structurally prevented for server-authoritative
  data; ezMU does not attempt anti-cheat.
- The Framework's replication module is a core piece and needs careful testing with simulated
  latency and packet loss.

## Alternatives considered

- **Client-authoritative gameplay** — trivial to cheat.
- **Fully pessimistic client (no prediction)** — safe but unresponsive.

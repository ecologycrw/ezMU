# Architecture rules

Source: ADR 0011 (layers), 0014 (networking), 0015 (content), 0016 (tools), 0017 (authority).

## Placement test — before writing code, decide where it belongs

1. Does every game of the genre need it, regardless of rules? If **no**, it belongs in a game (L6),
   not in ezMU.
2. Is it genre-specific (terrain grid, inventory grid, character composition, replication)?
   → Framework (L4). Otherwise → Engine (L0–L3).
3. Pick the **lowest** layer that can own it without depending upward.

## Mechanism vs. policy

- Engine/Framework expose mechanisms: data-driven definitions, extension points, interfaces.
- Never hard-code game policy (item lists, class names, formulas, protocol opcodes) in ezMU code.
- Every Framework feature must support the three customization levels: configure (data),
  extend (components/systems/hooks), replace (interface or skip the module).

## Boundaries

- Cross-module communication through public interfaces or events, never internals.
- Pass services explicitly (constructor/context); no service locators hidden in globals.
- Use handles (`TextureHandle`) rather than raw pointers across module boundaries.
- The Framework sees **domain messages** only; bytes live in protocol adapters.
- The client sends intents; the server decides facts. Never make the client authoritative over
  anything with value.
- Tools and Framework use only public APIs. If you need an internal, extend the public API instead.

## When unsure

Write or update a spec in `docs/specs/` and ask the owner before implementing.

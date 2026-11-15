# 0001. Record architecture decisions

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

ezMU is a long-lived engine developed largely with AI assistance. Neither contributors nor AI
agents keep the full history of the project in mind between sessions. Without a durable
record, decisions get re-litigated, or silently violated, months after they were made.

## Decision

We will record every significant architectural or process decision as an Architecture
Decision Record (ADR) in `docs/adr/`, using the format in [0000-template.md](0000-template.md).

- ADRs are numbered sequentially and never renumbered.
- An accepted ADR is not edited in substance. To change a decision, write a new ADR that
  supersedes it and update the old one's status.
- The index in [README.md](README.md) lists every ADR and its status.
- A decision is "significant" when it affects module boundaries, public API, dependencies,
  build/CI, file formats, protocols or development workflow.

## Consequences

- New contributors (human or AI) can learn *why* the project is shaped the way it is.
- Changing a decision has a small, deliberate cost: writing a superseding ADR.
- Reviews can reject changes that contradict an accepted ADR without one.

## Alternatives considered

- **Wiki or external docs** — drift from the code and are not reviewed with it.
- **Comments in code only** — cannot capture cross-cutting decisions or rejected alternatives.

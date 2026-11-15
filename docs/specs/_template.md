# Spec: <module or feature name>

- **Status:** Draft | In review | Approved | Implemented
- **Layer:** L0–L6
- **Owner:** <name>
- **Related ADRs:** <links>

## Purpose

One paragraph: what this module/feature is responsible for, and what it is **not**
responsible for.

## Use cases

Concrete scenarios from the perspective of its users (other modules, game developers, tools).

## Public API

Sketch of the public types and functions (headers under `include/ezmu/<module>/`).
Note ownership, lifetimes, threading constraints and error reporting (`Result<T>`).

## Dependencies

- Allowed module dependencies (must respect layering).
- Third-party libraries (must be listed in ADR 0008).

## Extension points

How games configure, extend or replace this module (mechanism vs. policy).

## Data formats / messages

Any file formats, schemas or domain messages introduced.

## Testing strategy

Unit tests, integration tests, reference images, recorded streams, performance checks.

## Done when

- [ ] Acceptance criterion 1
- [ ] Acceptance criterion 2
- [ ] Tests pass on all CI platforms
- [ ] Module `CLAUDE.md` written

## Open questions

- …

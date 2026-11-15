# 0018. Code, commit and branch conventions

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

Consistency matters more than any particular style, especially when most code is written with
AI assistance and reviewed by humans. Conventions must be automated wherever possible.

## Decision

### Code

| Element | Convention | Example |
|---|---|---|
| Types, concepts, enums | `PascalCase` | `TextureHandle` |
| Functions, variables, parameters | `camelCase` | `loadTexture`, `frameIndex` |
| Private/protected data members | `m_` + `camelCase` | `m_device` |
| Constants and enumerators | `k` + `PascalCase` | `kMaxFramesInFlight` |
| Mutable globals (rare, must be justified) | `g_` + `camelCase` | `g_assertionHandler` |
| Macros | `EZMU_` + `UPPER_SNAKE_CASE` | `EZMU_ASSERT` |
| Namespaces | `ezmu::<module>` (lowercase) | `ezmu::renderer` |
| Files | `snake_case`, `.hpp` / `.cpp` | `texture_handle.hpp` |

- Headers use `#pragma once`. Public headers are included as
  `#include <ezmu/<module>/<file>.hpp>`.
- **Error handling:** recoverable errors are returned as `ezmu::foundation::Result<T>`
  (`std::expected`). Engine code does not use exceptions for control flow. Programming errors
  are caught with `EZMU_ASSERT`.
- No global mutable state or singletons; services are passed explicitly.
- Formatting is defined by `.clang-format` (100 columns, 4 spaces) and applied automatically;
  static analysis by `.clang-tidy`.
- Warnings are errors on every compiler.

### Commits

- [Conventional Commits](https://www.conventionalcommits.org/): `<type>(<scope>): <summary>`,
  scope = module name when applicable.
- Every commit is signed off (DCO, [ADR 0004](0004-license-mpl-2-0-and-dco.md)).
- Small, focused commits.

### Branches

- **Trunk-based:** `main` always builds and passes tests.
- Short-lived branches merged through pull requests with mandatory CI.

## Consequences

- Hooks and CI enforce formatting, warnings and architecture rules; reviews focus on design.
- Conventional Commits allow automatic changelog generation.

## Alternatives considered

- **snake_case everywhere (std style)** — blurs the distinction between engine types and
  standard library types.
- **Git Flow** — too much ceremony for this team and workflow.

# ezmu::foundation (L0)

The base every other module uses. **Depends on nothing else in ezMU** and on no third-party
library yet (glm and Tracy will be wrapped here later, per ADR 0008).

## Responsibility

Core vocabulary types and utilities: version, `Result`/`Error`, assertions. Planned: logging,
memory/allocators, containers, math wrapper, VFS, job system, reflection, serialization,
profiling hooks.

Not here: anything OS-specific (→ `platform`), graphics (→ `rhi`), genre concepts (→ framework).

## Public API

| Header | Contents |
|---|---|
| `version.hpp` (generated from `src/version.hpp.in`) | `Version`, `kEngineName`, `kEngineVersion`, `kEngineVersionString` |
| `result.hpp` | `ErrorCode`, `toString(ErrorCode)`, `Error`, `Result<T>`, `makeError` |
| `assert.hpp` | `EZMU_ASSERT[_MSG]`, `EZMU_VERIFY[_MSG]`, `AssertionInfo`, handler get/set, `kAssertsEnabled` |

## Pitfalls

- `version.hpp` lives in the build tree; edit `src/version.hpp.in`, and the version itself in
  the root `project(... VERSION ...)`.
- The assertion handler is the single sanctioned mutable global; keep it that way.
- `EZMU_ASSERT` expressions are not evaluated in release builds — no side effects inside.
- `ErrorCode` values are a stable vocabulary; append new codes, never renumber.

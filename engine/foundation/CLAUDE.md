# ezmu::foundation (L0)

The base every other module uses. **Depends on nothing else in ezMU.** Third-party: stb
(private, PNG I/O, ADR 0021); glm and Tracy will be wrapped here later (ADR 0008).

## Responsibility

Core vocabulary types and utilities: version, `Result`/`Error`, assertions, geometry value
types, logging, CPU images with PNG I/O and comparison, job system. Planned: memory/allocators,
containers, math wrapper (glm), VFS, reflection, serialization, profiling hooks.

Not here: anything OS-specific (→ `platform`), graphics (→ `rhi`), genre concepts (→ framework),
game texture assets (→ `assets`).

## Public API

| Header | Contents |
|---|---|
| `version.hpp` (generated from `src/version.hpp.in`) | `Version`, `kEngineName`, `kEngineVersion`, `kEngineVersionString` |
| `result.hpp` | `ErrorCode`, `toString(ErrorCode)`, `Error`, `Result<T>`, `makeError` |
| `assert.hpp` | `EZMU_ASSERT[_MSG]`, `EZMU_VERIFY[_MSG]`, `AssertionInfo`, handler get/set, `kAssertsEnabled` |
| `geometry.hpp` | `Extent2D`, `Offset2D`, `Rect2D` |
| `log.hpp` | `LogLevel`, `LogRecord`, `ILogSink`, `StderrLogSink`, `Logger` (a service, no global) |
| `image.hpp` | `Rgba8`, `Image` (CPU RGBA8, tightly packed, top-left origin) |
| `image_io.hpp` | `encodePng`, `decodePng`, `writePng`, `readPng` |
| `image_compare.hpp` | `ImageCompareOptions`, `ImageComparison`, `compareImages` (reference-image tests) |
| `jobs.hpp` | `Job`, `JobCallable`, `kJobStorageSize`, `JobGroup`, `JobSystemDesc`, `kAutoWorkerCount`, `JobStats`, `JobSystem` (a service, no global; spec `docs/specs/jobs.md`) |

## Pitfalls

- `version.hpp` lives in the build tree; edit `src/version.hpp.in`, and the version itself in
  the root `project(... VERSION ...)`.
- The assertion handler is the single sanctioned mutable global; keep it that way. There is no
  global logger: pass a `Logger&` to whoever needs one.
- `EZMU_ASSERT` expressions are not evaluated in release builds — no side effects inside.
- `ErrorCode` values are a stable vocabulary; append new codes, never renumber.
- stb is included only from `src/image_io.cpp` (one implementation TU, system include path).
  Never include it from another file or a public header.
- Log sinks run under the Logger lock: a sink must not log.
- Jobs: a `JobGroup` must outlive its jobs and so must everything a job captures; wait for
  the group (its destructor asserts, then waits, if it is not done). Captures larger than
  `kJobStorageSize` (48 bytes) do not compile: capture a pointer to the data.
- Inline mode (`workerCount = 0`) runs jobs only in `wait`, `runPending` and the destructor:
  fire-and-poll work never completes unless the owner calls `runPending()` (once per frame).
- An exception escaping a job calls `std::terminate`. A full queue runs the job on the
  submitting thread (`JobStats::inlineOverflows`), so `submit` may take as long as the job.
- Threading rules for jobs live in `.claude/rules/threading.md`; TSan runs in CI only (Linux).

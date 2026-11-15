# 0021. stb for PNG image I/O in foundation

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

M1 needs to read and write PNG files: reference images for rendering tests and frame capture
for automated and AI-assisted visual checks ([ADR 0020](0020-gpu-tests-headless-and-software-vulkan-in-ci.md)).
Frame capture runs in applications (e.g. `ezmu_clear --capture out.png`), not only in tests,
so the code must be part of the engine. Per [ADR 0008](0008-third-party-libraries.md) every new
dependency needs an ADR. See the [rhi spec](../specs/rhi.md#decisions-approved-2026-11-15-every-proposal-below-was-accepted), decision D3.

## Decision

- We will use **stb_image** (decode) and **stb_image_write** (encode) from the `stb` package
  (public domain / MIT dual license, header-only) for PNG I/O.
- They are wrapped privately by **`foundation`**, which exposes ezMU types only: an `Image`
  (CPU, RGBA8, tightly packed), `readPng`, `writePng` and `compareImages`.
- `stb` is added to `vcpkg.json` in the commit that introduces the wrapper, and to the ADR 0008
  table.
- This is for engine-internal images (tests, captures, debug). Loading game textures is the job
  of `assets` importers and native formats ([ADR 0015](0015-content-agnostic-native-formats.md)),
  which may choose other libraries.

## Consequences

- Minimal footprint: no zlib or other transitive dependencies; the implementation is compiled
  in a single private `.cpp` of `foundation`.
- stb is not the fastest PNG encoder; acceptable for tests and occasional captures, not for
  streaming video capture.
- stb headers trigger warnings under strict flags; they are included only from that private
  `.cpp`, with targeted suppressions and a comment explaining why.

## Alternatives considered

- **libspng + zlib** — faster and stricter, but two libraries and more build surface.
- **libpng** — the reference implementation, heavier API and dependencies.
- **A separate L0 `image` module** — more structure than three functions need today; can be
  split out of `foundation` later without changing callers' types.
- **Test-only PNG support** — insufficient, since frame capture runs in applications.

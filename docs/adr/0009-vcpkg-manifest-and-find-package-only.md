# 0009. vcpkg in manifest mode; modules use `find_package` only

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

The engine depends on several native libraries ([ADR 0008](0008-third-party-libraries.md)),
built consistently on Windows and Linux, locally and in CI. A package manager must not become
a hard dependency of the code: if it ever disappears or must be replaced, the engine should
not need rewriting.

## Decision

- We will use **vcpkg in manifest mode**: `vcpkg.json` lists dependencies and
  `vcpkg-configuration.json` pins a **baseline** so every machine and CI build resolves the
  same versions.
- vcpkg is integrated **only** through the CMake toolchain file referenced by
  `CMakePresets.json` (via `VCPKG_ROOT`).
- **No module `CMakeLists.txt` may reference vcpkg.** Dependencies are located exclusively with
  standard `find_package(...)` calls and imported targets.
- CI uses vcpkg binary caching so dependencies are not rebuilt on every run.
- Small single-file libraries (e.g. volk, VMA, miniaudio) may be vendored into `third_party/`
  if ever needed.

## Consequences

- vcpkg can be replaced (Conan, system packages, manual builds) by changing presets and the
  toolchain only.
- `scripts/checks/check_rules.cmake` fails if a module `CMakeLists.txt` mentions vcpkg.
- The first configure on a clean machine is slow (dependencies are built from source); binary
  caching mitigates it.

## Alternatives considered

- **CPM.cmake** — simple, source-based, but dependencies become part of the project build and
  binary caching is weaker.
- **Conan** — capable, but adds a Python toolchain requirement.
- **Git submodules** — no versioning or transitive dependency handling.

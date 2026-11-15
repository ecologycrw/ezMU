# 0005. Target platforms and compilers

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

The audience of the genre plays almost entirely on Windows. Code compiled only on one platform
and one compiler accumulates hidden non-portable assumptions that are expensive to remove
later. Linux also opens the way to Steam Deck-class devices.

## Decision

- **Windows** (x64) is the primary platform, built with **MSVC** (Visual Studio 2026).
- **Linux** (x64) is supported from the first commit, built with **Clang (≥ 19)** and
  **GCC (≥ 14)**.
- **macOS** is not supported initially. Platform-specific code stays behind the platform and
  RHI layers so a MoltenVK-based port remains possible.
- CI builds and tests every supported platform/compiler combination on every change.

## Consequences

- Three compiler front-ends catch more bugs and non-standard code.
- CI time and occasional compiler-specific workarounds are the cost.
- OS-specific code lives only in `engine/platform` (and RHI backends); other modules are
  portable C++23.

## Alternatives considered

- **Windows only** — faster start, but creates portability debt.
- **Windows + Linux + macOS from day one** — macOS requires MoltenVK and extra CI cost before
  the engine has any users there.

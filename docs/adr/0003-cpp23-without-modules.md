# 0003. C++23 as the language, without C++ Modules

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

The engine, its tools and the games built on it share one base language. It must offer
predictable performance, mature tooling on Windows and Linux and direct access to Vulkan and
native libraries. Modern standard features (e.g. `std::expected`, `std::print`, `std::span`,
concepts, ranges) reduce the need for in-house utilities.

## Decision

We will use **C++23** for the engine, tools and samples, and it is the language games use to
build on ezMU.

- `CMAKE_CXX_STANDARD 23`, required, with compiler extensions disabled.
- We use C++23 library and language features that are supported by all compilers in
  [ADR 0005](0005-target-platforms-and-compilers.md), notably `std::expected` for error
  handling.
- We will **not** use C++20/23 named modules (`import`/`export module`) for now; code uses
  headers with `#pragma once`.

## Consequences

- Minimum compilers: MSVC from Visual Studio 2026, Clang ≥ 19, GCC ≥ 14.
  Clang 18 is not enough: libstdc++ only provides `std::expected` when
  `__cpp_concepts >= 202002L`, which Clang reports from version 19 (found by the first CI run).
- A feature is used only if it works on every supported compiler; CI enforces this.
- Revisit modules in a future ADR when CMake, compilers, clang-tidy and IDEs support them
  consistently.

## Alternatives considered

- **C++20** — lacks `std::expected` and `std::print`, which would need in-house replacements.
- **C++ Modules** — compile-time benefits, but tooling support (analysis, IDEs, mixed
  compilers, package managers) is still inconsistent.
- **Rust / C#** — not chosen; the project and its intended community work in C++.

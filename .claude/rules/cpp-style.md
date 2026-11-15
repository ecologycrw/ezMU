# C++ style

Source: ADR 0003 (C++23), ADR 0018 (conventions). Formatting is automatic (clang-format hook);
naming is checked by clang-tidy.

## Naming

- Types/concepts/enums `PascalCase`; functions/variables/parameters `camelCase`.
- Private/protected members `m_camelCase`; constants and enumerators `kPascalCase`.
- Mutable globals (rare, justified) `g_camelCase`; macros `EZMU_UPPER_SNAKE`.
- Namespace `ezmu::<module>`; files `snake_case.hpp/.cpp`; `#pragma once`.

## Language

- C++23, no extensions, no named modules. Use only features supported by MSVC (VS 2026),
  Clang ≥ 19 and GCC ≥ 14 (Clang 18 lacks `std::expected` with libstdc++).
- Prefer standard library facilities (`std::expected`, `std::span`, `std::format`/`std::print`,
  ranges, concepts) over in-house utilities.
- Mark functions `[[nodiscard]]` when ignoring the result is a bug; `noexcept` when they cannot fail.
- `enum class` only. Size enums deliberately (`std::uint8_t` when enough).
- Every member of a public aggregate (`*Desc`, events, value structs) has a default member
  initializer (`= value` or `{}`), so callers may omit it in designated initializers. GCC and
  Clang warn (`-Wmissing-field-initializers`) otherwise; MSVC does not, so CI catches it late.
- No `using namespace` in headers. No C-style casts.

## Errors

- Fallible operations return `ezmu::foundation::Result<T>`; create errors with `makeError(...)`.
- No exceptions for control flow. Catch at application boundaries only (e.g. `main`).
- `EZMU_ASSERT` for invariants (debug), `EZMU_VERIFY` when the check must always run.

## Ownership and memory

- RAII everywhere; no owning raw pointers; `std::unique_ptr` by default.
- No hidden allocations in per-frame hot paths; document allocation behavior in public APIs.

## Warnings

Warnings are errors on all compilers. Fix the cause; suppress only with a targeted
`NOLINT(<check>)` and a comment explaining why.

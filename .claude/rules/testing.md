---
paths:
  - "**/tests/**"
---

# Testing

- Framework: Catch2 v3 (`#include <catch2/catch_test_macros.hpp>`); tests link
  `Catch2::Catch2WithMain` through `ezmu_add_module_tests`.
- One test file per public header or feature: `<feature>_tests.cpp`.
- Test case names are sentences describing behavior: `"Result holds an error on failure"`.
  Tag with the feature: `[result]`.
- Test through the **public API**, as a game developer would. Do not include `src/` headers.
- Tests are deterministic: no wall-clock sleeps, no network, no dependence on test order.
  Fake time, I/O and network through interfaces.
- Global state touched by a test (e.g. the assertion handler) must be restored with an RAII guard.
- Prefer `STATIC_CHECK` for anything verifiable at compile time.
- Rendering tests (later): offscreen device, compare against reference images with a tolerance;
  Vulkan validation errors fail the test.
- Every bug fix comes with a test that fails without the fix.

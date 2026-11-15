# 0020. GPU tests: headless rendering, reference images and software Vulkan in CI

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

[ADR 0019](0019-ai-assisted-development-workflow.md) makes automated verification the top
priority: the agent must be able to check pixels without a human, and Vulkan validation errors
must fail tests ([ADR 0006](0006-vulkan-1-3-behind-rhi.md)). Hosted CI runners (GitHub Actions)
have no GPU and, on Windows, no Vulkan driver or loader. Real hardware is available only on the
owner's machine. See the [rhi spec](../specs/rhi.md#testing-strategy), decision D2.

## Decision

- Rendering tests use a **headless device** (no window, no surface) that renders into offscreen
  textures and reads them back to a CPU image.
- Results are compared against **reference images** (PNG) with a per-channel tolerance and a
  maximum ratio of differing pixels. On mismatch the test writes `actual.png` and `diff.png` to
  `<build>/test-output/<module>/<test>/`. Setting `EZMU_UPDATE_REFERENCES=1` rewrites the
  references instead of comparing; updated references are reviewed in the commit.
- Every GPU test runs with the Khronos validation layer (synchronization validation included)
  through a shared fixture; any warning or error fails the test. A self-test proves the
  fixture catches a deliberate error.
- GPU tests carry the CTest label `gpu`. When no Vulkan 1.3 adapter or no validation layer is
  available they are **skipped** (reported as skipped, never as passed). Setting
  `EZMU_REQUIRE_GPU=1` turns a skip into a failure.
- **Linux CI** installs Mesa **lavapipe** (software Vulkan 1.3) and the validation layers from
  the distribution and runs GPU tests with `EZMU_REQUIRE_GPU=1`, under ASan/UBSan.
- **Windows CI** builds everything and runs non-GPU tests; GPU tests skip there.
- The owner's machine (real GPU, Vulkan SDK) runs the full suite through `scripts/dev.ps1`.

## Consequences

- Pixel output is verified automatically on every change, on at least one platform.
- Software rasterization can differ slightly from hardware; tolerances must be chosen per test,
  and tests prefer exactly representable values where possible.
- Driver-specific issues on Windows hardware are caught only locally until a Windows software
  driver or a GPU runner is added (possible follow-up; would amend this ADR's CI section).
- Developer setup needs the validation layer (Vulkan SDK on Windows, distribution package on
  Linux); documented in the README.

## Alternatives considered

- **Install a Mesa lavapipe build on Windows CI** — covers MSVC builds too, but depends on a
  third-party binary download and adds maintenance; deferred.
- **No GPU tests in CI** — loses automated pixel verification, contrary to ADR 0019.
- **Self-hosted GPU runner** — real hardware, but cost, security and availability concerns for
  a public repository.

# ezMU

C++23 **client-side + network** engine specialized in classic MMORPGs (MU Online style).
Agnostic of game version, rules, server, protocol and content. No server, no game content, no
readers for third-party proprietary formats. See [ADR 0002](docs/adr/0002-scope-client-and-network-only.md).

## Architecture in one screen

Layers, dependencies **downward only** ([ADR 0011](docs/adr/0011-layered-architecture-engine-and-framework.md)):

| Layer | Where | What |
|---|---|---|
| L0 Foundation | `engine/foundation` | types, `Result`, asserts, memory, math, VFS, jobs, reflection |
| L1 RHI / Platform | `engine/rhi`, `engine/platform` | Vulkan 1.3 (only here), SDL3 (only here) |
| L2 Services | `engine/*` | assets, renderer, animation, audio, input, ui, world (EnTT), net, debug |
| L3 Runtime | `engine/runtime` | app lifecycle, game loop, states, composition root |
| L4 Framework | `framework/*` | genre **mechanisms**; games define **policy**; public Engine API only |
| L5 Tools | `tools/*` | editors/cooker; public API only |
| L6 Games | `samples/*` | sample apps |

Map: [docs/architecture/overview.md](docs/architecture/overview.md) · Decisions: [docs/adr/](docs/adr/README.md) · Roadmap: [docs/roadmap.md](docs/roadmap.md)

## Non-negotiable rules

- Declare modules with `ezmu_add_module(NAME … LAYER …)`; configure fails on layer violations.
- Public headers in `include/ezmu/<module>/`, private code in `src/`.
- Vulkan headers only in `engine/rhi`; SDL headers only in `engine/platform`; module build files
  never mention vcpkg (use `find_package`). Checked by `architecture.rules` test.
- Errors: `ezmu::foundation::Result<T>`; no exceptions for control flow; `EZMU_ASSERT` for invariants.
- No singletons / mutable globals (rare justified exceptions use `g_` and a NOLINT with reason).
- Renderer never reads ECS/game state; it consumes render snapshots ([ADR 0012](docs/adr/0012-dedicated-render-thread.md)).
- New third-party dependency or significant decision ⇒ new ADR. Accepted ADRs are superseded, not rewritten.
- Docs, code and comments in English.

## Build and verify (Windows)

```powershell
./scripts/dev.ps1                         # configure + build + test (windows-msvc-debug)
./scripts/dev.ps1 -Step test -TestFilter foundation
./scripts/check.ps1                       # build + tests + format check
./scripts/check.ps1 -Tidy                 # + clang-tidy
./scripts/check.ps1 -Fix -SkipBuild       # apply clang-format
```

Presets: `windows-msvc-{debug,release,asan}`, `linux-{clang,gcc}-{debug,asan}`.
`dev.ps1` enters the VS 2026 dev shell itself; `VCPKG_ROOT` must be set.

## Hooks (automatic, see `.claude/settings.json`)

- Edits to `build/`, `out/`, `vcpkg_installed/`, `third_party/`, `.git/`, `LICENSE` are blocked.
- Edited C++ files are clang-formatted.
- Before finishing a turn with uncommitted C++/CMake changes, build + tests run; failures come back to you.

## Workflow

Spec ([template](docs/specs/_template.md)) → plan → small steps → verify → small Conventional
Commit with `-s` (DCO). Topic rules live in `.claude/rules/`; each module has its own `CLAUDE.md`.

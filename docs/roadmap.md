# Roadmap

Living document. Milestones are ordered; items inside a milestone are refined by specs.

## M0 — Project skeleton ✅

Repository, license, ADRs, build system (CMake + presets + vcpkg), `foundation` minimum,
sample app, formatting/static analysis/architecture checks, Claude Code rules and hooks, CI on
Windows and Linux.

## M1 — First pixels ✅ (implemented; final owner checks pending)

Specs: [platform](specs/platform.md), [rhi](specs/rhi.md) (see their "Done when" lists).

- ✅ `platform` (SDL3 window, input, text input/IME, clipboard, headless backend)
- ✅ `rhi` (Vulkan 1.3 device, validation layers, resources, command lists, frames in flight,
  swapchain)
- ✅ Clear the screen (`samples/clear`, `ezmu_clear`, dedicated render thread); headless
  rendering tests with a reference image, on Mesa lavapipe in Linux CI
- ✅ Frame capture to PNG (`ezmu_clear --capture`) for automated and AI-assisted visual checks
- ✅ `foundation`: logging, geometry value types, `Image` + PNG I/O + image comparison
- ✅ Path-scoped rules for `rhi` and `platform`; architecture rule against Vulkan/SDL in public
  headers
- Pending owner checks: minimize/restore and 150 % DPI of `ezmu_clear` on a real desktop.

Moved out of M1: the job system (not needed to draw the first pixels; it arrives with
`runtime`/`assets` in M2). Follow-ups: Linux IME via IBus/Fcitx (SDL3 built without D-Bus),
running `samples.clear` in Linux CI under Xvfb.

## M2 — Runtime and render thread

- `runtime` (application lifecycle, game loop, game states)
- Render thread with extraction/snapshot model (replaces the `ezmu_clear` mailbox)
- `foundation`: job system basics
- `debug` overlay (ImGui)

## Later milestones (to be refined)

Assets and native formats, renderer (meshes, terrain, skinning, effects), world/ECS, input
mapping, UI, audio, net + adapters, framework modules, tools, sample game.

## Open topics

Identified gaps that need specs/ADRs before the related modules are built:

| Topic | Needed before |
|---|---|
| Development server: mock server and/or record–replay of sessions | `net`, `replication`, sample game |
| Reference protocol: format, schema/codegen, transport, encryption | `protocols/reference` |
| Native asset formats: models, animation, terrain, effects, data definitions | `assets`, `renderer`, tools |
| UI approach: in-house vs. library base, data-driven layouts, resolution/DPI scaling | `ui`, `gameui` |
| Sample game content shipped in the repository (placeholders, CC0, original) | sample game |
| Scripting: C++ + data only, or an embedded language later (record as ADR either way) | framework extension model |
| Distribution: asset packaging, launcher/auto-updater | first public release |
| Client integrity: package signing, file verification | first public release |
| Engine versioning: SemVer, API stability promise, deprecation, changelog | first public release |
| Performance targets: FPS, minimum spec, memory budgets | renderer |
| Diagnostics: crash dumps, persistent logs, error reports | runtime |
| Localization: UTF-8 everywhere, translation system | ui, gameui |
| Player settings: resolution, graphics options, keybinds, persistence | runtime, input |
| Community: contribution guide extensions, code of conduct, documentation site | public launch |

## Agent infrastructure backlog

Per [ADR 0019](adr/0019-ai-assisted-development-workflow.md), created when needed:

- Skills: create module · write ADR · add domain message · add ECS component/system · add native asset type
- Subagents: architecture reviewer · public API reviewer · test writer
- Path-scoped rules per module (done: rhi, platform; to do: renderer, threading, ecs,
  net/protocols, framework, tools)
- Linux (`sh`) versions of Claude Code hook scripts

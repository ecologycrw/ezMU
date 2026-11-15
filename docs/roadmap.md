# Roadmap

Living document. Milestones are ordered; items inside a milestone are refined by specs.

## M0 — Project skeleton ✅ (in progress)

Repository, license, ADRs, build system (CMake + presets + vcpkg), `foundation` minimum,
sample app, formatting/static analysis/architecture checks, Claude Code rules and hooks, CI on
Windows and Linux.

## M1 — First pixels

- `platform` (SDL3 window, input, text input)
- `rhi` (Vulkan 1.3 device, swapchain, command submission, validation layers)
- Clear the screen; offscreen/headless rendering test with a reference image
- Frame capture to PNG for automated and AI-assisted visual checks
- `foundation`: logging, job system basics

## M2 — Runtime and render thread

- `runtime` (application lifecycle, game loop, game states)
- Render thread with extraction/snapshot model
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
- Path-scoped rules per module (rhi, renderer, threading, ecs, net/protocols, framework, tools)
- Linux (`sh`) versions of Claude Code hook scripts

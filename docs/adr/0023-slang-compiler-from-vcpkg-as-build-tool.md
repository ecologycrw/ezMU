# 0023. Slang compiler from vcpkg as a build tool

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

[ADR 0010](0010-slang-shading-language.md) chose Slang as the shading language, compiled to
SPIR-V at build time, but not where the compiler comes from. M2 adds the first pipelines
([rhi spec, M2 update](../specs/rhi.md#m2-update-pipelines-uploads-and-bindless-textures)), so
every build — the owner's machine and every CI job — now needs `slangc`, at a version that is the
same everywhere, since shader compiler changes can change the generated SPIR-V.

Constraints:

- Windows CI runners have no Vulkan SDK ([ADR 0020](0020-gpu-tests-headless-and-software-vulkan-in-ci.md));
  Linux distribution packages do not ship a current Slang.
- Dependencies come from vcpkg in manifest mode, pinned by the baseline, located with standard
  CMake calls only ([ADR 0009](0009-vcpkg-manifest-and-find-package-only.md)).
- The vcpkg baseline (`8f363bd`, 2026-11-15) has the port `shader-slang` 2026.22. It installs
  the official prebuilt release (downloaded from the Slang GitHub releases, checked by SHA-512):
  the `slangc` tool, the Slang library and headers. It supports x64 Windows and Linux.

## Decision

- We will get `slangc` from the vcpkg port **`shader-slang`**, declared in `vcpkg.json` as a
  **host** dependency (`{ "name": "shader-slang", "host": true }`), so its version follows the
  baseline like every other dependency.
- It is a **build tool only** in M2: `cmake/EzmuShaders.cmake` finds it with
  `find_program(... slangc ...)` (the vcpkg toolchain puts host tools on the program path; no
  build file names vcpkg) and compiles engine shaders to SPIR-V that is embedded in the owning
  module. No ezMU target links the Slang library yet.
- Linking the Slang library (run-time compilation for hot-reload, reflection API at run time,
  per ADR 0010) is a later decision that amends this ADR when a module needs it.
- The ADR 0008 table gets a row "Slang compiler (`shader-slang`) — shader compilation at build
  time — build tool (`cmake/EzmuShaders.cmake`)" in the commit that adds the port.

## Consequences

- One `slangc` version for every machine and CI job; bumping it is a baseline bump
  (`build(deps): …`), with shader output differences visible in reference-image tests.
- Clean builds download the Slang release archive (large: compiler, library and standard
  modules); vcpkg binary caching keeps CI fast after the first run.
- The port is a prebuilt binary, not built from source: we depend on the upstream release for
  supported platforms (x64 Windows and Linux are covered; macOS too, should it ever matter).
- Developers do not need the Vulkan SDK to compile shaders; they still need it for the
  validation layers on Windows.
- License: the `LICENSE` file of the release tag `v2026.22` declares
  `Apache-2.0 WITH LLVM-exception` (checked 2026-11-15; the vcpkg port leaves `license` unset),
  compatible with MPL-2.0. It is recorded in the ADR 0008 row; a baseline bump re-checks it.
  Generated SPIR-V is our output and carries no license obligation from the compiler.

## Alternatives considered

- **`slangc` from the Vulkan SDK** — already on the owner's machine, but absent on CI runners,
  and its version changes with each SDK the developer happens to install.
- **Build Slang from source** (submodule, CPM or a source port) — reproducible, but slow and
  large (LLVM-sized build) for a tool we only run.
- **Commit precompiled SPIR-V** — no compiler in the build, but shaders and binaries drift, and
  contributors could not change shaders without the tool anyway.
- **GLSL + glslang / HLSL + DXC** — would reverse ADR 0010.

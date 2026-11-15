# 0010. Slang as the shading language

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

The engine needs a shader library (terrain, skinned characters, effects, UI, post-processing)
and game developers will write their own materials and effects. Descriptor layouts must stay in
sync between shaders and the RHI.

## Decision

- We will write shaders in **Slang**, compiled to **SPIR-V**.
- Shaders are compiled at build time by the asset cooker/build step; development builds support
  **shader hot-reload**.
- The RHI uses **Slang's reflection API** to derive descriptor and push-constant layouts, so
  layouts are not maintained by hand on both sides.
- Engine shaders are organized as Slang modules; materials and effects build on shared
  interfaces and generics.

## Consequences

- Shader code is modular and reusable across the engine, the framework and games.
- The Slang compiler is a build dependency (available in the Vulkan SDK and as a package).
- Fewer public examples than GLSL; HLSL-like syntax keeps the learning curve low.
- Keeps the door open to other backends (D3D12/Metal) should the RHI ever need them.

## Alternatives considered

- **GLSL** — ubiquitous and mature, but no modules/generics; reflection needs extra tooling
  (SPIRV-Reflect).
- **HLSL via DXC** — mature, but weaker modularity and a Microsoft-centric toolchain.

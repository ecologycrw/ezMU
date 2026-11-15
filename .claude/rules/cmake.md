---
paths:
  - "**/CMakeLists.txt"
  - "cmake/**"
  - "CMakePresets.json"
  - "vcpkg.json"
  - "vcpkg-configuration.json"
---

# CMake and dependencies

Source: ADR 0009, ADR 0011.

- Modules are declared only with `ezmu_add_module` / `ezmu_add_module_tests` /
  `ezmu_add_executable` from `cmake/EzmuModule.cmake`. Never call `add_library` for an engine
  module directly — the layer check would be skipped.
- Dependencies between ezMU modules use the `ezmu::<name>` alias. Same-layer dependencies must be
  listed in `ALLOW_SAME_LAYER`.
- Aggregates (`ezmu_add_aggregate`): `ezmu::engine` bundles every Engine module (defined at the
  end of `engine/CMakeLists.txt`; add each new engine module to it). Executables (samples,
  tools) and external consumers link aggregates; modules never do (configure fails). There is
  no `engine/engine` module. `ezmu::framework` will follow the same pattern.
- Third-party libraries: `find_package(<Pkg> CONFIG REQUIRED)` + imported targets. Never mention
  vcpkg in module build files (the `architecture.rules` test fails).
- Do not add global compile flags in module files; warnings and sanitizers come from the helpers.
- Add a dependency to `vcpkg.json` only together with the module that owns it, and record it in
  ADR 0008. Bumping the baseline in `vcpkg-configuration.json` is its own commit
  (`build(deps): …`).
- New presets must exist for both Windows and Linux when they make sense on both.

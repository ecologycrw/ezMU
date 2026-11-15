# ezMU

**ezMU** is a C++23 client-side engine specialized in classic MMORPGs in the style of MU Online.

It provides everything needed to build and run a game client — rendering, UI, assets, audio,
animation, world/entities, input — plus a networking layer that connects that client to
**any** server. It does not ship a server, game rules or game content: each developer builds
their own server, defines their own rules and supplies their own assets, on top of the same
technological foundation.

> **Status:** early bootstrap. The project skeleton (build, CI, documentation, conventions) is
> in place; engine modules are being designed and implemented incrementally. See
> [docs/roadmap.md](docs/roadmap.md).

## Principles

- **Client-side + network only.** No server implementation. See
  [ADR 0002](docs/adr/0002-scope-client-and-network-only.md).
- **Agnostic.** Not tied to a specific game version, rule set, protocol or content format.
- **Specialized.** A genre framework provides the mechanisms classic MMORPGs share (tile terrain,
  grid inventory, character composition, effects, replication); games define the policy.
- **Modular and layered.** Strict downward-only dependencies, enforced by the build. See
  [docs/architecture/overview.md](docs/architecture/overview.md).

## Repository layout

| Path | Contents |
|---|---|
| `engine/` | ezMU Engine — layers L0–L3 (foundation, platform, RHI, services, runtime) |
| `framework/` | ezMU Framework — layer L4 (genre modules) |
| `protocols/` | Optional protocol adapters |
| `tools/` | Editors and asset tooling (L5), built only on the public API |
| `samples/` | Sample applications (L6) |
| `docs/` | Architecture overview, ADRs, specs, roadmap |
| `cmake/`, `scripts/` | Build helpers, developer scripts and checks |

## Building

Requirements:

- CMake ≥ 3.28 and Ninja
- **Windows:** Visual Studio 2026 (MSVC) · **Linux:** Clang ≥ 19 or GCC ≥ 14
- [vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` set

Windows (PowerShell):

```powershell
./scripts/dev.ps1 -Preset windows-msvc-debug          # configure + build + test
./scripts/check.ps1                                    # full local check (build, tests, format, rules)
```

Linux:

```bash
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug
ctest --preset linux-clang-debug
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Contributions require a DCO sign-off.

## License

ezMU is licensed under the [Mozilla Public License 2.0](LICENSE). Modified engine files must
remain open; games built with ezMU may keep their own code under any license.

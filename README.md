# ezMU

**ezMU** is a C++23 client-side engine specialized in classic MMORPGs in the style of MU Online.

It provides everything needed to build and run a game client — rendering, UI, assets, audio,
animation, world/entities, input — plus a networking layer that connects that client to
**any** server. It does not ship a server, game rules or game content: each developer builds
their own server, defines their own rules and supplies their own assets, on top of the same
technological foundation.

> **Status:** early development. Milestone M1 "First pixels" is implemented: an SDL3 window,
> a Vulkan 1.3 RHI and a sample that clears the screen on a dedicated render thread. Engine
> modules are designed and implemented incrementally. See [docs/roadmap.md](docs/roadmap.md).

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
- [vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` set (it builds SDL3, volk,
  VMA, stb and Catch2)
- To run anything that renders: a GPU driver with **Vulkan 1.3**. Debug builds and tests also
  need the **Khronos validation layer**:
  - **Windows:** install the [Vulkan SDK](https://vulkan.lunarg.com/) (it provides the
    validation layer; the Vulkan loader comes with the GPU driver).
  - **Linux (Debian/Ubuntu):** `libvulkan1` and `vulkan-validationlayers`; Mesa
    `mesa-vulkan-drivers` adds lavapipe, a software Vulkan 1.3 device, if you have no GPU.

Linux also needs the system packages the vcpkg SDL3 port builds against (X11 and Wayland):

```bash
sudo apt-get install -y ninja-build pkg-config libx11-dev libxext-dev libxrandr-dev \
  libxcursor-dev libxi-dev libxss-dev libxtst-dev libxfixes-dev libxinerama-dev libxft-dev \
  libwayland-dev libxkbcommon-dev wayland-protocols libegl1-mesa-dev
```

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

GPU tests (CTest label `gpu`) are **skipped**, not failed, when there is no usable Vulkan 1.3
device or validation layer; set `EZMU_REQUIRE_GPU=1` to turn those skips into failures (Linux
CI does, on lavapipe). `EZMU_GPU=<name substring>` picks an adapter.

### Running the sample

`ezmu_clear` opens a window and clears it every frame from a dedicated render thread. It is
built to `build/<preset>/samples/clear/`, e.g.:

```bash
build/linux-clang-debug/samples/clear/ezmu_clear
```

Escape quits and F12 saves the next frame to `ezmu_clear.png`. For automated checks,
`ezmu_clear --frames 60 --capture out.png` renders 60 frames, writes the last one as a PNG and
exits; `--validation sync` adds synchronization validation and any validation message makes
it exit with an error. It exits with code 77 (skipped) when there is no display or Vulkan
device.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Contributions require a DCO sign-off.

## License

ezMU is licensed under the [Mozilla Public License 2.0](LICENSE). Modified engine files must
remain open; games built with ezMU may keep their own code under any license.

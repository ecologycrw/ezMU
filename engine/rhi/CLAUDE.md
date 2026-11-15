# ezmu::rhi (L1)

The only module that talks to Vulkan (ADR 0006). A thin layer shaped around Vulkan 1.3; volk
and VMA are private. Spec: [docs/specs/rhi.md](../../docs/specs/rhi.md) (M1 plan at the end).

## Responsibility

Device creation (instance, validation, adapter selection, logical device, queue, VMA). Coming in
M1: resources and handles, command lists, readback (step 6); frames in flight and swapchain
(step 7). Later: pipelines and shaders (Slang), bindless descriptors, async queues.

Not here: what to draw (→ `renderer`), windows (→ `platform`), game state (never, ADR 0012).

## Public API

| Header | Contents |
|---|---|
| `device.hpp` | `ValidationMode`, `kDefaultValidationMode`, `AdapterPreference`, `ValidationMessage`, `ValidationCallback`, `DeviceDesc`, `AdapterType`, `AdapterInfo`, `Device`, `toString(AdapterType)` |

## Allowed dependencies

`ezmu::foundation` (public); `volk::volk`, `Vulkan::Headers`, `GPUOpen::VulkanMemoryAllocator`
(private). **Never** `ezmu::platform`: the surface arrives as a raw `uint64_t`.

## Pitfalls

- Include Vulkan only through `src/vulkan.hpp` (volk, then VMA; `VK_NO_PROTOTYPES`). VMA is
  implemented in `src/vk_mem_alloc.cpp` only.
- volk's function pointers are process-global, hence one `Device` at a time (`create` fails
  otherwise) and `volkFinalize()` on destruction.
- `Device::Impl::~Impl` must cope with partially created devices (every handle may be null).
- GPU tests: tag `[gpu]`, create devices with `test::makeTestDevice`, and declare the
  `ValidationGuard` before the device. Keep the validation self-test passing: it proves the
  guard works.
- Validation messages are bugs. Do not filter or ignore one without an explicit, documented
  reason.
- `EZMU_GPU=<substring>` picks an adapter; `EZMU_REQUIRE_GPU=1` turns GPU-test skips into
  failures (set in Linux CI, which runs on Mesa lavapipe).

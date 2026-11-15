# ezmu::rhi (L1)

The only module that talks to Vulkan (ADR 0006). A thin layer shaped around Vulkan 1.3; volk
and VMA are private. Spec: [docs/specs/rhi.md](../../docs/specs/rhi.md) (M1 plan at the end).

## Responsibility

Device creation (instance, validation, adapter selection, logical device, queue, VMA);
generational handles, textures and buffers (VMA); command lists (state transitions on
synchronization2, dynamic-rendering clears, texture→buffer copies, debug labels); immediate
(blocking) submission; texture readback to `foundation::Image`; frames in flight (timeline
semaphore), deferred destruction and swapchains. Later: pipelines and shaders (Slang), bindless
descriptors, async queues.

Not here: what to draw (→ `renderer`), windows (→ `platform`), game state (never, ADR 0012).

## Public API

| Header | Contents |
|---|---|
| `device.hpp` | `ValidationMode`, `kDefaultValidationMode`, `AdapterPreference`, `ValidationMessage`, `ValidationCallback`, `DeviceDesc`, `kMaxFramesInFlight`, `AdapterType`, `AdapterInfo`, `Device` (resources, `executeImmediate`, `readTexture`, `beginFrame`/`endFrame`, `createSwapchain`), `toString(AdapterType)` |
| `handles.hpp` | `Handle<Tag>`, `TextureHandle`, `BufferHandle` |
| `resources.hpp` | `Format`, `bytesPerPixel`, `isDepthFormat`, `toString(Format)`, `TextureUsage` (+ `\|`, `&`, `hasAny`), `TextureDesc`, `MemoryLocation`, `BufferDesc` |
| `command_list.hpp` | `ResourceState`, `LoadOp`, `StoreOp`, `ColorAttachment`, `kMaxColorAttachments`, `RenderingDesc`, `CommandList` |
| `frame.hpp` | `Frame` (`commandList`, `index`, `slot`) |
| `swapchain.hpp` | `PresentMode`, `toString(PresentMode)`, `SwapchainDesc`, `Swapchain` (`resize`, `acquire`, `format`, `extent`, `presentMode`, `imageUsage`) |

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
- Resource state is tracked per texture **at record time** (`TextureRecord::state`). That is
  correct only while command lists are submitted in recording order on the single queue. Hence
  `executeImmediate`/`readTexture` are rejected while a frame is open (they would submit before
  the frame's already-recorded commands). Keep that invariant (or move tracking to submission).
- The `ResourceState` → layout/stage/access table lives in `src/command_list.cpp`. kUndefined
  and kPresent use ALL_COMMANDS as source stage so transitions chain with semaphore waits.
- Public functions handle a failed `EZMU_ASSERT` (stale handle, wrong state) by skipping the
  command, so tests may install a returning assertion handler; keep that pattern.
- Frame `n` signals timeline value `n + 1` (`src/frame.cpp`); `beginFrame` waits for
  `n + 1 - framesInFlight` (never more than `submittedValue`). No fences for pacing.
- `destroy()` removes the handle at once and calls `Impl::retire`: the record is freed now when
  the timeline has reached every submitted frame (and no frame is open), otherwise queued with
  that value and freed in `beginFrame`/`waitIdle`/`~Impl`. Never call it inside an
  `executeImmediate` callback (asserted). The frame churn test fails if deferral breaks.
- Swapchain images are `TextureRecord`s with `swapchainImage = true` (view owned, image not;
  `destroy()` rejects them). Acquire sets their state to kUndefined; the submission waits on the
  per-slot acquire semaphore at ALL_COMMANDS and signals the per-image render-finished semaphore.
  Recreation calls `vkQueueWaitIdle` (rare, not per frame) — the only wait outside shutdown.
- Swapchain presentation is only exercised with a window (`ezmu_clear`, `samples.clear`);
  headless tests cover argument checks only.
- CPU-visible buffers are host-coherent (VMA `requiredFlags`), so `mappedData()` needs no
  flush/invalidate; copies into kReadback buffers end with a TRANSFER→HOST barrier (fences do
  not make device writes visible to the host).
- Reference images: `tests/reference/<name>.png`, compared with `test::checkReferenceImage`;
  mismatches go to `<build>/test-output/rhi/<name>/`; `EZMU_UPDATE_REFERENCES=1` rewrites them
  (look at the PNG before committing it).
- `EZMU_GPU=<substring>` picks an adapter; `EZMU_REQUIRE_GPU=1` turns GPU-test skips into
  failures (set in Linux CI, which runs on Mesa lavapipe).

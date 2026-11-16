# Spec: rhi

- **Status:** Approved (2026-11-15, all proposals accepted)
- **Layer:** L1
- **Owner:** ecologycrw
- **Related ADRs:** [0005](../adr/0005-target-platforms-and-compilers.md),
  [0006](../adr/0006-vulkan-1-3-behind-rhi.md), [0007](../adr/0007-sdl3-platform-layer.md),
  [0008](../adr/0008-third-party-libraries.md), [0010](../adr/0010-slang-shading-language.md),
  [0011](../adr/0011-layered-architecture-engine-and-framework.md),
  [0012](../adr/0012-dedicated-render-thread.md),
  [0016](../adr/0016-tools-in-repository-public-api-only.md),
  [0019](../adr/0019-ai-assisted-development-workflow.md)
- **Milestone:** M1 "First pixels" (together with [platform](platform.md))

## Purpose

`ezmu::rhi` is the only place in ezMU that talks to Vulkan. It is a **thin** layer shaped
around Vulkan 1.3 (dynamic rendering, synchronization2, descriptor indexing, timeline
semaphores) that exposes ezMU handles and value types: device, textures, buffers, command
lists, frames in flight, swapchain, and CPU readback. It supports a **headless** device (no
window, no surface) so rendering can be tested offscreen in CI, and it turns validation-layer
errors into test failures.

It is **not** responsible for: deciding what to draw or in which order (→ `renderer`, L2,
render graph, materials), reading game state (never — ADR 0012), windows and OS events
(→ `platform`), shader compilation (→ build step / cooker, ADR 0010), image file formats
(→ `foundation` image I/O, see *Dependencies*).

## Scope for M1

M1 delivers exactly what "window clears the screen" plus "headless clear with a reference
image" needs, with an API that the later renderer can grow into.

| In M1 | Later (own spec update) |
|---|---|
| Instance, adapter selection, logical device, one graphics+present queue | Async compute / transfer queues |
| Validation layers + debug messenger, error counting, debug names/labels | GPU-assisted validation presets, crash breadcrumbs |
| VMA allocator; textures (2D, render target / transfer / sampled usage) and buffers | Samplers, 3D/array/cube textures, mips upload |
| Generational handles, deferred destruction | Bindless descriptor heap (descriptor indexing) |
| Command lists: state transitions, dynamic rendering with clear, copy texture→buffer | Pipelines, shaders (Slang reflection), draws, push constants |
| Frames in flight (timeline semaphore), swapchain, resize/out-of-date, present modes | Multiple swapchains in tools, HDR |
| Blocking readback of a texture to a CPU `Image` (tests and frame capture) | Async (non-stalling) capture, GPU timestamps |

## Use cases

1. **Sample `ezmu_clear` (M1)** and later **`runtime` (M2)**: create a device with the window's
   instance extensions, create a swapchain from the window's surface, then run a render loop on
   the **render thread** that clears the swapchain image every frame and presents.
2. **Rendering tests**: create a headless device, render into an offscreen texture, read it back
   and compare with a reference PNG; any validation message fails the test.
3. **Frame capture for AI-assisted checks**: `ezmu_clear --frames 60 --capture out.png` renders
   N frames, reads back the last swapchain image and writes a PNG, so an agent or CI can
   inspect what the screen showed.
4. **Renderer (later)**: records command lists from render snapshots, creates resources through
   handles, never touches Vulkan types.
5. **Tools (later)**: model viewer / editors reuse the same device with their own swapchains.

## Public API

Headers under `engine/rhi/include/ezmu/rhi/`. Namespace `ezmu::rhi`. **No Vulkan, volk or VMA
types in public headers**; interop uses opaque `void*` / `std::uint64_t` raw handles.
Sketch only — signatures are refined during implementation and reviewed in the PR.

### `device.hpp`

```cpp
enum class ValidationMode : std::uint8_t {
    kOff,
    kEnabled,          ///< VK_LAYER_KHRONOS_validation. Default in debug builds and tests.
    kEnabledWithSync,  ///< + synchronization validation. Default in rendering tests.
};

enum class AdapterPreference : std::uint8_t { kHighPerformance, kLowPower, kSoftware };

struct ValidationMessage {
    enum class Severity : std::uint8_t { kInfo, kWarning, kError } severity;
    std::string_view messageIdName;
    std::string_view text;            ///< Valid only during the callback.
};
using ValidationCallback = std::function<void(const ValidationMessage&)>;

struct DeviceDesc {
    std::string_view appName = "ezMU";
    std::span<const char* const> instanceExtensions;  ///< From platform::Window (empty = headless).
    bool requirePresent = true;          ///< false → headless device, no surface extensions.
    ValidationMode validation = kDefaultValidationMode;  ///< kEnabled in debug, kOff in release.
    ValidationCallback onValidationMessage;  ///< Called on the thread that made the Vulkan call.
    AdapterPreference adapterPreference = AdapterPreference::kHighPerformance;
    std::string_view adapterNameFilter;  ///< Substring match; env EZMU_GPU overrides.
    void* vkGetInstanceProcAddr = nullptr;  ///< Optional; from platform, to share SDL's loader.
    std::uint32_t framesInFlight = 2;
};

struct AdapterInfo {
    std::string name;
    enum class Type : std::uint8_t { kDiscrete, kIntegrated, kVirtual, kCpu, kOther } type;
    std::uint32_t vendorId, deviceId;
    std::string driverVersion;
    Version apiVersion;
};

/// Owns VkInstance, the debug messenger, VkDevice, the VMA allocator, the queue and all
/// resources created from it. Fails with kUnsupported when no adapter supports Vulkan 1.3 with
/// the required features, or when validation is requested and the layer is missing.
///
/// Thread-safety: externally synchronized. One thread uses a Device at a time; ownership may be
/// handed over (created on the main thread at startup, then moved to the render thread) with a
/// happens-before edge (e.g. std::thread start). In M1 every call after startup comes from the
/// render thread. Thread-safe resource creation from job workers is a later extension.
class Device {
public:
    [[nodiscard]] static foundation::Result<std::unique_ptr<Device>> create(const DeviceDesc&);
    ~Device();  ///< Waits for the GPU to go idle, then destroys everything it owns.

    [[nodiscard]] const AdapterInfo& adapterInfo() const noexcept;
    [[nodiscard]] std::uint32_t validationErrorCount() const noexcept;

    [[nodiscard]] foundation::Result<TextureHandle> createTexture(const TextureDesc&);
    [[nodiscard]] foundation::Result<BufferHandle> createBuffer(const BufferDesc&);
    void destroy(TextureHandle);  ///< Deferred until the GPU no longer uses it.
    void destroy(BufferHandle);

    /// Creates a swapchain on a surface created by platform::Window::createVulkanSurface.
    /// Takes ownership of `rawSurface` (destroys it). The window must outlive the swapchain.
    [[nodiscard]] foundation::Result<std::unique_ptr<Swapchain>>
    createSwapchain(std::uint64_t rawSurface, const SwapchainDesc&);

    /// Waits for the frame slot to be free, recycles its command pool and deferred deletions.
    [[nodiscard]] foundation::Result<Frame*> beginFrame();
    /// Submits the frame's command list (waiting on acquired swapchain images) and presents them.
    [[nodiscard]] foundation::Result<> endFrame(Frame& frame);

    /// Blocking convenience for tests and on-demand capture: copies `texture` to a readback
    /// buffer, waits, and returns tightly packed RGBA8 pixels (BGRA and sRGB formats are
    /// returned as stored, channel-swizzled to RGBA). Stalls the GPU; never call per frame.
    [[nodiscard]] foundation::Result<foundation::Image> readTexture(TextureHandle texture);

    void waitIdle();

    /// Opaque raw handles for interop (platform surface creation, debugging, tests).
    [[nodiscard]] void* nativeInstance() const noexcept;
};
```

### `handles.hpp`, `resources.hpp`

```cpp
/// 32-bit index + 32-bit generation. Default-constructed = invalid. Using a destroyed handle
/// is caught by EZMU_ASSERT (generation mismatch).
template <typename Tag> class Handle { /* index, generation, isValid(), == */ };
using TextureHandle = Handle<struct TextureTag>;
using BufferHandle = Handle<struct BufferTag>;

enum class Format : std::uint8_t {
    kUndefined, kRgba8Unorm, kRgba8Srgb, kBgra8Unorm, kBgra8Srgb, kRgba16Float, kDepth32Float,
};

enum class TextureUsage : std::uint8_t {  // flags
    kRenderTarget = 1 << 0, kDepthStencil = 1 << 1, kSampled = 1 << 2,
    kTransferSrc = 1 << 3, kTransferDst = 1 << 4,
};

struct TextureDesc { Extent2D size; Format format; TextureUsage usage; std::string_view debugName; };

enum class MemoryLocation : std::uint8_t { kGpu, kUpload, kReadback };
struct BufferDesc { std::uint64_t size; MemoryLocation location; std::string_view debugName; };
```

### `command_list.hpp`

Barriers use a small set of **resource states** mapped internally to synchronization2 stage/
access masks and image layouts. The RHI tracks the current state of each texture (whole-image,
M1), so a transition only names the target state. The future render graph (renderer, L2)
will issue these transitions automatically.

```cpp
enum class ResourceState : std::uint8_t {
    kUndefined, kRenderTarget, kDepthWrite, kShaderRead, kTransferSrc, kTransferDst, kPresent,
};

enum class LoadOp : std::uint8_t { kLoad, kClear, kDontCare };
enum class StoreOp : std::uint8_t { kStore, kDontCare };

struct ColorAttachment {
    TextureHandle texture;
    LoadOp load = LoadOp::kClear;
    StoreOp store = StoreOp::kStore;
    std::array<float, 4> clearColor{0, 0, 0, 1};
};
struct RenderingDesc { std::span<const ColorAttachment> colorAttachments; Rect2D area; };

/// Records GPU commands for one frame. Obtained from Frame::commandList(); valid until
/// Device::endFrame(). Same thread as the owning Device.
class CommandList {
public:
    void transition(TextureHandle texture, ResourceState newState);
    void beginRendering(const RenderingDesc& desc);   ///< vkCmdBeginRendering (dynamic rendering).
    void endRendering();
    void copyTextureToBuffer(TextureHandle src, BufferHandle dst);
    void beginDebugLabel(std::string_view name);      ///< Visible in RenderDoc / Nsight.
    void endDebugLabel();
};
```

### `swapchain.hpp`, `frame.hpp`

```cpp
enum class PresentMode : std::uint8_t { kFifo, kMailbox, kImmediate };  // kFifo = vsync, default

struct SwapchainDesc {
    Extent2D pixelSize;               ///< From platform::Window::pixelSize().
    PresentMode presentMode = PresentMode::kFifo;  ///< Falls back to kFifo if unsupported.
    bool srgb = true;                 ///< Prefer *_SRGB surface formats.
    bool allowCapture = true;         ///< Adds transfer-src usage for frame capture.
};

/// Render-thread object. Recreated internally on VK_ERROR_OUT_OF_DATE_KHR / suboptimal.
class Swapchain {
public:
    /// Records the new size (e.g. after platform WindowResized); applied on the next acquire.
    void resize(Extent2D pixelSize);
    /// Acquires the next image for `frame`. Returns an invalid handle when presentation must be
    /// skipped this frame (minimized window, zero extent, swapchain being recreated).
    [[nodiscard]] foundation::Result<TextureHandle> acquire(Frame& frame);
    [[nodiscard]] Format format() const noexcept;
    [[nodiscard]] Extent2D extent() const noexcept;
};

class Frame {
public:
    [[nodiscard]] CommandList& commandList() noexcept;
    [[nodiscard]] std::uint64_t index() const noexcept;  ///< Monotonic frame counter.
};
```

### Example: clearing the window (render thread)

```cpp
Frame* frame = EZMU_TRY(device->beginFrame());
TextureHandle image = EZMU_TRY(swapchain->acquire(*frame));
if (image.isValid()) {
    CommandList& cmd = frame->commandList();
    cmd.transition(image, ResourceState::kRenderTarget);
    const ColorAttachment color{.texture = image, .clearColor = {0.1f, 0.2f, 0.4f, 1.0f}};
    cmd.beginRendering({.colorAttachments = {&color, 1}, .area = {{0, 0}, swapchain->extent()}});
    cmd.endRendering();
    cmd.transition(image, ResourceState::kPresent);
}
EZMU_TRY(device->endFrame(*frame));   // submits; presents acquired images
```

(`EZMU_TRY` is illustrative; whether foundation gets a propagation macro is out of scope.)

### Errors

Every `VkResult` is checked internally and converted to `foundation::Result` with the result
name and the failing call in the message. `VK_ERROR_DEVICE_LOST` is reported as `kInternal`
and is fatal for the device in M1 (no recovery). Programming errors (wrong thread, stale
handle, transition inside rendering) are `EZMU_ASSERT`s.

## Implementation notes

- **Loader (volk):** `volkInitialize()` by default; when `DeviceDesc::vkGetInstanceProcAddr` is
  set (from `platform::Window::vulkanGetInstanceProcAddr()`), `volkInitializeCustom()` so SDL
  and the RHI are guaranteed to use the same Vulkan library. `VK_NO_PROTOTYPES` everywhere.
- **VMA** gets its function pointers from volk (`VMA_STATIC_VULKAN_FUNCTIONS=0`,
  `VMA_DYNAMIC_VULKAN_FUNCTIONS=1`).
- **Required features:** Vulkan 1.3 `dynamicRendering`, `synchronization2`; 1.2
  `timelineSemaphore`, `descriptorIndexing` (+ runtime array / partially bound / update after
  bind bits). Adapters without them are rejected with a message listing what is missing.
- **Adapter selection:** filter by required features and (if `requirePresent`) present support;
  then `EZMU_GPU` env / `adapterNameFilter`; then preference (discrete > integrated > cpu for
  kHighPerformance; cpu first for kSoftware). The chosen adapter is logged.
- **Frame pacing:** one timeline semaphore; frame slot `i` waits for value
  `frameIndex - framesInFlight`. Acquire uses one binary semaphore per frame slot; the
  "render finished" semaphore used by present is **per swapchain image** (avoids the known
  semaphore-reuse hazard). Fences are not used for frame pacing.
- **Deferred destruction:** destroyed resources go into the current frame slot's queue and are
  freed when that slot's timeline value is reached.
- **Debug names:** every Vulkan object gets `vkSetDebugUtilsObjectNameEXT` when validation or
  debug utils are available.

### Differences from the sketch (step 5, device)

- **One `Device` at a time:** volk's function pointers are process-global, so a second
  concurrent `Device::create` fails with `kInvalidArgument` (same rule as `platform::Platform`).
- Added `validationMode()`, `validationWarningCount()`, `nativePhysicalDevice()` and
  `nativeDevice()` (the last two for interop and the validation self-test).
- `DeviceDesc::vkGetInstanceProcAddr` is a `VulkanVoidFunction` (function pointer), matching
  `platform::Window::vulkanGetInstanceProcAddr()`; `framesInFlight` moves to the frame/swapchain
  step.
- Without a callback, validation messages are printed to stderr.
- Presentation support is checked per surface when the swapchain is created (step 7);
  `requirePresent` only requires `VK_KHR_swapchain` at device creation.

### Differences from the sketch (step 6, resources, command lists and readback)

- **Immediate submission until frames exist:** `Device::executeImmediate(record)` records a
  one-shot command list (one transient pool, one command buffer, one fence, created on first
  use), submits it and waits. Tests, `readTexture` and setup work use it; step 7 adds per-frame
  command lists. Because every submission is waited for, `destroy()` frees resources
  immediately in step 6; step 7 moves destruction to the frame slot's deferred queue.
- `TextureUsage` gained `kNone` (the `TextureDesc` default; creation fails without a usage) and
  `hasAny`, `|`, `&` helpers, mirroring `platform::KeyMod`. `bytesPerPixel`, `isDepthFormat` and
  `toString(Format)` are public helpers.
- `BufferDesc` has no usage flags yet: M1 buffers are transfer source/destination only.
  CPU-visible buffers (kUpload, kReadback) are persistently mapped and **host-coherent**;
  `Device::mappedData(BufferHandle)` exposes them (needed to use `copyTextureToBuffer`).
- Texture creation also checks `vkGetPhysicalDeviceImageFormatProperties`, returning
  kUnsupported for format/usage/size combinations the adapter lacks.
- `RenderingDesc::area` with an empty extent means the whole first attachment.
  `kMaxColorAttachments = 8`; recording never allocates.
- Transitions: read-only states (kShaderRead, kTransferSrc, kPresent) to themselves are no-ops;
  writable states to themselves emit a barrier (orders e.g. two clears of the same image).
  kUndefined and kPresent use ALL_COMMANDS as source stage so a transition after a semaphore
  wait (swapchain acquire, step 7) is ordered after it. State is tracked at record time, valid
  because command lists execute in recording order on the single queue.
- `copyTextureToBuffer` into a kReadback buffer appends a TRANSFER→HOST barrier, since fence
  waits do not make device writes visible to the host.
- `readTexture` requires kTransferSrc usage and a written texture (state ≠ kUndefined), restores
  the texture's tracked state, and supports the four 8-bit RGBA/BGRA formats (kUnsupported
  otherwise).
- Programming errors (stale handles, wrong states, unbalanced begin/end) are `EZMU_ASSERT`s
  after which the call does nothing, so a returning assertion handler (tests) stays safe.
  Destroying an invalid (default) handle is a no-op, like `delete nullptr`.
- Reference-image mismatches also write `expected.png` next to `actual.png`/`diff.png`.

### Differences from the sketch (step 7, frames in flight and swapchain)

- `DeviceDesc::framesInFlight` (default 2) is validated against `kMaxFramesInFlight = 4`
  (kInvalidArgument otherwise). Frame `n` signals timeline value `n + 1`; `beginFrame` waits for
  `n + 1 - framesInFlight`, capped at the last submitted value so a failed submission cannot
  hang the next frame. Each slot has one command pool + primary command buffer, reset at
  `beginFrame`. `Frame` also exposes `slot()` for per-slot game resources;
  `Device::framesInFlight()` returns the setting.
- **Deferred destruction** uses one queue ordered by timeline value instead of per-slot
  queues: `destroy()` tags the object with the open frame's value (or, outside frames, the last
  submitted one). If the GPU has already reached it — e.g. a device that never ran frames, as in
  step 6 — the object is freed at once. Pending objects are freed in `beginFrame`, in
  `waitIdle` and at shutdown. `Device::pendingDestructionCount()` is public for tests and
  diagnostics.
- `executeImmediate` and `readTexture` are rejected (assert + kInvalidArgument) while a frame
  is open: they submit immediately, before the frame's already-recorded commands, which would
  break record-time state tracking. Outside frames they run after the submitted frames
  (barriers order them on the single queue). Capturing a presented swapchain image therefore
  means copying it to a readback buffer inside the frame (step 8).
- `endFrame` closes open passes/labels and transitions acquired images not in kPresent (each
  after an assertion) so the submission stays valid; all acquired images of a frame are
  presented with one `vkQueuePresentKHR`. Present results OUT_OF_DATE/SUBOPTIMAL mark the
  swapchain for recreation on its next `acquire`; other errors fail `endFrame` after the frame
  is over.
- **Swapchain:** created eagerly when `pixelSize` is not empty (so `format()`/`extent()` are
  known), otherwise on the first acquire. Formats: B8G8R8A8/R8G8B8A8, sRGB first when
  `srgb` (UNORM first otherwise), SRGB_NONLINEAR color space; kUnsupported if none. Image count
  `minImageCount + 1`; opaque composite alpha when available; `kTransferSrc` only when
  `allowCapture` and the surface supports it (`Swapchain::imageUsage()` reports it, as does
  `presentMode()` for the kFifo fallback). `SwapchainDesc` gained `debugName`.
- `acquire` at most once per swapchain per frame; on OUT_OF_DATE it recreates and retries once,
  then returns an invalid handle. **Recreation waits for the queue** (`vkQueueWaitIdle`) before
  replacing images: without `VK_EXT_swapchain_maintenance1` there is no signal for when
  presentation of old images ends; it happens only on resize/out-of-date, never per frame.
- Presentation support: `surfaceSupport` requires `requirePresent` **and** `VK_KHR_surface`
  among `instanceExtensions`; otherwise `createSwapchain` fails with kUnsupported without
  touching the surface (it cannot belong to this instance). The surface is owned from the
  moment it passes that check and is destroyed on any later failure.
- Destroying the Device before its swapchains is an assertion.

### Differences from the sketch (step 8, `ezmu_clear`)

- **Shutdown order is swapchain → Device → window → platform**, not "window, then device"
  (see *Threading*). A Device loaded through `Window::vulkanGetInstanceProcAddr()` uses the
  Vulkan library SDL loaded for the window; destroying the last Vulkan window makes SDL unload
  it, and the Device's teardown then calls into unloaded code (access violation on exit, found
  by `samples.clear`).
- The mailbox carries pixel size, minimized and capture; **quit is the render thread's
  `std::stop_token`** (`std::jthread::request_stop`). While minimized the render thread blocks
  on a condition variable instead of producing empty frames.
- Capture copies the acquired image into a kReadback buffer inside the frame (before
  kPresent), then `waitIdle` and swizzles BGRA → RGBA. `--capture` with `--frames N` captures
  the last presented frame; F12 captures the next one at runtime (default `ezmu_clear.png`).
  Escape quits.
- `--color r,g,b[,a]` and `--validation off|on|sync` were added for the test.
- Exit code **77** means "skipped": no display (Platform or window creation fails) or no usable
  Vulkan device / surface support (kUnsupported). With `EZMU_REQUIRE_GPU=1` only the missing
  display still skips. Any validation message makes the sample exit with 1.
- `samples.clear` runs through `samples/clear/tests/run_clear_test.cmake` (skip via
  `SKIP_REGULAR_EXPRESSION`, since `cmake -P` cannot set an exit code before CMake 3.29) and
  checks the PNG with the test-only helper `ezmu_clear_check`, which encodes the linear clear
  color for the reported swapchain format (sRGB or UNORM) and allows one step of rounding.
  Linux CI has no display, so the test skips there; running it under Xvfb on lavapipe is a
  possible follow-up.

## Threading

Per [ADR 0012](../adr/0012-dedicated-render-thread.md):

- **Main thread** (startup only): `Device::create`, `Window::createVulkanSurface`,
  `Device::createSwapchain`. Then the device and swapchain are handed to the render thread.
- **Render thread** (everything else): `beginFrame`, `acquire`, recording, `endFrame`
  (submit + present), resource creation/destruction, `readTexture`, swapchain `resize`.
- The main thread communicates with the render thread only through data it publishes
  (in M1: a small mailbox with "new pixel size", "minimized", "capture requested", "quit";
  from M2: the render snapshot). The RHI has no knowledge of either.
- **Shutdown:** the render thread calls `waitIdle()` and destroys the swapchain (which destroys
  the surface) **before** the main thread destroys the window, then the device. *(Corrected in
  step 8: the device goes before the window — see "Differences from the sketch (step 8)".)*
- `ValidationCallback` runs on whichever thread made the offending Vulkan call; it must be
  thread-safe (the test fixture's recorder uses a mutex).

## Dependencies

- ezMU modules: `ezmu::foundation` (public: `Result`, `Image`, math value types, logging).
  **No dependency on `platform`** — see [platform spec](platform.md#dependencies).
- Third-party (private to the module, already listed in ADR 0008): **volk**
  (`volk::volk`), **Vulkan Memory Allocator** (`GPUOpen::VulkanMemoryAllocator`); Vulkan
  headers come with volk. Added to `vcpkg.json` in the commit that creates the module.
- **Runtime requirements (not linked):** a Vulkan 1.3 driver; for debug/tests, the Khronos
  validation layer (Vulkan SDK on Windows; `vulkan-validationlayers` package on Linux).
  Documented in the README "Building" section.
- **PNG encode/decode** for reference images and frame capture is needed by `foundation`'s
  image I/O (decision D3, [ADR 0021](../adr/0021-stb-for-png-image-io.md)). The RHI itself only produces `foundation::Image`.

## Extension points

- **Configure:** `DeviceDesc` (validation, adapter choice, frames in flight), `SwapchainDesc`
  (present mode, sRGB, capture).
- **Extend:** raw handle accessors for interop and debugging; validation callback for custom
  reporting.
- **Replace:** the RHI is consumed by the renderer through handles only, so a different backend
  could implement the same headers later (not a goal, ADR 0006). Games that need custom Vulkan
  work will get an explicit "native access" extension in a later spec, not ad-hoc includes.

## Data formats / messages

- `foundation::Image` (CPU, RGBA8, tightly packed, top-left origin) is the readback format.
- Reference images: PNG, RGBA8, sRGB-encoded values as stored, under
  `engine/rhi/tests/reference/` (and later `engine/renderer/tests/reference/`).

## Testing strategy

Rendering tests follow [ADR 0019](../adr/0019-ai-assisted-development-workflow.md): the agent
must be able to verify pixels without a human.

1. **No-GPU unit tests** (always run): handle generation/staleness, `Format`/usage flag helpers,
   descriptor default values. Run on every CI preset.
2. **GPU tests**, CTest label `gpu`, headless device (`requirePresent = false`):
   - Device creation reports a Vulkan ≥ 1.3 adapter; required features present.
   - **Validation guard:** a shared test fixture creates the device with a recording
     `ValidationCallback` and `kEnabledWithSync`; at the end of each test case it `CHECK`s that
     no warning/error was recorded and prints them otherwise. A **self-test** issues a
     deliberately invalid raw Vulkan call (allowed: tests live in `engine/rhi`) and asserts the
     fixture caught it — proving that validation is really on.
   - **Clear + readback:** clear an `kRgba8Unorm` texture to exactly representable values
     (e.g. 0, 51, 102, 255 → 0.0, 0.2, 0.4, 1.0) and compare pixels exactly.
   - **Reference image:** clear two textures (or regions via two rendering passes) and compare
     with `reference/clear_*.png` using per-channel tolerance and a max-differing-pixel ratio.
     On mismatch the test writes `actual.png` and `diff.png` to
     `${CMAKE_BINARY_DIR}/test-output/rhi/<test>/` for inspection (by the owner or the agent).
     `EZMU_UPDATE_REFERENCES=1` rewrites references instead of comparing (commit reviewed).
   - **Frames in flight:** 200 headless frames with resource create/destroy churn; no
     validation messages; deferred deletions drained at shutdown.
   - When no Vulkan 1.3 adapter or validation layer is available the GPU tests are **skipped**
     (Catch2 `SKIP`, reported as skipped by CTest), never silently passed. A CI job that is
     expected to have a GPU sets `EZMU_REQUIRE_GPU=1`, which turns "skipped" into "failed".
3. **Sample test** `samples.clear` (label `gpu`, needs a display): runs `ezmu_clear --frames 10
   --capture <out>.png` and compares the capture with the expected clear color.
4. **CI** (decision D2, [ADR 0020](../adr/0020-gpu-tests-headless-and-software-vulkan-in-ci.md)): Linux jobs install Mesa **lavapipe** (software Vulkan 1.3) and the
   validation layers and run GPU tests with `EZMU_REQUIRE_GPU=1` under ASan/UBSan; Windows
   jobs build everything and run non-GPU tests (GPU tests skip: no driver on hosted runners).
   The owner's machine (real GPU, Vulkan SDK) runs the full suite through `scripts/dev.ps1`.

## Done when

- [ ] `engine/rhi` module exists (`ezmu_add_module(NAME rhi LAYER 1 …)`), volk + VMA in
      `vcpkg.json`.
- [ ] Public API above implemented, documented with thread rules, no Vulkan types in public
      headers (checked by a new `architecture.rules` rule).
- [ ] Headless GPU tests (validation guard + self-test, clear + readback, reference image,
      frames in flight) pass on lavapipe in Linux CI with ASan/UBSan and on the owner's GPU.
- [ ] No validation errors or warnings in any test or in the sample.
- [ ] **"Window clears the screen":** `ezmu_clear` opens a window via `platform`, renders on a
      dedicated render thread, clears to a solid color every frame with vsync, survives
      resize, minimize/restore and close without validation messages or leaks, and
      `--capture` writes a PNG of the presented image.
- [ ] `engine/rhi/CLAUDE.md` and path-scoped rule `.claude/rules/rhi-vulkan.md` written.
- [ ] Owner ran `ezmu_clear` on their machine and checked the window visually.

## M1 implementation plan

Each step is a small, verified Conventional Commit (`git commit -s`). Order matters.

1. **`build: add ezmu::engine aggregator`** — new helper `ezmu_add_aggregate(NAME engine
   LAYER 3 MODULES …)` in `cmake/EzmuModule.cmake`: an INTERFACE target `ezmu_engine` /
   alias `ezmu::engine`, with `EZMU_LAYER 3`, **not** an `engine/engine` module (no headers,
   not in `EZMU_MODULES`). Defined at the end of `engine/CMakeLists.txt`, aggregating every
   engine module (today `foundation`; `platform` and `rhi` are added as they land). Rule:
   modules (L0–L4) must depend on specific modules, never on an aggregate — enforced in
   `_ezmu_check_deps`; aggregates are for executables (samples, tools) and external users.
   `ezmu_hello` links `ezmu::engine`. `ezmu::framework` follows the same pattern later.
   Document in `.claude/rules/cmake.md` and `docs/architecture/overview.md`.
2. **`feat(foundation): …` prerequisites** — minimal math value types (`Extent2D`, `Rect2D`,
   `Version` reuse), minimal logging (levels + pluggable sink, default stderr), `Image` (RGBA8)
   with PNG read/write and image comparison (per D3). Small update to
   `engine/foundation/CLAUDE.md`; logging and the job system get their own short spec if they
   grow beyond this.
3. **`docs(adr): …`** — ADRs for decisions taken in this review (D2 GPU testing in CI,
   D3 PNG library), and ADR 0008 table/amendment for the PNG library.
4. **`feat(platform): …`** — module, SDL3 in `vcpkg.json`, headless tests, Linux CI packages
   for SDL3, `engine/platform/CLAUDE.md`, `.claude/rules/platform.md`; add to `ezmu::engine`.
5. **`feat(rhi): device and validation`** — module skeleton, volk + VMA, headless device,
   adapter selection, validation guard + self-test, CI lavapipe + validation layers,
   `engine/rhi/CLAUDE.md`, `.claude/rules/rhi-vulkan.md`; add to `ezmu::engine`.
6. **`feat(rhi): resources, command lists and readback`** — handles, textures, buffers,
   transitions, dynamic-rendering clear, readback, reference image test.
7. **`feat(rhi): frames in flight and swapchain`** — timeline pacing, deferred destruction,
   swapchain with resize/out-of-date handling.
8. **`feat(samples): ezmu_clear`** — window + render thread + mailbox, resize/minimize,
   `--frames/--capture`, `samples.clear` test.
9. **`test(architecture): …`** — new rule: public headers of `rhi`/`platform` must not include
   Vulkan/SDL headers (today rule 2/3 allow them anywhere inside the module). *(Done as rule 5
   of `scripts/checks/check_rules.cmake`, which covers every public header.)*
10. **`docs: …`** — roadmap (M1 status, path-scoped rules done), overview, README build
    requirements (Vulkan SDK / validation layers, Linux packages).

### Path-scoped rules to add

- **`.claude/rules/rhi-vulkan.md`** (`paths: engine/rhi/**`): Vulkan/volk/VMA only here and only
  in `src/`; public headers expose handles and opaque raw handles; `VK_NO_PROTOTYPES`; Vulkan 1.3
  idioms only (dynamic rendering, no `VkRenderPass`/`VkFramebuffer`; synchronization2 only);
  every `VkResult` checked and converted to `Result`; all memory through VMA; every object
  debug-named; deferred destruction, never `vkDeviceWaitIdle` in per-frame paths; thread
  contract (render thread); a validation message is a bug, never suppressed without an ADR-level
  reason; GPU tests use the validation fixture and the `gpu` label; reference image workflow.
- **`.claude/rules/platform.md`** (`paths: engine/platform/**`): SDL only here; no SDL types or
  values (scancodes, keycodes, window ids) escape public headers; main-thread-only contract and
  its assert; `pollEvents()` must not allocate in steady state; UTF-8 everywhere; scancodes
  stay USB HID; headless backend in tests; no dependency on `rhi` (Vulkan interop is
  type-erased).

## Decisions (approved 2026-11-15: every proposal below was accepted)

- **D1 — Platform ↔ RHI surface integration.** *Proposal:* type-erased — `Window` exposes the
  instance extensions and `createVulkanSurface(void* vkInstance) → uint64_t`; the RHI takes the
  raw surface; the composition root (sample, later `runtime`) wires them; no same-layer
  dependency. *Alternatives:* `rhi` depends on `platform` via `ALLOW_SAME_LAYER` (simpler call
  site, couples the RHI to SDL windows and blocks headless/other platforms); `platform` exposes
  native handles (HWND, X11, Wayland) and the RHI creates surfaces per WSI (more code in the
  RHI, duplicates SDL's work).
- **D2 — GPU tests in CI.** *Proposal:* Linux runs GPU tests on Mesa lavapipe with validation
  layers (`EZMU_REQUIRE_GPU=1`); Windows CI builds and skips GPU tests; the owner's GPU covers
  real hardware locally. *Alternatives:* also install a Mesa lavapipe build on Windows CI
  (third-party binary download, more maintenance); no GPU tests in CI at all (loses automated
  pixel verification, contradicts ADR 0019).
- **D3 — PNG library and location.** *Proposal:* `stb_image` + `stb_image_write`
  (public domain / MIT, header-only, vcpkg `stb`) wrapped privately by `foundation`
  (`Image`, `readPng`, `writePng`, `compareImages`). New dependency → ADR. *Alternatives:*
  `libspng` + zlib (faster, more deps); a separate L0 module `image`; keep PNG test-only
  (but frame capture in the sample needs it at runtime).
- **D4 — Barrier model.** *Proposal:* explicit `transition(texture, ResourceState)` with
  per-texture state tracking (whole image) — simple, safe, render graph automates it later.
  *Alternatives:* mirror synchronization2 barriers 1:1 (maximum control, verbose and
  error-prone for callers); fully automatic tracking inside the RHI (hidden cost, belongs in
  the render graph).
- **D5 — Defaults.** *Proposal:* 2 frames in flight, `kFifo` (vsync) present mode, sRGB
  swapchain format, validation on in debug builds and tests and off in release.
- **D6 — Render thread already in M1.** *Proposal:* yes — `ezmu_clear` uses a `std::jthread`
  render thread and a tiny mailbox, so the RHI's threading contract is exercised from day one;
  M2 replaces the mailbox with `runtime` and render snapshots. *Alternative:* single-threaded
  sample until M2.
- **D7 — Scope.** *Proposal:* no pipelines/shaders/triangle in M1 (the Slang toolchain is its
  own step at the start of the renderer work). *Alternative:* add a triangle to M1 to validate
  the Slang build step early.

# Spec: renderer (bootstrap: render snapshots)

- **Status:** Approved (M2 batch A, 2026-11-15; every proposal accepted)
- **Layer:** L2
- **Owner:** ecologycrw
- **Related ADRs:** [0006](../adr/0006-vulkan-1-3-behind-rhi.md),
  [0010](../adr/0010-slang-shading-language.md),
  [0011](../adr/0011-layered-architecture-engine-and-framework.md),
  [0012](../adr/0012-dedicated-render-thread.md),
  [0020](../adr/0020-gpu-tests-headless-and-software-vulkan-in-ci.md)
- **Milestone:** M2 "Runtime and render thread" (batch A, with [jobs](jobs.md) and
  [runtime](runtime.md)); part 2 depends on the M2 update of the [rhi spec](rhi.md) (batch B)

## Purpose

`ezmu::renderer` turns **render snapshots** into GPU work. A snapshot is an immutable,
self-contained description of one frame produced by the game thread during extraction
([ADR 0012](../adr/0012-dedicated-render-thread.md)); the renderer consumes it on the render
thread and never reads game state. This spec creates the module with the smallest renderer that
proves the model: the snapshot types, the exchange between the two threads, a feedback channel
back to the game thread, and a `Renderer` that clears and presents (or renders offscreen) and
captures frames. Part 2 adds generic 2D draw lists and texture resources, needed by the debug
overlay.

It is **not** responsible for: owning threads or deciding when frames happen (→ `runtime`,
L3); windows and events (→ `platform`); Vulkan (→ `rhi`); meshes, materials, terrain, skinning,
effects, text shaping and the render graph (later renderer milestones, own spec updates);
ImGui (→ `debug`, which produces 2D draw lists in generic ezMU types).

## Scope for M2

| Part 1 (batch A) | Part 2 (batch B, after rhi M2) | Later |
|---|---|---|
| `RenderSnapshot` (output state, clear color, capture request) | `DrawList2D` (vertices, indices, commands, clip rects) | Scene data: camera, meshes, materials, lights |
| `SnapshotRing` (game → render, bounded, back-pressure) | `RenderResources`: textures created from the game thread | Buffers/meshes from the game thread, streaming |
| `RenderFeedbackQueue` (render → game: frame reports, captures, fatal errors) | 2D pass: alpha-blended, scissored, textured triangles | Render graph, multiple views, post-processing |
| `Renderer`: swapchain or offscreen output, clear, present, capture to `Image` | Fallback textures (white, "missing") | GPU timestamps, async capture |

## Use cases

1. **Runtime** (main thread) writes one snapshot per game frame into the `SnapshotRing`; its
   render thread takes the oldest published snapshot, calls `Renderer::render`, releases it and
   pushes the result into the `RenderFeedbackQueue`; the main thread drains feedback at the start
   of the next frame.
2. **Window changes** (resize, minimize, display scale) reach the render thread only as
   `OutputState` in the snapshot — this replaces the `ezmu_clear` mailbox.
3. **Frame capture** (F12, `--capture`, AI-assisted visual checks): a game state sets a capture
   id in the snapshot; the renderer copies that frame's output and returns a
   `foundation::Image` through feedback; the main thread (or a job) writes the PNG.
4. **Rendering tests** feed synthetic snapshots straight to a `Renderer` with an offscreen
   target and compare the readback with reference images — no runtime, no window
   ([ADR 0012](../adr/0012-dedicated-render-thread.md) consequences,
   [ADR 0020](../adr/0020-gpu-tests-headless-and-software-vulkan-in-ci.md)).
5. **Debug overlay** (part 2, batch B): `debug` converts ImGui output into `DrawList2D`s in the
   snapshot and creates its font atlas through `RenderResources`.
6. **Game UI** (later, `ui`/`gameui`): the same 2D draw lists carry retained-mode UI.

## Public API

Headers under `engine/renderer/include/ezmu/renderer/`. Namespace `ezmu::renderer`. Sketch
only — names and exact signatures are refined during implementation and reviewed in the PR.

`snapshot.hpp`, `draw_2d.hpp` and `resources.hpp` depend only on `foundation`, so producers
(`debug`, game code) do not see `rhi` types. Only `renderer.hpp` includes `rhi` headers.

### `snapshot.hpp` (part 1)

```cpp
/// What the renderer must draw into. Written by the game thread from window events.
struct OutputState {
    foundation::Extent2D pixelSize{};  ///< Swapchain / offscreen size. Empty = nothing to draw.
    float displayScale = 1.0F;         ///< Pixels per logical unit (e.g. 1.5 at 150 %).
    bool presentable = true;           ///< false while minimized: GPU work is skipped.
};

/// One frame, as seen by the render thread. Plain data, owned by a SnapshotRing slot.
/// Allocation: contents are cleared with reset(), keeping capacity, so a warmed-up ring does
/// not allocate per frame.
struct RenderSnapshot {
    std::uint64_t frameIndex = 0;                  ///< Game frame that produced it (monotonic).
    std::chrono::nanoseconds simulationTime{};     ///< Game time at extraction.
    OutputState output{};
    std::array<float, 4> clearColor{0.0F, 0.0F, 0.0F, 1.0F};  ///< Linear RGBA.
    std::uint64_t captureId = 0;                   ///< Non-zero: capture this frame's output.

    // Part 2:
    [[nodiscard]] DrawList2D& addDrawList2D();     ///< Drawn after the scene, in order.
    [[nodiscard]] std::span<const DrawList2D> drawLists2D() const noexcept;
    std::vector<ResourceCommand> resourceCommands; ///< Applied before drawing (see resources).

    void reset();   ///< Clears everything above; keeps allocated capacity.
};
```

### `snapshot_ring.hpp` (part 1)

```cpp
/// Bounded FIFO of snapshot slots between exactly one producer thread (game) and one consumer
/// thread (render). With 2 slots the game thread is at most one frame ahead of the render
/// thread: while the render thread reads snapshot N, the game thread writes N+1, and it blocks
/// in beginWrite() until N is released (back-pressure, so the game loop is paced by the render
/// thread and, through it, by vsync).
/// Thread-safety: *Write from the producer, *Read from the consumer, close() from either.
class SnapshotRing {
public:
    explicit SnapshotRing(std::uint32_t slotCount = 2);   ///< 2..4 (EZMU_ASSERT).

    /// Producer: waits for a free slot and returns it reset(), or nullptr once closed or `stop`
    /// is requested. Must be followed by endWrite() before the next beginWrite().
    [[nodiscard]] RenderSnapshot* beginWrite(const std::stop_token& stop);
    void endWrite();                      ///< Publishes the slot to the consumer.

    /// Consumer: waits for the oldest published snapshot, or nullptr once closed (after the
    /// published ones were read) or `stop` is requested.
    [[nodiscard]] const RenderSnapshot* beginRead(const std::stop_token& stop);
    void endRead();                       ///< Returns the slot to the producer.

    /// Wakes both sides; later begin* return nullptr. Used on shutdown and when the render
    /// thread fails, so the game thread never blocks forever.
    void close();
};
```

### `feedback.hpp` (part 1)

```cpp
struct FrameReport {
    std::uint64_t snapshotFrameIndex = 0;   ///< Game frame rendered (timeline 1).
    std::uint64_t renderFrameIndex = 0;     ///< rhi Frame::index() (timeline 2), if presented.
    std::chrono::nanoseconds renderCpuTime{};   ///< Time spent in Renderer::render.
    std::chrono::nanoseconds waitTime{};        ///< Render thread idle, waiting for a snapshot.
    bool presented = false;                 ///< false: skipped (minimized, out of date, empty).
};

struct CaptureResult {
    std::uint64_t captureId = 0;
    foundation::Result<foundation::Image> image{};  ///< RGBA8 as stored, or why it failed.
    bool srgbEncoded = false;   ///< Values are sRGB-encoded (sRGB output format).
};

/// Render → game channel. The render thread pushes; the game thread drains once per frame.
/// Thread-safety: push* from the render thread, drain from the game thread. One short lock per
/// call; frame reports use a fixed ring (the oldest are dropped if the game thread falls
/// behind, counted in droppedReports).
class RenderFeedbackQueue {
public:
    void pushReport(const FrameReport& report);
    void pushCapture(CaptureResult result);                 ///< Rare; may allocate.
    void pushFatalError(foundation::Error error);           ///< Render thread is stopping.

    struct Drained {
        std::vector<FrameReport> reports;     ///< Reused by the caller between drains.
        std::vector<CaptureResult> captures;
        std::optional<foundation::Error> fatalError;
        std::uint64_t droppedReports = 0;
    };
    void drain(Drained& out);   ///< Swaps contents into `out` (which is cleared first).
};
```

### `renderer.hpp` (part 1)

```cpp
struct RendererDesc {
    rhi::Device* device = nullptr;      ///< Required. Must outlive the Renderer.
    /// Window output. Null: render into an offscreen texture of OutputState::pixelSize
    /// (headless runs and tests). The Renderer owns and destroys it.
    std::unique_ptr<rhi::Swapchain> swapchain{};
    rhi::Format offscreenFormat = rhi::Format::kRgba8Srgb;
    foundation::Logger* logger = nullptr;  ///< Optional.
};

struct FrameResult {
    FrameReport report{};
    std::optional<CaptureResult> capture{};  ///< Set when the snapshot requested a capture.
};

/// Records and submits the GPU work for one snapshot per call.
///
/// Thread-safety: same contract as rhi::Device — created on the thread that owns the device
/// at that moment and then used, and destroyed, only on the device's (render) thread.
/// Lifetime: destroy before the Device; the destructor waits for the GPU and destroys the
/// swapchain (and its surface) and every renderer resource.
class Renderer {
public:
    [[nodiscard]] static foundation::Result<std::unique_ptr<Renderer>> create(RendererDesc desc);
    ~Renderer();

    /// Renders `snapshot`: applies the output state (swapchain resize / offscreen resize),
    /// clears, draws (part 2), copies the output for a capture, ends the frame and presents.
    /// Skips GPU work when the output is empty or not presentable. Errors are fatal for the
    /// renderer (device lost, out of memory); recoverable per-resource errors (part 2) are
    /// reported in the result instead.
    [[nodiscard]] foundation::Result<FrameResult> render(const RenderSnapshot& snapshot);

    /// Offscreen mode only: the current target (kTransferSrc usage), for tests.
    [[nodiscard]] rhi::TextureHandle offscreenTarget() const noexcept;
};
```

### `draw_2d.hpp` (part 2)

Generic 2D triangles in **logical units** (the renderer multiplies by `scale`), so ImGui, the
future game UI and tools can all produce them without a dependency on each other.

```cpp
/// Renderer-level texture id, valid on the game thread (see resources.hpp). Default = the
/// built-in white texture, so untextured geometry needs no texture.
struct TextureId { std::uint32_t index = 0; std::uint32_t generation = 0; /* ==, isValid */ };

struct Vertex2D {
    float x = 0.0F, y = 0.0F;      ///< Logical units, origin top-left of the output.
    float u = 0.0F, v = 0.0F;
    std::uint32_t color = 0xFFFFFFFFU;  ///< RGBA8 packed (R in the low byte), sRGB-encoded,
                                        ///< straight alpha.
};

struct ClipRect { float minX = 0.0F, minY = 0.0F, maxX = 0.0F, maxY = 0.0F; };  ///< Empty = no clip.

struct DrawCommand2D {
    ClipRect clip{};
    TextureId texture{};
    std::uint32_t indexOffset = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t vertexOffset = 0;   ///< Added to every index of the command.
};

struct DrawList2D {
    std::vector<Vertex2D> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<DrawCommand2D> commands;
    float scale = 1.0F;                ///< Logical → pixels (usually OutputState::displayScale).
    void clear();                      ///< Keeps capacity.
};

/// Checks offsets and index ranges. The renderer asserts it in debug builds; producers and
/// tests may call it directly.
[[nodiscard]] foundation::Result<> validate(const DrawList2D& list);
```

### `resources.hpp` (part 2)

The `rhi::Device` belongs to the render thread, but textures such as a font atlas are created
by game-thread code. `RenderResources` lives on the game thread: it **reserves** a `TextureId`
immediately and queues a command that travels inside the next snapshot, so the texture exists
on the render thread before any draw that uses it (same snapshot or later).

```cpp
enum class TextureFormat2D : std::uint8_t { kRgba8Unorm, kRgba8Srgb };

struct TextureCreate {
    TextureId id{};
    foundation::Extent2D size{};
    TextureFormat2D format = TextureFormat2D::kRgba8Unorm;
    std::vector<std::uint8_t> pixels;   ///< Tightly packed; moved, never copied again.
    std::string debugName;
};
/// Replaces a sub-rectangle (added in batch B for ImGui's dynamic textures, ADR 0022).
struct TextureUpdate {
    TextureId id{};
    foundation::Rect2D region{};
    std::vector<std::uint8_t> pixels;   ///< Tightly packed rows of `region`.
};
struct TextureDestroy { TextureId id{}; };
using ResourceCommand = std::variant<TextureCreate, TextureUpdate, TextureDestroy>;

/// Thread-safety: game (main) thread only in M2. Allocation: only when textures are created
/// or updated (pixel payloads).
class RenderResources {
public:
    [[nodiscard]] TextureId createTexture(foundation::Extent2D size, TextureFormat2D format,
                                          std::vector<std::uint8_t> pixels,
                                          std::string_view debugName = {});
    void updateTexture(TextureId id, const foundation::Rect2D& region,
                       std::vector<std::uint8_t> pixels);
    void destroyTexture(TextureId id);    ///< Deferred: in-flight frames may still use it.
    /// Moves the queued commands into `snapshot` (called by runtime during extraction).
    void flushInto(RenderSnapshot& snapshot);
};
```

A texture that fails to create (unsupported size, out of memory) is reported in
`FrameResult` and its id resolves to a built-in magenta "missing" texture, so the frame still
renders and the problem is visible (decision R6).

### Example: one frame on each thread

```cpp
// Game thread (inside runtime's extraction phase)
if (RenderSnapshot* s = ring.beginWrite(stop)) {
    s->frameIndex = frame;
    s->output = {.pixelSize = windowPixels, .displayScale = scale, .presentable = !minimized};
    s->clearColor = {0.1F, 0.2F, 0.4F, 1.0F};
    ring.endWrite();
}

// Render thread (runtime's render loop)
while (const RenderSnapshot* s = ring.beginRead(stop)) {
    auto result = renderer->render(*s);
    ring.endRead();
    if (!result) { feedback.pushFatalError(std::move(result.error())); ring.close(); break; }
    feedback.pushReport(result->report);
    if (result->capture) { feedback.pushCapture(std::move(*result->capture)); }
}
```

## Implementation notes

- **Part 1 rendering** is the `ezmu_clear` render loop moved into the module: begin frame,
  acquire (or use the offscreen target), transition, clear with dynamic rendering, optional
  capture copy into a readback buffer, transition to present, end frame. Capture keeps the M1
  blocking approach (`waitIdle` after the frame, only for captured frames; async capture is a
  later rhi item). BGRA images are swizzled to RGBA. A capture requested on a frame whose
  presentation is skipped (out-of-date swapchain, empty output) is carried over to the next
  rendered frame; if the renderer is destroyed first, it fails with `kUnsupported`.
- **Output changes:** a new `pixelSize` calls `Swapchain::resize`; in offscreen mode the target
  is recreated (old one destroyed through rhi deferred destruction).
- **Snapshot slots** reuse every vector; `DrawList2D`s are recycled by `addDrawList2D()`
  (an active count over a vector that never shrinks), so their inner vectors keep capacity too.
- **Part 2 needs from rhi (batch B):** buffer → texture copy (uploads), sampled textures and a
  sampler, a graphics pipeline from Slang-compiled SPIR-V with alpha blending, dynamic
  viewport/scissor, vertex/index data per frame (per-slot upload buffers), push constants (scale
  and translation), indexed draws. Exact shape is decided in the rhi M2 update.
- **Color:** vertex colors are sRGB-encoded (as ImGui and most UI tools produce them) and are
  linearized in the shader before blending into the sRGB target.
- ImGui ≥ 1.92 asks renderers to create and update textures incrementally; ADR 0022 (batch B)
  picks 1.93, hence `TextureUpdate`. An update is applied with the rhi upload
  allocator and `copyBufferToTexture` on the region, between transitions to `kTransferDst` and
  back to `kShaderRead`.

## Threading

- **Game (main) thread:** `SnapshotRing::beginWrite/endWrite`, `RenderResources`,
  `RenderFeedbackQueue::drain`.
- **Render thread:** `SnapshotRing::beginRead/endRead`, `Renderer` (all calls after creation),
  `RenderFeedbackQueue::push*`.
- Snapshot contents are written only between `beginWrite` and `endWrite` and read only between
  `beginRead` and `endRead`; the ring's mutex provides the happens-before edges. No other state
  is shared.
- Two timelines ([ADR 0012](../adr/0012-dedicated-render-thread.md)): every `FrameReport`
  carries both the game frame index and the rhi frame index, which the debug overlay displays.

## Dependencies

- `ezmu::foundation` (public), `ezmu::rhi` (public: `RendererDesc` names `rhi::Device` and
  `rhi::Swapchain`). No `platform` dependency (output size and state come in the snapshot).
- No third-party library. Shaders for part 2 follow [ADR 0010](../adr/0010-slang-shading-language.md)
  and the toolchain chosen in the rhi M2 update.

## Extension points

- **Configure:** `RendererDesc` (offscreen format), snapshot ring depth (2–4 slots).
- **Extend:** any producer adds `DrawList2D`s and textures (part 2); later renderer milestones
  add scene data and render features behind the same snapshot model.
- **Replace:** the snapshot is plain data; a game can consume it with its own renderer (see
  the [runtime spec](runtime.md#extension-points): `GraphicsMode::kNone` or composing the L2
  modules directly). An `IRenderer` interface is not introduced in M2.

## Data formats / messages

None persisted. Reference images under `engine/renderer/tests/reference/` (PNG, RGBA8), same
workflow as rhi ([ADR 0020](../adr/0020-gpu-tests-headless-and-software-vulkan-in-ci.md)).

## Testing strategy

**No-GPU tests** (every CI preset, including TSan):

- `SnapshotRing`: FIFO order across threads over 10 000 frames; back-pressure (the producer
  blocks while every slot is published, verified with latches, no sleeps); `close()` wakes a
  blocked producer and a blocked consumer; stop tokens; `reset()` keeps capacity (second lap
  does not grow `capacity()`).
- `RenderFeedbackQueue`: drain order, dropped-report counting, fatal error delivery.
- Part 2: `RenderResources` id reservation, generations, commands flushed in order;
  `validate(DrawList2D)` accepts valid lists and rejects out-of-range offsets/indices.

**GPU tests** (label `gpu`, headless device with the validation fixture from rhi tests):

- Offscreen clear: snapshots with exactly representable colors, readback compared exactly.
- Output resize recreates the offscreen target; empty / non-presentable output skips GPU work.
- Capture: `captureId` returns a `CaptureResult` with the expected image in `FrameResult`.
- Pipelined run: a producer thread and the render thread exchange 300 snapshots through the
  ring with changing colors and sizes; no validation messages, reports in order.
- Part 2: reference images for solid quads, textured quads, clip rectangles, alpha blending,
  `vertexOffset`, display scale 1.5; `TextureUpdate` changes only its region; texture
  create/update/destroy churn over 200 frames; failed
  creation renders the "missing" texture and reports the error.
- The swapchain path stays covered by `samples.clear` once `ezmu_clear` runs on `runtime`.

## Done when

- [ ] `engine/renderer` module (`ezmu_add_module(NAME renderer LAYER 2 …)`) with part 1 API,
      documented with thread rules; added to `ezmu::engine`.
- [ ] Part 1 tests pass on all CI presets (GPU tests on lavapipe in Linux CI and on the owner's
      GPU), no validation messages.
- [ ] Part 2 API and tests, after the rhi M2 update (batch B).
- [ ] `engine/renderer/CLAUDE.md` and `.claude/rules/renderer.md` (renderer never reads game
      state; producer headers stay free of rhi; no per-frame allocation once warm).
- [ ] Overview and roadmap updated.

## Decisions (approved 2026-11-15: every proposal below was accepted)

- **R1 — Snapshot ownership.** *Proposal:* `renderer` owns the snapshot types; `debug` will
  depend on `renderer` through `ALLOW_SAME_LAYER` (batch B). *Alternatives:* snapshot types in
  `foundation` (genre-neutral but render-specific vocabulary in L0); `debug` produces its own
  types and `runtime` copies them (double copy, two vocabularies).
- **R2 — Exchange policy.** *Proposal:* FIFO ring with 2 slots and a blocking producer
  (pipelining depth 1, the game is paced by the render thread). *Alternatives:* 3 slots (more
  throughput when frame times vary, one more frame of latency); "latest wins" mailbox (the
  render thread skips stale snapshots; the game runs unthrottled and resource commands inside
  skipped snapshots would need separate handling).
- **R3 — Game-thread resource creation.** *Proposal:* ids reserved on the game thread, commands
  carried inside the snapshot (ordering guaranteed, no extra channel). *Alternative:* a separate
  thread-safe command queue to the render thread (needs its own synchronization with snapshot
  order).
- **R4 — 2D index type.** *Proposal:* 32-bit indices plus `vertexOffset`. *Alternative:*
  16-bit (ImGui's default, half the index memory; widening costs little at overlay sizes).
- **R5 — Capture result.** *Proposal:* the renderer returns a `foundation::Image`; writing the
  PNG is the caller's job (runtime uses a job). *Alternative:* the renderer writes files (file
  I/O and paths on the render thread).
- **R6 — Failed textures.** *Proposal:* error in the frame result + magenta "missing" texture.
  *Alternative:* treat as fatal (stops the game for a recoverable problem).
- **R7 — Renderer owns the swapchain.** *Proposal:* yes (created on the main thread with the
  surface, moved into `RendererDesc`). *Alternative:* runtime keeps it and passes it per frame
  (more parameters, same thread rules).

## Resolved questions (2026-11-15)

- **Several outputs per snapshot?** Not in M2: one output until a tool needs several
  windows/views.

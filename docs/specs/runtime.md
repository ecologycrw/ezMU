# Spec: runtime

- **Status:** Approved (M2 batch A, 2026-11-15; every proposal accepted)
- **Layer:** L3
- **Owner:** ecologycrw
- **Related ADRs:** [0007](../adr/0007-sdl3-platform-layer.md),
  [0011](../adr/0011-layered-architecture-engine-and-framework.md),
  [0012](../adr/0012-dedicated-render-thread.md),
  [0014](../adr/0014-ports-and-adapters-networking.md),
  [0016](../adr/0016-tools-in-repository-public-api-only.md),
  [0019](../adr/0019-ai-assisted-development-workflow.md),
  [0020](../adr/0020-gpu-tests-headless-and-software-vulkan-in-ci.md)
- **Milestone:** M2 "Runtime and render thread" (batch A, with [jobs](jobs.md) and
  [renderer](renderer.md))

## Purpose

`ezmu::runtime` is the **composition root and game loop** of an ezMU application. It creates
and wires the engine services (logger, job system, platform and window, RHI device and
swapchain, renderer), owns the main and render threads, runs the frame in a fixed, documented
sequence of phases, and drives a stack of **game states** supplied by the game. What
`ezmu_clear` does by hand in M1 becomes a few lines of game code on top of `Application`.

The runtime is a **mechanism**: it knows nothing about login screens, character selection or
maps. Games define their states (policy); the runtime decides when their callbacks run.

It is **not** responsible for: action mapping (→ `input`), the ECS world (→ `world`; runtime
will call its systems once it exists), networking (→ `net`; the dispatch phase is reserved
below), UI (→ `ui`), the debug overlay itself (→ `debug`, batch B), settings persistence, crash
dumps and persistent logs (roadmap open topics, deferred — see decision RT7), a general event
bus (deferred until a second consumer needs one).

## Scope for M2

| In M2 | Later (own spec update) |
|---|---|
| `Application`: create (composition root), run, shutdown order | Event bus |
| Main thread frame phases; render thread loop over `SnapshotRing` | Network dispatch phase (with `net`) |
| Game state stack: push / pop / replace, deferred transitions | World systems phase (with `world`) |
| Frame hooks (engine services such as the debug overlay) | Player settings, keybind files |
| Timing: variable update + optional fixed step; frame limiter; background rate | Diagnostics: crash dumps, persistent logs, error reports |
| Window lifecycle → `OutputState`; quit/close handling with veto | Multiple windows (tools) |
| Frame capture to PNG (via renderer feedback + a job) | Hot-reload orchestration |
| Headless mode and "no graphics" mode for tests | |
| `ezmu_clear` ported onto the runtime | |

## Use cases

1. **A game's `main`** fills an `AppDesc`, calls `Application::create`, pushes its first state
   and calls `run()`; `run()` returns the exit code when the last state is popped or quit is
   requested.
2. **Game states** (e.g. a game's login, character selection and in-game states) react to
   events, update with frame and fixed-step time, and write their part of the render snapshot
   during extraction. Switching states is a request applied at a defined point of the frame.
3. **Engine services that run in every state** — the debug overlay (batch B), later input and
   network dispatch — plug in as **frame hooks**, which see events before the states and can
   consume them (e.g. a click on the overlay does not reach the game).
4. **Closing the window** asks the top state (`onQuitRequested`), so a game can show "exit the
   game?" instead of quitting immediately; OS-level quit requests are handled the same way.
5. **Minimizing** stops GPU work (snapshot `presentable = false`) and drops the game loop to a
   low background rate, while the simulation keeps running — an MMORPG client must keep its
   connection alive when minimized.
6. **Captures** for AI-assisted and automated checks: `ctx.requestCapture(path)` produces a PNG
   of the frame as presented, written by a job off the main thread.
7. **Tests and CI**: `headless = true` with `graphics = kNone` runs the full loop, both threads
   and states on any machine (no display, no GPU) with deterministic time; with
   `graphics = kVulkan` it renders offscreen for pixel checks.

## Public API

Headers under `engine/runtime/include/ezmu/runtime/`. Namespace `ezmu::runtime`. Sketch only —
names and exact signatures are refined during implementation and reviewed in the PR.

### `application.hpp`

```cpp
enum class GraphicsMode : std::uint8_t {
    kVulkan,  ///< rhi device + renderer (swapchain, or offscreen when headless).
    kNone,    ///< No GPU: the render thread consumes and releases snapshots only.
};

enum class TimeMode : std::uint8_t {
    kRealTime,  ///< Game time follows the platform clock.
    kFixed,     ///< Every frame advances exactly one fixed step: deterministic tests/captures.
};

struct TimingDesc {
    double fixedUpdateHz = 60.0;                  ///< 0 disables onFixedUpdate.
    std::uint32_t maxFixedStepsPerFrame = 5;      ///< Caps catch-up after a hitch.
    std::chrono::nanoseconds maxFrameDelta = std::chrono::milliseconds(250);  ///< Clamp.
    double maxFrameRate = 0.0;                    ///< 0: paced by the render thread only.
    double backgroundFrameRate = 30.0;            ///< While minimized / nothing presentable.
    TimeMode mode = TimeMode::kRealTime;
};

struct AppDesc {
    std::string_view appName = "ezMU";
    platform::WindowDesc window{};
    bool headless = false;                        ///< Headless platform backend, no window.
    foundation::Extent2D headlessSize{1280, 720}; ///< Offscreen output size when headless.
    GraphicsMode graphics = GraphicsMode::kVulkan;
    rhi::ValidationMode validation = rhi::kDefaultValidationMode;
    rhi::PresentMode presentMode = rhi::PresentMode::kFifo;
    std::uint32_t snapshotSlots = 2;              ///< renderer::SnapshotRing depth (2..4).
    foundation::JobSystemDesc jobs{};
    TimingDesc timing{};
    std::uint64_t frameLimit = 0;                 ///< Quit after N game frames; 0 = no limit.
    bool logToStderr = true;                      ///< Adds a StderrLogSink to the logger.
};

/// Where startup stopped, so a caller can tell "this machine cannot run it" (no display, no
/// Vulkan 1.3 device: kUnsupported) from a real failure, per stage.
enum class StartupStage : std::uint8_t {
    kJobs, kPlatform, kWindow, kDevice, kSurface, kSwapchain, kRenderer, kDone,
};

/// The application: composition root, main loop and owner of the render thread.
///
/// Thread-safety: create, run and destroy on the main thread (the thread that runs main()).
/// Game states and hooks are called on the main thread only.
class Application {
public:
    /// Creates every service in dependency order. On failure, everything created so far is
    /// destroyed in reverse order and `stage` (if given) tells which step failed.
    [[nodiscard]] static foundation::Result<std::unique_ptr<Application>>
    create(const AppDesc& desc, StartupStage* stage = nullptr);
    ~Application();   ///< Shutdown order: see "Lifecycle".

    /// Adds a hook; hooks run in ascending `order` (events: descending, see "Frame").
    void addHook(std::unique_ptr<IFrameHook> hook, std::int32_t order = 0);

    /// Enters `initialState` and runs frames until quit, until the stack is empty, until
    /// frameLimit, or until a fatal error. Returns the exit code (requestQuit) or the error
    /// (ctx.fail, render thread failure). Can be called once.
    [[nodiscard]] foundation::Result<int> run(std::unique_ptr<IGameState> initialState);

    [[nodiscard]] AppContext& context() noexcept;
    [[nodiscard]] foundation::Logger& logger() noexcept;   ///< Add sinks before run().
};
```

### `game_state.hpp`

```cpp
enum class EventResult : std::uint8_t { kIgnored, kConsumed };

struct FrameTime {
    std::uint64_t frameIndex = 0;              ///< Game frame, starts at 0.
    std::chrono::nanoseconds delta{};          ///< Clamped to maxFrameDelta.
    std::chrono::nanoseconds total{};          ///< Game time since run() started.
    float fixedAlpha = 0.0F;                   ///< Fraction of the next fixed step (interpolation).
};

struct FixedStep {
    std::uint64_t stepIndex = 0;
    std::chrono::nanoseconds step{};           ///< 1 / fixedUpdateHz.
};

/// A game-defined mode of the application. Owned by the StateStack. Every callback runs on
/// the main thread with the application context. Only the top state receives events and
/// updates; every state on the stack extracts, bottom to top (decision RT4).
class IGameState {
public:
    virtual ~IGameState() = default;
    virtual void onEnter(AppContext& ctx) {}
    virtual void onExit(AppContext& ctx) {}
    virtual void onPause(AppContext& ctx) {}    ///< Another state was pushed on top.
    virtual void onResume(AppContext& ctx) {}   ///< The state above was popped.
    virtual EventResult onEvent(AppContext& ctx, const platform::Event& event) {
        return EventResult::kIgnored;
    }
    /// Window close or OS quit. Return false to keep running (e.g. show a confirmation).
    virtual bool onQuitRequested(AppContext& ctx) { return true; }
    virtual void onFixedUpdate(AppContext& ctx, const FixedStep& step) {}
    virtual void onUpdate(AppContext& ctx, const FrameTime& time) {}
    virtual void onExtract(AppContext& ctx, renderer::RenderSnapshot& snapshot) {}
};

/// Requests are recorded and applied at the start of the next frame, never in the middle of
/// iterating callbacks. Popping the last state ends run() with exit code 0.
class StateStack {
public:
    void push(std::unique_ptr<IGameState> state);
    void pop();
    void replace(std::unique_ptr<IGameState> state);    ///< pop + push, one transition.
    [[nodiscard]] IGameState* top() const noexcept;     ///< Current, before pending requests.
    [[nodiscard]] std::size_t size() const noexcept;
};
```

### `frame_hook.hpp`

```cpp
/// An engine service or game system that runs every frame, in every state (debug overlay,
/// later input and network dispatch). Main thread only.
class IFrameHook {
public:
    virtual ~IFrameHook() = default;
    virtual EventResult onEvent(AppContext& ctx, const platform::Event& event) {
        return EventResult::kIgnored;
    }
    virtual void onBeginFrame(AppContext& ctx, const FrameTime& time) {}  ///< After events.
    /// After the states extracted: hooks draw on top (e.g. the overlay's DrawList2D).
    virtual void onExtract(AppContext& ctx, renderer::RenderSnapshot& snapshot) {}
    virtual void onShutdown(AppContext& ctx) {}  ///< Before the states are exited.
};
```

### `app_context.hpp`

```cpp
struct RuntimeStats {
    std::chrono::nanoseconds gameFrameTime{};    ///< Main thread work of the last frame.
    std::chrono::nanoseconds waitForRender{};    ///< Time blocked on SnapshotRing back-pressure.
    std::span<const renderer::FrameReport> renderReports;  ///< Drained this frame.
    std::uint32_t validationMessages = 0;        ///< Vulkan validation warnings + errors so far.
    foundation::JobStats jobs{};
};

/// Services and controls available to states and hooks. Main thread only; valid during run().
class AppContext {
public:
    [[nodiscard]] foundation::Logger& logger() noexcept;
    [[nodiscard]] foundation::JobSystem& jobs() noexcept;
    [[nodiscard]] platform::Platform& platform() noexcept;
    [[nodiscard]] platform::Window* window() noexcept;          ///< nullptr when headless.
    [[nodiscard]] renderer::RenderResources& renderResources() noexcept;  ///< renderer part 2.
    [[nodiscard]] StateStack& states() noexcept;
    [[nodiscard]] const FrameTime& time() const noexcept;
    [[nodiscard]] const RuntimeStats& stats() const noexcept;

    void requestQuit(int exitCode = 0);       ///< Ends run() after the current frame.
    void fail(foundation::Error error);       ///< Ends run() with this error after the frame.

    /// Captures the next rendered frame to a PNG at `path` (written by a job). The result is
    /// logged ("capture: WxH <encoding> -> path") and run() waits for pending captures before
    /// returning. Fails with a logged error under GraphicsMode::kNone. Returns the capture id.
    std::uint64_t requestCapture(std::filesystem::path path);
};
```

### `fixed_step.hpp`

The fixed-step accumulator is a small public value type, so its arithmetic is unit-tested
directly and reusable by tools that run their own loops.

```cpp
class FixedStepAccumulator {
public:
    FixedStepAccumulator(std::chrono::nanoseconds step, std::uint32_t maxStepsPerFrame);
    /// Adds frame time; returns how many fixed steps to run now (≤ maxStepsPerFrame; excess
    /// time is dropped and counted).
    [[nodiscard]] std::uint32_t advance(std::chrono::nanoseconds delta) noexcept;
    [[nodiscard]] float alpha() const noexcept;               ///< Leftover / step.
    [[nodiscard]] std::uint64_t droppedSteps() const noexcept;
};
```

### Example: a minimal game

```cpp
class ClearState final : public runtime::IGameState {
public:
    runtime::EventResult onEvent(runtime::AppContext& ctx, const platform::Event& event) override {
        if (const auto* key = std::get_if<platform::KeyDown>(&event);
            key != nullptr && key->scancode == platform::Scancode::kF12 && !key->repeat) {
            ctx.requestCapture("ezmu_clear.png");
            return runtime::EventResult::kConsumed;
        }
        return runtime::EventResult::kIgnored;
    }
    void onExtract(runtime::AppContext&, renderer::RenderSnapshot& snapshot) override {
        snapshot.clearColor = {0.1F, 0.2F, 0.4F, 1.0F};
    }
};

int main() {
    auto app = runtime::Application::create({.appName = "ezmu_clear"});
    if (!app) { /* report, exit */ }
    auto exitCode = (*app)->run(std::make_unique<ClearState>());
    return exitCode ? *exitCode : 1;
}
```

## Frame

Each main-thread frame runs these phases, in this order (the order is part of the contract):

1. **Feedback:** drain `RenderFeedbackQueue` — store frame reports in `RuntimeStats`, hand
   capture images to jobs that write the PNGs, turn a fatal render error into the result of
   `run()`. Then `jobs.runPending()` (progress for inline mode).
2. **Transitions:** apply pending state requests (exit/enter, pause/resume); stop if the stack
   is empty.
3. **Events:** `platform.pollEvents()`. The runtime first applies window lifecycle events to its
   `OutputState` (resize, minimize/restore, display scale) — these are always delivered to
   hooks and states too, and cannot be consumed away from the runtime. Then each event goes to
   hooks in **descending** order and, unless consumed, to the top state. Close/quit requests ask
   the top state's `onQuitRequested`.
4. *(Reserved: network dispatch — ADR 0014 "delivery at a defined point of the frame".)*
5. **Begin:** hooks' `onBeginFrame` in ascending order.
6. **Fixed updates:** 0..N `onFixedUpdate` of the top state (`FixedStepAccumulator`).
7. **Update:** top state's `onUpdate`.
8. **Extraction:** `ring.beginWrite` (blocking here is the back-pressure, measured as
   `waitForRender`), fill frame index, time, `OutputState` and pending capture id, flush
   `RenderResources` commands, states' `onExtract` bottom to top, hooks' `onExtract` ascending,
   `ring.endWrite`.
9. **Pacing:** sleep until the frame-rate cap (`maxFrameRate`, or `backgroundFrameRate` while
   nothing is presentable) using a precise platform sleep; nothing in `kFixed` time mode
   unless a cap is set.

The **render thread** runs the loop shown in the [renderer spec](renderer.md#example-one-frame-on-each-thread):
`beginRead` → `Renderer::render` → `endRead` → push feedback. With `GraphicsMode::kNone` it only
releases snapshots and pushes reports with `presented = false`, so the threading model is
exercised on machines without a GPU.

## Lifecycle

**Startup** (`create`, main thread), stopping at the first failure and unwinding in reverse:
logger → job system → platform (`kHeadless` when `headless`) → window (unless headless) →
device (window instance extensions, or headless device) → surface → swapchain (window only) →
renderer (on the main thread; the device is handed to the render thread when it starts, the
thread start being the happens-before edge required by rhi) → ring and feedback queue. The
render thread starts in `run()`, so a failed `create` never involves threads.

Errors use `ErrorCode::kUnsupported` when the machine lacks a capability (no display, no Vulkan
library, no Vulkan 1.3 adapter, no presentation support), so samples can report "skipped"
(exit code 77, [ADR 0020](../adr/0020-gpu-tests-headless-and-software-vulkan-in-ci.md))
together with `StartupStage`; the skip policy itself stays in the application.

**Shutdown** (end of `run()` and destructor):

1. Hooks' `onShutdown`, then every state's `onExit`, top to bottom. States must wait for their
   own `JobGroup`s here (a `JobGroup` asserts if destroyed while busy).
2. The main thread stops producing snapshots and closes the ring; the render thread renders
   the snapshots already published, gets `nullptr` from `beginRead` and exits; join. The
   renderer (with the swapchain and its surface) is destroyed on the render thread before it
   exits. If the render thread failed, it closed the ring itself, so the main thread never
   stays blocked in `beginWrite`.
3. Drain the last feedback and wait for pending capture jobs (captures requested in the last
   frames are not lost — needed by `ezmu_clear --frames N --capture`).
4. Destroy the job system (runs anything still queued), then the device, then the window, then
   the platform — the device goes **before** the window because SDL unloads the Vulkan library
   with the last Vulkan window (M1 lesson, see the [rhi spec](rhi.md#differences-from-the-sketch-step-8-ezmu_clear)).

## Threading

- **Main thread:** `Application`, `AppContext`, states, hooks, platform, `RenderResources`,
  `SnapshotRing` producer side, feedback drain.
- **Render thread:** owned by the runtime (`std::jthread`); `Renderer`, rhi device after
  handover, `SnapshotRing` consumer side, feedback push. No game code runs there in M2.
- **Job workers:** whatever states, hooks and the runtime submit (in M2: PNG writing).
- The only shared objects between main and render threads are the `SnapshotRing` and the
  `RenderFeedbackQueue`, plus an atomic validation-message counter written by the rhi callback.
- Debug builds assert the main-thread rule in `AppContext` and `StateStack` (thread id recorded
  in `create`, as `platform::Platform` does).

## Dependencies

- `ezmu::foundation`, `ezmu::platform`, `ezmu::rhi`, `ezmu::renderer` (all public: they appear
  in `AppDesc` and `AppContext`). Batch B adds `ezmu::debug`.
- **Small `platform` addition:** a precise sleep (`platform::sleepFor(std::chrono::nanoseconds)`,
  wrapping `SDL_DelayPrecise`, callable from any thread) for frame pacing; the default
  `std::this_thread::sleep_for` granularity on Windows (~15.6 ms) is too coarse. Recorded as a
  one-line update of the [platform spec](platform.md).
- No third-party library.

## Extension points

- **Configure:** `AppDesc` (window, headless, graphics, validation, present mode, ring depth,
  jobs, timing, frame limit).
- **Extend:** game states (policy of the game), frame hooks (services that run in every state),
  extra log sinks through `Application::logger()`.
- **Replace:** `GraphicsMode::kNone` plus the public `renderer`/`rhi` APIs let a game drive its
  own rendering; games that want a completely different loop skip `runtime` and compose the L2
  modules themselves, as `ezmu_clear` did in M1. A pluggable render-thread body is not
  introduced until someone needs it.

## Data formats / messages

None persisted. Capture PNGs follow the M1 convention (RGBA8 as stored; the log line names the
encoding so tests can compare sRGB or UNORM values).

## Testing strategy

**No-GPU tests** (every CI preset, including TSan): `headless = true`,
`graphics = GraphicsMode::kNone`, `TimeMode::kFixed`, events injected with
`platform::Platform::injectEvent`, no wall-clock sleeps.

- Lifecycle: create / run / destroy; a recording state and hook check the exact callback
  order for a scripted session (enter, updates, push → pause + enter, pop → exit + resume,
  quit → hooks' shutdown then exit top to bottom).
- Transitions requested during frame N take effect at the start of N+1; popping the last state
  ends `run()` with 0; `replace` is a single transition.
- Event routing: a hook that consumes a key hides it from the state; descending hook order;
  window lifecycle events still update `OutputState` (checked through a state's `onExtract`
  input) and reach states.
- Quit: window close with `onQuitRequested` returning false keeps running; true ends the run;
  `requestQuit(3)` returns 3; `fail(error)` returns the error; `frameLimit` stops after N frames.
- `FixedStepAccumulator`: step counts, alpha, cap and dropped steps for synthetic deltas.
- Minimized: `presentable = false` reaches the snapshot; with `kFixed` the background cap is
  not applied in tests (cap only with real time), so no sleeps happen.
- Inline jobs (`jobs.workerCount = 0`) still complete fire-and-poll work through `runPending`.
- `create` failure unwinding: an invalid `AppDesc` (e.g. 1 snapshot slot) fails at the right
  `StartupStage` without leaks (ASan/LSan in Linux CI).

**GPU tests** (label `gpu`, `headless = true`, `graphics = kVulkan`): a state sets a clear color
and requests a capture on frame 3; the PNG matches the color (exact, offscreen `kRgba8Srgb`
encoding); no validation messages over 100 frames with output size changes.

**Sample test:** `samples.clear` keeps passing unchanged in behavior after `ezmu_clear` is
ported (window, swapchain, vsync, resize, minimize, capture), and the owner re-checks resize,
minimize/restore and 150 % DPI on the desktop.

## Done when

- [ ] `engine/runtime` module (`ezmu_add_module(NAME runtime LAYER 3 …)`) with the API above,
      documented with thread rules; added to `ezmu::engine`.
- [ ] Tests above pass on all CI presets (TSan included); GPU tests on lavapipe and the owner's
      GPU, no validation messages.
- [ ] `platform::sleepFor` added with a test (no assertion on duration precision in CI).
- [ ] `ezmu_clear` runs on `runtime`: the mailbox is gone, the sample is a state plus option
      parsing, `samples.clear` passes, the owner re-checked it on the desktop.
- [ ] `engine/runtime/CLAUDE.md`, `.claude/rules/threading.md`; overview "Threads" section and
      roadmap updated.

## Implementation plan (M2 batch A)

Each step is a small, verified Conventional Commit (`git commit -s`). Order matters.

1. **`build: add ThreadSanitizer preset`** — `linux-clang-tsan` (configure, build and test
   presets), sanitizer selection in `cmake/EzmuSanitizers.cmake`, CI job running `-LE gpu`.
2. **`feat(foundation): job system`** — [jobs spec](jobs.md), tests, foundation `CLAUDE.md`.
3. **`feat(renderer): snapshots, ring, feedback and clear renderer`** — new module, part 1 of
   the [renderer spec](renderer.md) (capture code moved from `ezmu_clear`), tests,
   `engine/renderer/CLAUDE.md`, `.claude/rules/renderer.md`; add to `ezmu::engine`.
4. **`feat(platform): precise sleep`** — `sleepFor`, test, platform spec note.
5. **`feat(runtime): application, game loop and game states`** — this spec; no-GPU and GPU
   tests; `engine/runtime/CLAUDE.md`; add to `ezmu::engine`.
6. **`refactor(samples): run ezmu_clear on runtime`** — mailbox and render loop removed;
   exit-code policy (77) kept in the sample using `StartupStage` and `EZMU_REQUIRE_GPU`;
   `samples.clear` unchanged or adapted to the capture log line.
7. **`docs: …`** — overview (threads, modules), roadmap (M2 progress), `.claude/rules/threading.md`.

Batch B (separate review): rhi M2 update (pipelines, uploads, Slang toolchain, ADR 0023),
renderer part 2, `debug` spec + ADR 0022 (Dear ImGui integration), overlay in `ezmu_clear`.
The runtime additions for the overlay (`AppDesc::debugOverlay`, `AppContext::debugOverlay()`,
`EZMU_DEBUG_TOOLS`) are specified in the [debug spec](debug.md#changes-to-other-specs-batch-b).

### Path-scoped rules

- **`.claude/rules/threading.md`** (`paths: engine/runtime/**, engine/renderer/**,
  engine/foundation/**/jobs*`): every public function states its thread; main ↔ render thread
  share only the snapshot ring, the feedback queue and documented atomics; every atomic has a
  memory-order comment; no blocking on the render thread other than the ring, vsync and
  captures; tests synchronize with latches/barriers, never sleeps; TSan must stay clean.
- **`.claude/rules/renderer.md`** (`paths: engine/renderer/**`): see the
  [renderer spec](renderer.md#done-when).

## Decisions (approved 2026-11-15: every proposal below was accepted)

- **RT1 — Loop timing.** *Proposal:* variable-rate update every frame plus an optional
  fixed-step `onFixedUpdate` (default 60 Hz, cap 5 steps, interpolation alpha); `kFixed` time
  mode for deterministic runs. *Alternatives:* variable only (simpler, less reproducible logic);
  fixed only (forces interpolation onto everything visual).
- **RT2 — Pacing.** *Proposal:* the main thread is paced by snapshot back-pressure (and so by
  vsync); an optional `maxFrameRate` cap; `backgroundFrameRate` (30 Hz) while minimized so the
  simulation and connection keep running without burning a core. *Alternative:* block the main
  thread while minimized as `ezmu_clear` M1 did with its render thread (stops the simulation —
  unacceptable for an online client).
- **RT3 — Extension model.** *Proposal:* two concepts — game states (one active, stack) for game
  modes, and frame hooks (always on, ordered) for services. *Alternatives:* a single "layer
  stack" for both (blurs game policy and engine services); a general event bus now (no second
  consumer yet; listed as later).
- **RT4 — Stacked states.** *Proposal:* only the top state gets events and updates; all states
  extract bottom to top (a menu state over the in-game state still shows the world).
  *Alternative:* per-state flags (`updatesBelow`, `drawsBelow`) — more flexible, more rules;
  can be added later without breaking the default.
- **RT5 — Deferred transitions.** *Proposal:* state requests apply at the start of the next
  frame. *Alternative:* immediate transitions (re-entrancy hazards while iterating callbacks).
- **RT6 — Error model for states.** *Proposal:* callbacks return `void`; `ctx.fail(error)` and
  `ctx.requestQuit(code)` end the run after the frame. *Alternative:* every callback returns
  `Result<>` (noisy for the common case).
- **RT7 — Deferred roadmap topics.** *Proposal:* diagnostics (crash dumps, persistent logs,
  error reports) and player settings stay out of M2; the runtime only allows extra log sinks.
  *Alternative:* add a file log sink to `foundation` now (small; see open questions).
- **RT8 — `ezmu_clear` on runtime.** *Proposal:* port the existing sample instead of adding a
  new one, so `samples.clear` keeps guarding the whole stack. *Alternative:* keep `ezmu_clear`
  as the low-level example and add a new runtime sample (two samples to maintain).

## Resolved questions (2026-11-15)

- **`FileLogSink` in M2?** No: it follows RT7 and waits for the diagnostics spec; M2 only lets
  applications add their own sinks through `Application::logger()`.
- **Capture callback?** No: `AppContext::requestCapture` takes a path only in M2; a callback
  receiving the `Image` is added with its first consumer.

# Spec: jobs (foundation job system)

- **Status:** Approved (M2 batch A, 2026-11-15; every proposal accepted)
- **Layer:** L0 (part of `foundation`)
- **Owner:** ecologycrw
- **Related ADRs:** [0005](../adr/0005-target-platforms-and-compilers.md),
  [0011](../adr/0011-layered-architecture-engine-and-framework.md),
  [0012](../adr/0012-dedicated-render-thread.md), [0018](../adr/0018-code-commit-and-branch-conventions.md),
  [0019](../adr/0019-ai-assisted-development-workflow.md)
- **Milestone:** M2 "Runtime and render thread" (batch A, with [renderer](renderer.md) and
  [runtime](runtime.md))

## Purpose

The job system runs short, independent CPU tasks on a fixed pool of worker threads. It is a
**service** owned by the composition root (`runtime`) and passed explicitly to whoever needs
it; there is no global scheduler. A thread that waits for jobs helps execute them, so waiting
inside a job cannot deadlock the pool.

It is **not** responsible for: the main (game) thread or the render thread — both are dedicated
threads owned by `runtime` ([ADR 0012](../adr/0012-dedicated-render-thread.md)), not workers;
asynchronous file I/O (→ VFS / `assets`, later); fibers, task graphs with dependencies between
jobs, priorities or thread affinity (later, behind the same API, when a consumer needs them);
GPU work.

## Scope for M2

| In M2 | Later (own spec update) |
|---|---|
| Fixed worker pool, created and destroyed by its owner | Priorities (e.g. high / normal / background queues) |
| `submit` into a `JobGroup`, `wait` that helps, `isDone` polling | Job dependencies / task graph |
| `parallelFor` over an index range with a grain size | Work stealing with per-worker queues |
| Inline mode (`workerCount = 0`) for deterministic tests and debugging | Thread names and profiler zones (Tracy, ADR 0008) |
| Allocation-free submission (inline callable storage, fixed ring) | Main-thread-only job queue |
| Statistics for the debug overlay | Worker pinning / affinity |
| ThreadSanitizer CI preset | |

## Use cases

1. **Runtime** creates one `JobSystem` at startup (worker count derived from the hardware, see
   decision J1) and hands a reference to services and game states through its context.
2. **Fire and poll:** the main thread submits background work (in M2: encoding a frame capture
   to PNG; later: asset decoding) into a `JobGroup`, keeps simulating, and checks
   `group.isDone()` on later frames instead of blocking.
3. **Fork and join:** a system splits a loop with `parallelFor` (later: culling, animation
   pose evaluation, particle update) and continues when every chunk ran; the calling thread
   executes chunks too.
4. **Nested jobs:** a job submits sub-jobs and waits for them; the waiting worker runs queued
   jobs meanwhile, so the pool never starves itself.
5. **Tests and debugging:** with `workerCount = 0` every job runs on the thread that calls
   `wait` or `runPending` (see *Inline mode*), in submission order — deterministic and easy to
   step through in a debugger.
6. **Debug overlay** (M2 batch B): reads `stats()` to show queued/executed jobs and how often
   the queue overflowed.

## Public API

Header `engine/foundation/include/ezmu/foundation/jobs.hpp`. Namespace `ezmu::foundation`.
Sketch only — names and exact signatures are refined during implementation and reviewed in the
PR.

```cpp
/// Move-only callable with inline storage: no heap allocation, ever. A callable that does not
/// fit (captures larger than kJobStorageSize) or is not nothrow-move-constructible is a compile
/// error — capture a pointer to the data instead.
class Job {
public:
    static constexpr std::size_t kJobStorageSize = 48;
    template <typename F> requires std::invocable<F&> /* + size/alignment checks */
    Job(F&& callable);
    Job(Job&&) noexcept;
    Job& operator=(Job&&) noexcept;
    void operator()();        ///< Runs the callable once. Exceptions escaping a job terminate.
};

/// Counts the unfinished jobs submitted into it. Owned by the caller (stack or member); it must
/// outlive its jobs: the destructor asserts isDone(). Reusable once done.
/// Thread-safety: isDone() from any thread; submit/wait as described on JobSystem.
class JobGroup {
public:
    JobGroup() = default;
    JobGroup(const JobGroup&) = delete;
    JobGroup& operator=(const JobGroup&) = delete;
    ~JobGroup();
    [[nodiscard]] bool isDone() const noexcept;
};

inline constexpr std::uint32_t kAutoWorkerCount = std::numeric_limits<std::uint32_t>::max();

struct JobSystemDesc {
    /// kAutoWorkerCount: hardware threads minus 2 (main + render thread), at least 1.
    /// 0: inline mode, no worker threads.
    std::uint32_t workerCount = kAutoWorkerCount;
    /// Capacity of the shared queue (power of two). Submitting to a full queue runs the job on
    /// the submitting thread instead (counted in JobStats::inlineOverflows).
    std::uint32_t queueCapacity = 4096;
    /// Optional, called once on each worker thread before it takes jobs (e.g. to name the
    /// thread or register it with a profiler — OS calls live outside foundation).
    std::function<void(std::uint32_t workerIndex)> onWorkerStart{};
};

struct JobStats {
    std::uint64_t submitted = 0;
    std::uint64_t executed = 0;
    std::uint64_t inlineOverflows = 0;   ///< Jobs run inline because the queue was full.
    std::uint32_t queued = 0;            ///< Snapshot of the queue depth.
    std::uint32_t maxQueued = 0;         ///< High-water mark since creation.
};

/// Fixed pool of worker threads sharing one bounded queue.
///
/// Thread-safety: create and destroy on one thread (the owner). submit, wait, parallelFor and
/// stats may be called from any thread, including workers and the render thread.
/// Allocation: create() allocates the queue and threads; nothing else allocates.
class JobSystem {
public:
    [[nodiscard]] static Result<std::unique_ptr<JobSystem>> create(const JobSystemDesc& desc);
    /// Runs every job still queued, then joins the workers. Submitting during destruction is an
    /// assertion.
    ~JobSystem();

    [[nodiscard]] std::uint32_t workerCount() const noexcept;

    /// Queues `job`; it counts towards `group` until it finished running.
    void submit(JobGroup& group, Job job);

    /// Returns when every job of `group` finished. Executes queued jobs (of any group) while
    /// waiting, then blocks without spinning when the queue is empty.
    void wait(JobGroup& group);

    /// Calls body(begin, end) for consecutive chunks of [0, count) of at most `grain` indices,
    /// in parallel, and returns when all chunks ran. The calling thread executes chunks too.
    /// `body` must be safe to call concurrently. Does not allocate.
    template <typename F> requires std::invocable<F&, std::size_t, std::size_t>
    void parallelFor(std::size_t count, std::size_t grain, F&& body);

    /// Runs the jobs queued at the time of the call on the calling thread (see Inline mode).
    void runPending();

    [[nodiscard]] JobStats stats() const noexcept;
};
```

### Example

```cpp
JobGroup group;
for (Chunk& chunk : chunks) {
    jobs.submit(group, [&chunk] { chunk.decode(); });   // captures one pointer: fits inline
}
jobs.wait(group);                                       // helps until every chunk is decoded

jobs.parallelFor(particles.size(), 256, [&](std::size_t begin, std::size_t end) {
    for (std::size_t i = begin; i < end; ++i) { particles[i].integrate(dt); }
});
```

### Inline mode

With `workerCount = 0` there are no threads: `submit` only queues, and `wait` / `parallelFor`
run the queued jobs on the calling thread in submission order. Polling `isDone()` without ever
calling `wait` therefore never completes in inline mode; the owner (runtime) calls
`runPending()` once per frame so fire-and-poll work still progresses. `runPending()` exists in
every mode and is a cheap no-op when workers keep the queue empty.

### Errors

`create` fails with `kInvalidArgument` for a `queueCapacity` that is zero or not a power of two
and with `kInternal` if a thread cannot be started (already started threads are joined). Jobs
return nothing: results and errors travel through state the job captures. An exception that
escapes a job calls `std::terminate` (jobs run in a `noexcept` context), consistent with "no
exceptions for control flow" ([ADR 0018](../adr/0018-code-commit-and-branch-conventions.md)).

## Implementation notes

- **Queue:** one bounded ring buffer protected by a mutex; workers sleep on a condition
  variable when it is empty. Simple and correct first; the API (groups, `wait` that helps)
  allows replacing it with per-worker work-stealing deques later without changing callers.
- **Groups:** `std::atomic<std::uint32_t>` pending count; `wait` re-checks it between jobs and
  blocks with C++20 `std::atomic::wait` / `notify_all` when there is nothing to help with.
- **parallelFor** stores the body and an atomic "next chunk" index on the caller's stack and
  submits up to `workerCount` helper jobs that claim chunks until none are left; the caller
  claims chunks too, then waits for the helpers. No allocation, no per-chunk job.
- **No OS calls in foundation** ([ADR 0005](../adr/0005-target-platforms-and-compilers.md)):
  `std::jthread` only. Naming threads needs OS APIs, so it is left to `onWorkerStart` (the
  runtime can call a `platform` helper; added together with profiling).
- Memory orderings are documented next to every atomic; relaxed only for statistics.

### Differences from the sketch (M2 batch A, step 2)

- **Storage constant and trait.** `kJobStorageSize` (48) is a namespace-scope constant, and the
  compile-time check is a public concept, `JobCallable<F>`: invocable with no arguments, at most
  48 bytes and `alignof(std::max_align_t)`, nothrow-move-constructible and -destructible, and
  not `Job` itself. Tests `STATIC_CHECK` it; `Job`'s converting constructor is constrained by it.
- **Empty `Job`.** `Job` is default-constructible (empty, also the moved-from state) and has
  `isEmpty()`; the queue slots need it. `operator()` is `noexcept` (an escaping exception
  terminates) and asserts on an empty job. Submitting an empty job asserts.
- **Group completion uses the condition variable, not `std::atomic::wait`.** A waiter may
  destroy its `JobGroup` as soon as the count reaches zero, so `notify_all` on the group's atomic
  after the last decrement would touch a destroyed object. The completing thread instead takes
  the queue mutex briefly and notifies the system's single condition variable, which workers and
  waiters share (waiters wake for new jobs too, and help). Cost: one lock and a `notify_all` per
  completed group, which can wake idle workers spuriously; revisit with profiling.
- **Callable destroyed before completion.** A job's callable is destroyed before its group is
  decremented, so a group owner may free captured state as soon as `isDone()` is true.
- **`JobGroup` destructor.** It asserts when the group is not done and then waits for it
  (helping), so even in release builds jobs never decrement a destroyed group. A group remembers
  the `JobSystem` it was submitted to; using one group with two systems asserts.
- **Destruction.** `~JobSystem` stops the workers, which drain the queue first, including jobs
  that running jobs submit (nested submits during destruction are therefore allowed); in inline
  mode the destructor runs them on the calling thread. Only after the queue is drained and
  closed does `submit` assert.
- **`parallelFor`.** With a single chunk, or in inline mode, the calling thread runs the chunks
  directly without using the queue (it does not run other queued jobs; in inline mode only
  `wait`, `runPending` and the destructor do). A grain of 0 is treated as 1. Otherwise it
  submits `min(workerCount, chunks - 1)` helper jobs, as sketched.
- **`runPending`.** Runs at most as many jobs as were queued when it was called; jobs those jobs
  submit wait for the next call, so a job that resubmits itself cannot make it loop forever.
- **Class shape.** `JobSystem` is non-copyable and non-movable and keeps its state behind a
  private `Impl` (no `<thread>`/`<mutex>` in the public header). `stats()` reads each counter
  independently; the fields are not one consistent snapshot.
- **ThreadSanitizer.** Verified only in CI (`linux-clang-tsan`, Linux): MSVC has no TSan, so the
  Windows development machine only validates the preset configuration.

## Dependencies

- Standard library only (`<thread>`, `<atomic>`, `<mutex>`, `<condition_variable>`).
- No third-party library, so no ADR: see decision J3 for the libraries considered.

## Extension points

- **Configure:** `JobSystemDesc` (worker count, queue size, worker start hook).
- **Extend:** any module or game submits its own jobs; `onWorkerStart` lets the application
  integrate profilers or thread naming.
- **Replace:** consumers depend on `JobSystem&`; a game can run with `workerCount = 0` or use
  its own threads. An `IJobSystem` interface is not introduced (YAGNI, as for `platform`).

## Data formats / messages

None.

## Testing strategy

Unit tests in `engine/foundation/tests/jobs_tests.cpp`, through the public API, with no
wall-clock sleeps (synchronization with `std::latch` / `std::barrier` and the API itself):

- Every submitted job runs exactly once (1, 2, 4, 8 workers and inline mode; 10 000 jobs).
- Inline mode runs jobs in submission order on the waiting thread (thread id recorded).
- `wait` helps: with 1 worker blocked by a latch, the waiting thread executes the other jobs.
- Nested submit + wait inside jobs with 1 worker completes (no deadlock).
- `parallelFor` visits every index of [0, count) exactly once for edge cases (count 0, 1,
  count < grain, count not a multiple of grain, grain 1).
- A full queue runs the job inline and increments `inlineOverflows`.
- The destructor runs jobs still queued; `JobGroup` destructor asserts when not done (RAII
  assertion-handler guard, as in existing foundation tests).
- `Job` rejects oversized captures (compile-time check, `STATIC_CHECK` on a trait).
- Stress: 100 000 tiny jobs across groups from several submitting threads.

**ThreadSanitizer** (decision J4): a new `linux-clang-tsan` preset (Linux only: MSVC has no
TSan) and CI job runs the non-GPU tests (`-LE gpu`), so data races in `jobs`, the snapshot ring
and the runtime are caught automatically. ASan/UBSan presets stay as they are.

## Done when

- [x] `jobs.hpp` implemented in `foundation`, documented with thread rules and allocation
      behavior.
- [ ] Unit tests above pass on all CI presets, including the new TSan job. *(Local: MSVC debug
      and release; CI not run yet — the commits are not pushed.)*
- [ ] `linux-clang-tsan` preset added (`CMakePresets.json`, `cmake/EzmuSanitizers.cmake`) and
      run in CI. *(Preset, sanitizer selection and CI job added; not yet confirmed in CI.)*
- [x] `engine/foundation/CLAUDE.md` updated (public API table, pitfalls: job lifetimes, inline
      mode needs `runPending`).
- [x] `.claude/rules/threading.md` written (see [runtime spec](runtime.md#path-scoped-rules)).

## Decisions (approved 2026-11-15: every proposal below was accepted)

- **J1 — Default worker count.** *Proposal:* `hardware_concurrency() - 2` (main and render
  threads already exist), minimum 1. *Alternatives:* `- 1` (oversubscribes when both dedicated
  threads are busy); a fixed small number such as 4 (predictable, wastes large CPUs).
- **J2 — Callable storage.** *Proposal:* in-house `Job` with 48 bytes of inline storage and a
  compile error for larger captures, so submission never allocates. *Alternatives:*
  `std::move_only_function` (standard, but its small-buffer size is implementation-defined and
  it may allocate); `std::function` (copyable only, allocates).
- **J3 — In-house vs. library.** *Proposal:* in-house, small (one queue, groups, parallelFor),
  no ADR needed. *Alternatives:* enkiTS (zlib, mature, pinned tasks), Taskflow (MIT, task
  graphs, heavier), marl (Apache-2.0, fibers). All would need an ADR; none is required by the
  M2 consumers, and the API above leaves room to switch the implementation.
- **J4 — ThreadSanitizer in CI.** *Proposal:* yes, from this step, because M2 is the
  milestone that introduces real concurrency (jobs, snapshot ring, render thread).
  *Alternative:* rely on review and ASan (does not detect races).
- **J5 — Full queue.** *Proposal:* run the job inline on the submitting thread (never blocks,
  never allocates, visible in stats). *Alternatives:* block until space is available (can
  deadlock if a worker submits); grow the queue (allocates in the hot path).

## Resolved questions (2026-11-15)

- **May the render thread `wait` on jobs?** Yes, documented as "keep it short" (it may stall a
  frame); revisit with profiling.

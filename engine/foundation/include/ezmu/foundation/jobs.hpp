#pragma once

#include <ezmu/foundation/assert.hpp>
#include <ezmu/foundation/result.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

// Job system (docs/specs/jobs.md): a fixed pool of worker threads sharing one bounded queue.
// It is a service owned by the composition root and passed explicitly; there is no global
// scheduler. A thread that waits for jobs helps execute them.

namespace ezmu::foundation {

class Job;
class JobSystem;

/// Bytes of inline storage in a Job; larger captures do not compile.
inline constexpr std::size_t kJobStorageSize = 48;

/// A callable that a Job can store: invocable with no arguments, fits kJobStorageSize bytes
/// with at most max_align_t alignment, and is nothrow-move-constructible and -destructible.
/// Capture a pointer to the data when a callable does not satisfy it.
template <typename F>
concept JobCallable =
    !std::same_as<std::remove_cvref_t<F>, Job> && std::invocable<std::decay_t<F>&> &&
    std::constructible_from<std::decay_t<F>, F> && sizeof(std::decay_t<F>) <= kJobStorageSize &&
    alignof(std::decay_t<F>) <= alignof(std::max_align_t) &&
    std::is_nothrow_move_constructible_v<std::decay_t<F>> &&
    std::is_nothrow_destructible_v<std::decay_t<F>>;

/// Move-only callable with inline storage: constructing, moving and running a Job never
/// allocates. A default-constructed (or moved-from) Job is empty.
/// Thread-safety: a Job is a value; distinct Jobs may be used from distinct threads.
class Job {
public:
    Job() noexcept = default;

    /// Stores `callable` inline. Implicit, so lambdas can be passed to JobSystem::submit.
    template <JobCallable F>
    Job(F&& callable) noexcept(std::is_nothrow_constructible_v<std::decay_t<F>, F>)
        : m_ops(&kOpsFor<std::decay_t<F>>) {
        ::new (static_cast<void*>(m_storage.data())) std::decay_t<F>(std::forward<F>(callable));
    }

    Job(Job&& other) noexcept { takeFrom(other); }
    Job& operator=(Job&& other) noexcept {
        if (this != &other) {
            reset();
            takeFrom(other);
        }
        return *this;
    }
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;
    ~Job() { reset(); }

    /// True when the Job holds no callable.
    [[nodiscard]] bool isEmpty() const noexcept { return m_ops == nullptr; }

    /// Runs the callable (asserts the Job is not empty). An exception escaping the callable
    /// calls std::terminate.
    void operator()() noexcept {
        EZMU_ASSERT_MSG(m_ops != nullptr, "running an empty Job");
        m_ops->invoke(m_storage.data());
    }

private:
    struct Ops {
        void (*invoke)(void* self) noexcept = nullptr;
        /// Move-constructs into `dst` and destroys `src`.
        void (*relocate)(void* dst, void* src) noexcept = nullptr;
        void (*destroy)(void* self) noexcept = nullptr;
    };

    template <typename D>
    static D* as(void* self) noexcept {
        return std::launder(static_cast<D*>(self));
    }

    template <typename D>
    static constexpr Ops kOpsFor{
        .invoke = [](void* self) noexcept { (*as<D>(self))(); },
        .relocate =
            [](void* dst, void* src) noexcept {
                ::new (dst) D(std::move(*as<D>(src)));
                as<D>(src)->~D();
            },
        .destroy = [](void* self) noexcept { as<D>(self)->~D(); },
    };

    void reset() noexcept {
        if (m_ops != nullptr) {
            m_ops->destroy(m_storage.data());
            m_ops = nullptr;
        }
    }
    void takeFrom(Job& other) noexcept {
        if (other.m_ops != nullptr) {
            other.m_ops->relocate(m_storage.data(), other.m_storage.data());
            m_ops = std::exchange(other.m_ops, nullptr);
        }
    }

    alignas(std::max_align_t) std::array<std::byte, kJobStorageSize> m_storage{};
    const Ops* m_ops = nullptr;
};

/// Counts the unfinished jobs submitted into it. Owned by the caller (stack or member) and
/// reusable once done. It should outlive its jobs: destroying a group that is not done asserts
/// and then waits for its jobs (helping, like JobSystem::wait), so they never touch a destroyed
/// group. Its JobSystem must outlive it while it has unfinished jobs.
/// Thread-safety: isDone() from any thread; submit/wait as described on JobSystem.
class JobGroup {
public:
    JobGroup() = default;
    JobGroup(const JobGroup&) = delete;
    JobGroup& operator=(const JobGroup&) = delete;
    JobGroup(JobGroup&&) = delete;
    JobGroup& operator=(JobGroup&&) = delete;
    ~JobGroup();

    /// True when every job submitted into the group finished running. When it returns true,
    /// the effects of those jobs are visible to the calling thread.
    [[nodiscard]] bool isDone() const noexcept {
        // acquire: pairs with the acq_rel decrement after each job, so job effects are visible.
        return m_pending.load(std::memory_order_acquire) == 0;
    }

private:
    friend class JobSystem;

    std::atomic<std::uint32_t> m_pending{0};
    /// Set by submit; only read by the destructor when the group is not done.
    std::atomic<JobSystem*> m_system{nullptr};
};

/// JobSystemDesc::workerCount value: hardware threads minus 2 (main and render thread), at
/// least 1.
inline constexpr std::uint32_t kAutoWorkerCount = std::numeric_limits<std::uint32_t>::max();

/// Creation parameters of a JobSystem.
struct JobSystemDesc {
    /// Number of worker threads. kAutoWorkerCount: see its comment. 0: inline mode, no worker
    /// threads (jobs run in wait, parallelFor and runPending, in submission order).
    std::uint32_t workerCount = kAutoWorkerCount;
    /// Capacity of the shared queue, a power of two. Submitting to a full queue runs the job on
    /// the submitting thread instead (counted in JobStats::inlineOverflows).
    std::uint32_t queueCapacity = 4096;
    /// Optional, called once on each worker thread before it takes jobs (e.g. to name the thread
    /// or register it with a profiler; OS calls live outside foundation).
    std::function<void(std::uint32_t workerIndex)> onWorkerStart{};
};

/// Counters for the debug overlay. Totals are monotonic since creation.
struct JobStats {
    std::uint64_t submitted = 0;
    std::uint64_t executed = 0;
    std::uint64_t inlineOverflows = 0; ///< Jobs run inline because the queue was full.
    std::uint32_t queued = 0;          ///< Snapshot of the queue depth.
    std::uint32_t maxQueued = 0;       ///< High-water mark since creation.
};

/// Fixed pool of worker threads sharing one bounded queue.
///
/// Thread-safety: create and destroy on one thread (the owner). submit, wait, parallelFor,
/// runPending and stats may be called from any thread, including workers and the render
/// thread (keep waits on the render thread short: they may stall a frame).
/// Allocation: create() allocates the queue and threads; nothing else allocates.
class JobSystem {
public:
    /// Fails with kInvalidArgument when queueCapacity is zero or not a power of two, and with
    /// kInternal when a worker thread cannot be started (started threads are joined).
    [[nodiscard]] static Result<std::unique_ptr<JobSystem>> create(const JobSystemDesc& desc);

    /// Runs every job still queued (including jobs they submit), then joins the workers.
    /// Submitting from outside those jobs while the destructor runs is a bug; it asserts once
    /// the queue is closed.
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&) = delete;
    JobSystem& operator=(JobSystem&&) = delete;

    /// Number of worker threads (0 in inline mode).
    [[nodiscard]] std::uint32_t workerCount() const noexcept;

    /// Queues `job` (asserts it is not empty); it counts towards `group` until it finished
    /// running. If the queue is full, runs it on the calling thread before returning.
    void submit(JobGroup& group, Job job);

    /// Returns when every job of `group` finished. Executes queued jobs (of any group) while
    /// waiting, then blocks without spinning when the queue is empty.
    void wait(JobGroup& group);

    /// Calls body(begin, end) for consecutive chunks of [0, count) of at most `grain` indices
    /// (a grain of 0 is treated as 1), in parallel, and returns when all chunks ran. The calling
    /// thread executes chunks too. `body` must be safe to call concurrently. Does not allocate.
    template <typename F>
        requires std::invocable<F&, std::size_t, std::size_t>
    void parallelFor(std::size_t count, std::size_t grain, F&& body);

    /// Runs, on the calling thread, at most as many jobs as were queued at the time of the
    /// call. Needed once per frame in inline mode so fire-and-poll work progresses; a cheap
    /// no-op when workers keep the queue empty.
    void runPending();

    /// Snapshot of the counters; the fields are read independently (not one atomic snapshot).
    [[nodiscard]] JobStats stats() const noexcept;

private:
    struct Impl;
    explicit JobSystem(std::unique_ptr<Impl> impl) noexcept;

    void workerLoop(std::uint32_t workerIndex);
    void execute(Job job, JobGroup& group) noexcept;
    void stopWorkers() noexcept;

    std::unique_ptr<Impl> m_impl;
};

template <typename F>
    requires std::invocable<F&, std::size_t, std::size_t>
// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward): body is called many times, never moved.
void JobSystem::parallelFor(std::size_t count, std::size_t grain, F&& body) {
    if (count == 0) {
        return;
    }
    grain = std::max<std::size_t>(grain, 1);
    const std::size_t chunks =
        (count / grain) + (count % grain != 0 ? std::size_t{1} : std::size_t{0});
    if (chunks == 1 || workerCount() == 0) {
        for (std::size_t begin = 0; begin < count; begin += grain) {
            body(begin, std::min(begin + grain, count));
        }
        return;
    }

    struct State {
        std::remove_reference_t<F>* callable = nullptr;
        std::size_t total = 0;
        std::size_t chunkSize = 1;
        std::size_t chunkCount = 0;
        // relaxed: only claims chunk indices; results are published through the group.
        std::atomic<std::size_t> nextChunk{0};

        void run() {
            for (;;) {
                const std::size_t chunk = nextChunk.fetch_add(1, std::memory_order_relaxed);
                if (chunk >= chunkCount) {
                    return;
                }
                const std::size_t begin = chunk * chunkSize;
                (*callable)(begin, std::min(begin + chunkSize, total));
            }
        }
    };
    State state{
        .callable = std::addressof(body), .total = count, .chunkSize = grain, .chunkCount = chunks};

    JobGroup group;
    const std::size_t helpers = std::min<std::size_t>(workerCount(), chunks - 1);
    for (std::size_t i = 0; i < helpers; ++i) {
        submit(group, [&state] { state.run(); });
    }
    state.run();
    wait(group);
}

} // namespace ezmu::foundation

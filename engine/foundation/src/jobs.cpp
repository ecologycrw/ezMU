#include <ezmu/foundation/jobs.hpp>

#include <bit>
#include <condition_variable>
#include <format>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

// Design (docs/specs/jobs.md, "Implementation notes"): one bounded ring buffer guarded by a
// mutex, and one condition variable shared by sleeping workers and by threads blocked in wait().
// The condition variable (not std::atomic::wait on the group) also signals group completion,
// because a waiter may destroy its group as soon as the count reaches zero; the completing
// thread therefore never touches the group after the final decrement.

namespace ezmu::foundation {

namespace {

[[nodiscard]] std::uint32_t resolveWorkerCount(std::uint32_t requested) noexcept {
    if (requested != kAutoWorkerCount) {
        return requested;
    }
    // Main and render threads are dedicated (ADR 0012); hardware_concurrency() may return 0.
    const unsigned hardware = std::thread::hardware_concurrency();
    return hardware > 2 ? hardware - 2 : 1;
}

struct Entry {
    Job job{};
    JobGroup* group = nullptr;
};

} // namespace

struct JobSystem::Impl {
    explicit Impl(const JobSystemDesc& desc)
        : ring(desc.queueCapacity), mask(desc.queueCapacity - 1),
          onWorkerStart(desc.onWorkerStart) {}

    // --- Guarded by `mutex` -------------------------------------------------------------
    [[nodiscard]] std::uint64_t size() const noexcept { return tail - head; }
    [[nodiscard]] bool empty() const noexcept { return head == tail; }
    [[nodiscard]] bool full() const noexcept { return size() == ring.size(); }

    [[nodiscard]] Entry& slot(std::uint64_t position) noexcept {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        return ring[static_cast<std::size_t>(position & mask)]; // Masked: always in bounds.
    }

    void push(Job job, JobGroup& group) noexcept {
        Entry& entry = slot(tail);
        entry.job = std::move(job);
        entry.group = &group;
        ++tail;
        publishDepth();
    }

    [[nodiscard]] Entry pop() noexcept {
        Entry& source = slot(head);
        Entry entry{.job = std::move(source.job), .group = std::exchange(source.group, nullptr)};
        ++head;
        publishDepth();
        return entry;
    }

    void publishDepth() noexcept {
        const auto depth = static_cast<std::uint32_t>(size());
        // relaxed: statistics only (written under the mutex, read lock-free by stats()).
        queued.store(depth, std::memory_order_relaxed);
        if (depth > maxQueued.load(std::memory_order_relaxed)) {
            maxQueued.store(depth, std::memory_order_relaxed);
        }
    }

    std::vector<Entry> ring;
    std::uint64_t mask = 0;
    std::uint64_t head = 0; ///< Next entry to pop.
    std::uint64_t tail = 0; ///< Next free entry.
    bool stopping = false;  ///< Workers exit once the queue is empty.
    bool closed = false;    ///< Destructor drained the queue; submitting is a bug.

    std::mutex mutex;
    /// Signalled when a job is queued, when a group completes and when stopping.
    std::condition_variable wakeUp;

    // --- Immutable after create() -----------------------------------------------------
    std::function<void(std::uint32_t)> onWorkerStart;
    std::uint32_t workerCount = 0;
    std::vector<std::jthread> workers;

    // --- Statistics (relaxed: counters only, never used for synchronization) ----------
    std::atomic<std::uint64_t> submitted{0};
    std::atomic<std::uint64_t> executed{0};
    std::atomic<std::uint64_t> inlineOverflows{0};
    std::atomic<std::uint32_t> queued{0};
    std::atomic<std::uint32_t> maxQueued{0};
};

// NOLINTNEXTLINE(bugprone-exception-escape): wait() throws only on mutex misuse; terminate then.
JobGroup::~JobGroup() {
    EZMU_ASSERT_MSG(isDone(), "JobGroup destroyed while it still has unfinished jobs");
    if (!isDone()) {
        // relaxed: the owner thread already synchronized with the submit (it submitted itself,
        // or learned about the group from the submitter).
        if (JobSystem* system = m_system.load(std::memory_order_relaxed); system != nullptr) {
            system->wait(*this);
        }
    }
}

JobSystem::JobSystem(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {}

Result<std::unique_ptr<JobSystem>> JobSystem::create(const JobSystemDesc& desc) {
    if (desc.queueCapacity == 0 || !std::has_single_bit(desc.queueCapacity)) {
        return makeError(
            ErrorCode::kInvalidArgument,
            std::format("job queue capacity must be a power of two (got {})", desc.queueCapacity));
    }

    // NOLINTNEXTLINE(modernize-make-unique): the constructor is private.
    std::unique_ptr<JobSystem> system(new JobSystem(std::make_unique<Impl>(desc)));
    Impl& impl = *system->m_impl;
    impl.workerCount = resolveWorkerCount(desc.workerCount);
    impl.workers.reserve(impl.workerCount);
    try {
        for (std::uint32_t index = 0; index < impl.workerCount; ++index) {
            impl.workers.emplace_back([self = system.get(), index] { self->workerLoop(index); });
        }
    } catch (const std::system_error& error) {
        // std::thread reports failure only through exceptions; convert at this boundary.
        const auto started = impl.workers.size();
        system->stopWorkers();
        impl.workerCount = 0;
        return makeError(ErrorCode::kInternal, std::format("cannot start job worker thread {}: {}",
                                                           started, error.what()));
    }
    return system;
}

JobSystem::~JobSystem() {
    // Workers drain the queue (including jobs submitted by running jobs) before exiting.
    stopWorkers();

    // Inline mode, or nothing left: run the rest here, then close the queue.
    for (;;) {
        Entry entry;
        {
            const std::scoped_lock lock(m_impl->mutex);
            if (m_impl->empty()) {
                m_impl->closed = true;
                return;
            }
            entry = m_impl->pop();
        }
        execute(std::move(entry.job), *entry.group);
    }
}

std::uint32_t JobSystem::workerCount() const noexcept {
    return m_impl->workerCount;
}

void JobSystem::submit(JobGroup& group, Job job) {
    EZMU_ASSERT_MSG(!job.isEmpty(), "submitting an empty Job");

    // relaxed: only read by the group's destructor, on a thread ordered after this submit.
    const JobSystem* previous = group.m_system.exchange(this, std::memory_order_relaxed);
    [[maybe_unused]] const bool sameSystem = previous == nullptr || previous == this;
    EZMU_ASSERT_MSG(sameSystem, "a JobGroup is used with two different JobSystems");
    // relaxed: the queue mutex (or running the job inline below) orders this increment before
    // the job's acq_rel decrement.
    group.m_pending.fetch_add(1, std::memory_order_relaxed);
    m_impl->submitted.fetch_add(1, std::memory_order_relaxed);

    {
        std::unique_lock lock(m_impl->mutex);
        EZMU_ASSERT_MSG(!m_impl->closed, "submitting to a JobSystem that is being destroyed");
        if (m_impl->full()) {
            // Decision J5: never block, never allocate; run it here.
            lock.unlock();
            m_impl->inlineOverflows.fetch_add(1, std::memory_order_relaxed);
            execute(std::move(job), group);
            return;
        }
        m_impl->push(std::move(job), group);
    }
    m_impl->wakeUp.notify_one();
}

void JobSystem::wait(JobGroup& group) {
    if (group.isDone()) {
        return;
    }
    std::unique_lock lock(m_impl->mutex);
    for (;;) {
        m_impl->wakeUp.wait(lock, [&] { return group.isDone() || !m_impl->empty(); });
        if (group.isDone()) {
            return;
        }
        Entry entry = m_impl->pop();
        lock.unlock();
        execute(std::move(entry.job), *entry.group);
        lock.lock();
    }
}

void JobSystem::runPending() {
    // relaxed: a hint only; a job queued concurrently is picked up by a later call or a worker.
    if (m_impl->queued.load(std::memory_order_relaxed) == 0) {
        return;
    }
    std::uint64_t remaining = 0;
    {
        const std::scoped_lock lock(m_impl->mutex);
        remaining = m_impl->size();
    }
    for (; remaining > 0; --remaining) {
        Entry entry;
        {
            const std::scoped_lock lock(m_impl->mutex);
            if (m_impl->empty()) {
                return; // Workers or other threads took the rest.
            }
            entry = m_impl->pop();
        }
        execute(std::move(entry.job), *entry.group);
    }
}

JobStats JobSystem::stats() const noexcept {
    // relaxed: independent counters for display; no cross-field consistency is promised.
    return JobStats{
        .submitted = m_impl->submitted.load(std::memory_order_relaxed),
        .executed = m_impl->executed.load(std::memory_order_relaxed),
        .inlineOverflows = m_impl->inlineOverflows.load(std::memory_order_relaxed),
        .queued = m_impl->queued.load(std::memory_order_relaxed),
        .maxQueued = m_impl->maxQueued.load(std::memory_order_relaxed),
    };
}

void JobSystem::workerLoop(std::uint32_t workerIndex) {
    if (m_impl->onWorkerStart) {
        m_impl->onWorkerStart(workerIndex);
    }
    std::unique_lock lock(m_impl->mutex);
    for (;;) {
        m_impl->wakeUp.wait(lock, [&] { return m_impl->stopping || !m_impl->empty(); });
        if (m_impl->empty()) {
            return; // Stopping and drained.
        }
        Entry entry = m_impl->pop();
        lock.unlock();
        execute(std::move(entry.job), *entry.group);
        lock.lock();
    }
}

void JobSystem::execute(Job job, JobGroup& group) noexcept {
    job();
    // Destroy the callable before signalling: once the group is done its owner may free
    // anything the callable captured.
    job = Job{};
    m_impl->executed.fetch_add(1, std::memory_order_relaxed);

    // acq_rel: release publishes the job's effects to isDone()/wait() (acquire); acquire
    // orders this thread after other jobs of the group when it observes the final count.
    if (group.m_pending.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        // The group may be destroyed from here on; do not touch it. Taking the mutex orders
        // this notification after any waiter that evaluated its predicate before the decrement.
        {
            const std::scoped_lock lock(m_impl->mutex);
        }
        m_impl->wakeUp.notify_all();
    }
}

void JobSystem::stopWorkers() noexcept {
    {
        const std::scoped_lock lock(m_impl->mutex);
        m_impl->stopping = true;
    }
    m_impl->wakeUp.notify_all();
    m_impl->workers.clear(); // std::jthread joins.
}

} // namespace ezmu::foundation

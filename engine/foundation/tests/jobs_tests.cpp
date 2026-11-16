#include <ezmu/foundation/assert.hpp>
#include <ezmu/foundation/jobs.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <latch>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>

// Synchronization uses std::latch and the job API itself; no wall-clock sleeps (testing rules).

namespace {

using ezmu::foundation::ErrorCode;
using ezmu::foundation::Job;
using ezmu::foundation::JobCallable;
using ezmu::foundation::JobGroup;
using ezmu::foundation::JobSystem;
using ezmu::foundation::JobSystemDesc;

std::unique_ptr<JobSystem> makeJobs(std::uint32_t workers, std::uint32_t capacity = 4096) {
    auto jobs = JobSystem::create({.workerCount = workers, .queueCapacity = capacity});
    REQUIRE(jobs.has_value());
    return std::move(*jobs);
}

std::atomic<int> g_assertFailures{0};

void countingHandler(const ezmu::foundation::AssertionInfo& /*info*/) {
    g_assertFailures.fetch_add(1);
}

/// Installs the counting assertion handler for the lifetime of the object.
class ScopedAssertCapture {
public:
    ScopedAssertCapture() : m_previous(ezmu::foundation::setAssertionHandler(&countingHandler)) {
        g_assertFailures = 0;
    }
    ~ScopedAssertCapture() { ezmu::foundation::setAssertionHandler(m_previous); }

    ScopedAssertCapture(const ScopedAssertCapture&) = delete;
    ScopedAssertCapture& operator=(const ScopedAssertCapture&) = delete;
    ScopedAssertCapture(ScopedAssertCapture&&) = delete;
    ScopedAssertCapture& operator=(ScopedAssertCapture&&) = delete;

private:
    ezmu::foundation::AssertionHandler m_previous;
};

/// Counts destructions of live (not moved-from) instances.
struct DestructionCounter {
    explicit DestructionCounter(int& target) noexcept : count(&target) {}
    DestructionCounter(DestructionCounter&& other) noexcept
        : count(std::exchange(other.count, nullptr)) {}
    DestructionCounter& operator=(DestructionCounter&&) = delete;
    DestructionCounter(const DestructionCounter&) = delete;
    DestructionCounter& operator=(const DestructionCounter&) = delete;
    ~DestructionCounter() {
        if (count != nullptr) {
            ++*count;
        }
    }
    int* count;
};

struct ThrowingMove {
    ThrowingMove() = default;
    ThrowingMove(ThrowingMove&&) noexcept(false) {}
    void operator()() const {}
};

} // namespace

TEST_CASE("Job accepts small callables and rejects the rest at compile time", "[jobs]") {
    auto small = [pointer = static_cast<int*>(nullptr)] { static_cast<void>(pointer); };
    auto exact = [data = std::array<std::byte, ezmu::foundation::kJobStorageSize>{}] {
        static_cast<void>(data);
    };
    auto oversized = [data = std::array<std::byte, ezmu::foundation::kJobStorageSize + 1>{}] {
        static_cast<void>(data);
    };
    auto withArguments = [](int value) { static_cast<void>(value); };

    STATIC_CHECK(JobCallable<decltype(small)>);
    STATIC_CHECK(JobCallable<decltype(exact)>);
    STATIC_CHECK_FALSE(JobCallable<decltype(oversized)>);
    STATIC_CHECK_FALSE(JobCallable<decltype(withArguments)>);
    STATIC_CHECK_FALSE(JobCallable<ThrowingMove>);
    STATIC_CHECK_FALSE(JobCallable<Job>);
    STATIC_CHECK_FALSE(std::is_copy_constructible_v<Job>);
    STATIC_CHECK(std::is_nothrow_move_constructible_v<Job>);
}

TEST_CASE("Job runs its callable and destroys it exactly once across moves", "[jobs]") {
    int runs = 0;
    int destructions = 0;
    {
        Job job([counter = DestructionCounter(destructions), &runs] { ++runs; });
        CHECK_FALSE(job.isEmpty());
        Job moved(std::move(job));
        CHECK(job.isEmpty()); // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
        Job assigned;
        CHECK(assigned.isEmpty());
        assigned = std::move(moved);
        assigned();
        assigned();
        CHECK(runs == 2);
        CHECK(destructions == 0);
    }
    CHECK(destructions == 1);
}

TEST_CASE("JobSystem rejects a queue capacity that is not a power of two", "[jobs]") {
    const std::uint32_t capacity = GENERATE(0U, 3U, 100U);
    auto jobs = JobSystem::create({.workerCount = 0, .queueCapacity = capacity});
    REQUIRE_FALSE(jobs.has_value());
    CHECK(jobs.error().code() == ErrorCode::kInvalidArgument);
}

TEST_CASE("JobSystem derives at least one worker by default", "[jobs]") {
    auto jobs = JobSystem::create({});
    REQUIRE(jobs.has_value());
    CHECK((*jobs)->workerCount() >= 1);
}

TEST_CASE("Every submitted job runs exactly once", "[jobs]") {
    const std::uint32_t workers = GENERATE(0U, 1U, 2U, 4U, 8U);
    CAPTURE(workers);
    auto jobs = makeJobs(workers);
    CHECK(jobs->workerCount() == workers);

    constexpr std::size_t kJobCount = 10'000;
    std::vector<std::atomic<int>> runs(kJobCount);
    JobGroup group;
    for (std::size_t i = 0; i < kJobCount; ++i) {
        jobs->submit(group, [&runs, i] { runs[i].fetch_add(1, std::memory_order_relaxed); });
    }
    jobs->wait(group);

    CHECK(group.isDone());
    std::size_t exactlyOnce = 0;
    for (const auto& count : runs) {
        exactlyOnce += count.load() == 1 ? std::size_t{1} : std::size_t{0};
    }
    CHECK(exactlyOnce == kJobCount);
    const auto stats = jobs->stats();
    CHECK(stats.submitted == kJobCount);
    CHECK(stats.executed == kJobCount);
}

TEST_CASE("Inline mode runs jobs in submission order on the waiting thread", "[jobs]") {
    auto jobs = makeJobs(0);
    std::vector<int> order;
    std::vector<std::thread::id> threads;
    JobGroup group;
    for (int i = 0; i < 100; ++i) {
        jobs->submit(group, [&order, &threads, i] {
            order.push_back(i);
            threads.push_back(std::this_thread::get_id());
        });
    }
    CHECK(order.empty());
    CHECK_FALSE(group.isDone());
    CHECK(jobs->stats().queued == 100);

    jobs->wait(group);

    REQUIRE(order.size() == 100);
    for (int i = 0; i < 100; ++i) {
        CHECK(order.at(static_cast<std::size_t>(i)) == i);
    }
    for (const auto& id : threads) {
        CHECK(id == std::this_thread::get_id());
    }
}

TEST_CASE("runPending runs only the jobs queued when it is called", "[jobs]") {
    auto jobs = makeJobs(0);
    JobGroup group;
    int first = 0;
    int nested = 0;
    jobs->submit(group, [&jobs, &group, &first, &nested] {
        ++first;
        jobs->submit(group, [&nested] { ++nested; });
    });

    jobs->runPending();
    CHECK(first == 1);
    CHECK(nested == 0);
    CHECK_FALSE(group.isDone());

    jobs->runPending();
    CHECK(nested == 1);
    CHECK(group.isDone());

    jobs->runPending(); // Empty queue: no-op.
}

TEST_CASE("wait executes queued jobs while the workers are busy", "[jobs]") {
    auto jobs = makeJobs(1);
    std::latch started(1);
    std::latch release(1);
    JobGroup blocked;
    jobs->submit(blocked, [&started, &release] {
        started.count_down();
        release.wait();
    });
    started.wait(); // The only worker is now inside the blocking job.

    std::vector<std::thread::id> threads;
    JobGroup group;
    for (int i = 0; i < 16; ++i) {
        jobs->submit(group, [&threads] { threads.push_back(std::this_thread::get_id()); });
    }
    jobs->wait(group); // Must help: the worker cannot run anything.

    CHECK(threads.size() == 16);
    for (const auto& id : threads) {
        CHECK(id == std::this_thread::get_id());
    }
    CHECK_FALSE(blocked.isDone());
    release.count_down();
    jobs->wait(blocked);
}

TEST_CASE("Jobs that submit and wait for sub-jobs do not deadlock", "[jobs]") {
    const std::uint32_t workers = GENERATE(0U, 1U, 2U);
    CAPTURE(workers);
    auto jobs = makeJobs(workers);
    std::atomic<int> leaves{0};

    JobGroup outer;
    for (int i = 0; i < 8; ++i) {
        jobs->submit(outer, [&jobs, &leaves] {
            JobGroup inner;
            for (int j = 0; j < 8; ++j) {
                jobs->submit(inner, [&leaves] { leaves.fetch_add(1); });
            }
            jobs->wait(inner);
        });
    }
    jobs->wait(outer);
    CHECK(leaves.load() == 64);
}

TEST_CASE("parallelFor visits every index exactly once", "[jobs]") {
    const std::uint32_t workers = GENERATE(0U, 1U, 4U);
    const auto [count, grain] = GENERATE(table<std::size_t, std::size_t>({
        {0, 4},     // empty range
        {1, 4},     // single index
        {3, 8},     // count < grain
        {10, 3},    // count not a multiple of grain
        {17, 1},    // grain 1
        {5, 0},     // grain 0 is treated as 1
        {1000, 64}, // many chunks
    }));
    CAPTURE(workers, count, grain);
    auto jobs = makeJobs(workers);

    std::vector<std::atomic<int>> visits(count);
    std::atomic<bool> chunkTooLarge{false};
    const std::size_t maxChunk = grain == 0 ? 1 : grain;
    jobs->parallelFor(count, grain, [&](std::size_t begin, std::size_t end) {
        if (begin >= end || end - begin > maxChunk || end > count) {
            chunkTooLarge.store(true);
            return;
        }
        for (std::size_t i = begin; i < end; ++i) {
            visits[i].fetch_add(1, std::memory_order_relaxed);
        }
    });

    CHECK_FALSE(chunkTooLarge.load());
    std::size_t exactlyOnce = 0;
    for (const auto& visit : visits) {
        exactlyOnce += visit.load() == 1 ? std::size_t{1} : std::size_t{0};
    }
    CHECK(exactlyOnce == count);
}

TEST_CASE("A full queue runs the job on the submitting thread", "[jobs]") {
    auto jobs = makeJobs(0, 4);
    JobGroup group;
    int runs = 0;
    for (int i = 0; i < 6; ++i) {
        jobs->submit(group, [&runs] { ++runs; });
    }
    CHECK(runs == 2); // Jobs 5 and 6 found the queue full.
    auto stats = jobs->stats();
    CHECK(stats.inlineOverflows == 2);
    CHECK(stats.queued == 4);
    CHECK(stats.maxQueued == 4);

    jobs->wait(group);
    CHECK(runs == 6);
    stats = jobs->stats();
    CHECK(stats.queued == 0);
    CHECK(stats.maxQueued == 4);
    CHECK(stats.executed == 6);
}

TEST_CASE("Destroying the job system runs the jobs still queued", "[jobs]") {
    const std::uint32_t workers = GENERATE(0U, 2U);
    CAPTURE(workers);
    std::atomic<int> runs{0};
    JobGroup group; // Outlives the job system.
    {
        auto jobs = makeJobs(workers);
        JobSystem* system = jobs.get(); // Jobs still run while the unique_ptr is being reset.
        for (int i = 0; i < 32; ++i) {
            jobs->submit(group, [system, &group, &runs] {
                runs.fetch_add(1);
                system->submit(group, [&runs] { runs.fetch_add(1); }); // Nested submit.
            });
        }
    }
    CHECK(runs.load() == 64);
    CHECK(group.isDone());
}

TEST_CASE("Destroying an unfinished JobGroup asserts and waits for its jobs", "[jobs]") {
    if constexpr (!ezmu::foundation::kAssertsEnabled) {
        SKIP("assertions are compiled out in this build");
    }
    auto jobs = makeJobs(0);
    int runs = 0;
    {
        const ScopedAssertCapture capture;
        {
            JobGroup group;
            jobs->submit(group, [&runs] { ++runs; });
        }
        CHECK(g_assertFailures.load() == 1);
    }
    CHECK(runs == 1);
}

TEST_CASE("onWorkerStart runs once on every worker", "[jobs]") {
    constexpr std::uint32_t kWorkers = 3;
    std::latch allStarted(kWorkers);
    std::mutex mutex;
    std::set<std::uint32_t> indices;
    std::set<std::thread::id> threads;

    auto jobs = JobSystem::create(JobSystemDesc{
        .workerCount = kWorkers,
        .onWorkerStart =
            [&](std::uint32_t index) {
                {
                    const std::scoped_lock lock(mutex);
                    indices.insert(index);
                    threads.insert(std::this_thread::get_id());
                }
                allStarted.count_down();
            },
    });
    REQUIRE(jobs.has_value());
    allStarted.wait();

    const std::scoped_lock lock(mutex);
    CHECK(indices == std::set<std::uint32_t>{0, 1, 2});
    CHECK(threads.size() == kWorkers);
    CHECK_FALSE(threads.contains(std::this_thread::get_id()));
}

TEST_CASE("Many threads submitting tiny jobs into their own groups", "[jobs][stress]") {
    constexpr int kSubmitters = 4;
    constexpr int kJobsPerSubmitter = 25'000;
    // A small queue also exercises the inline overflow path under contention.
    auto jobs = makeJobs(4, 256);
    std::atomic<int> runs{0};
    std::latch start(kSubmitters);
    {
        std::vector<std::jthread> submitters;
        submitters.reserve(kSubmitters);
        for (int s = 0; s < kSubmitters; ++s) {
            submitters.emplace_back([&jobs, &runs, &start] {
                start.arrive_and_wait();
                JobGroup group;
                for (int i = 0; i < kJobsPerSubmitter; ++i) {
                    jobs->submit(group, [&runs] { runs.fetch_add(1, std::memory_order_relaxed); });
                }
                jobs->wait(group);
            });
        }
    }
    constexpr auto kTotal = static_cast<std::uint64_t>(kSubmitters) * kJobsPerSubmitter;
    CHECK(runs.load() == static_cast<int>(kTotal));
    const auto stats = jobs->stats();
    CHECK(stats.submitted == kTotal);
    CHECK(stats.executed == kTotal);
}

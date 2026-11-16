#include <ezmu/renderer/snapshot.hpp>
#include <ezmu/renderer/snapshot_ring.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <latch>
#include <stop_token>
#include <thread>
#include <vector>

// Synchronization uses std::latch and the ring itself; no wall-clock sleeps (testing rules).

namespace {

using ezmu::renderer::RenderSnapshot;
using ezmu::renderer::SnapshotRing;

/// Writes one snapshot with `frameIndex`; returns false when the ring refused.
bool writeFrame(SnapshotRing& ring, std::uint64_t frameIndex, const std::stop_token& stop = {}) {
    RenderSnapshot* snapshot = ring.beginWrite(stop);
    if (snapshot == nullptr) {
        return false;
    }
    snapshot->frameIndex = frameIndex;
    ring.endWrite();
    return true;
}

} // namespace

TEST_CASE("SnapshotRing delivers snapshots in FIFO order across threads", "[snapshot_ring]") {
    constexpr std::uint64_t kFrames = 10'000;
    const std::uint32_t slots = GENERATE(2U, 3U, 4U);
    CAPTURE(slots);
    SnapshotRing ring(slots);
    CHECK(ring.slotCount() == slots);

    std::jthread producer([&ring] {
        for (std::uint64_t i = 1; i <= kFrames; ++i) {
            if (!writeFrame(ring, i)) {
                return;
            }
        }
        ring.close();
    });

    std::vector<std::uint64_t> received;
    received.reserve(kFrames);
    while (const RenderSnapshot* snapshot = ring.beginRead({})) {
        received.push_back(snapshot->frameIndex);
        ring.endRead();
    }
    producer.join();

    REQUIRE(received.size() == kFrames);
    std::uint64_t inOrder = 0;
    for (std::uint64_t i = 0; i < kFrames; ++i) {
        inOrder += received.at(i) == i + 1 ? 1U : 0U;
    }
    CHECK(inOrder == kFrames);
}

TEST_CASE("SnapshotRing blocks the producer while every slot is published", "[snapshot_ring]") {
    SnapshotRing ring(2);
    REQUIRE(writeFrame(ring, 1));
    REQUIRE(writeFrame(ring, 2)); // Both slots published, none read.

    std::latch aboutToWrite(1);
    std::atomic<bool> released{false};
    bool sawRelease = false;
    std::jthread producer([&] {
        aboutToWrite.count_down();
        RenderSnapshot* snapshot = ring.beginWrite({});
        // Only endRead() frees a slot, so it must have happened before beginWrite() returned.
        sawRelease = released.load();
        if (snapshot != nullptr) {
            snapshot->frameIndex = 3;
            ring.endWrite();
        }
    });

    aboutToWrite.wait();
    const RenderSnapshot* first = ring.beginRead({});
    REQUIRE(first != nullptr);
    CHECK(first->frameIndex == 1);
    released.store(true);
    ring.endRead();
    producer.join();

    CHECK(sawRelease);
    for (const std::uint64_t expected : {2U, 3U}) {
        const RenderSnapshot* snapshot = ring.beginRead({});
        REQUIRE(snapshot != nullptr);
        CHECK(snapshot->frameIndex == expected);
        ring.endRead();
    }
}

TEST_CASE("SnapshotRing close wakes a blocked producer", "[snapshot_ring]") {
    SnapshotRing ring(2);
    REQUIRE(writeFrame(ring, 1));
    REQUIRE(writeFrame(ring, 2));
    std::latch aboutToWrite(1);
    bool written = true;
    std::jthread producer([&] {
        aboutToWrite.count_down();
        written = writeFrame(ring, 3);
    });
    aboutToWrite.wait();
    ring.close();
    producer.join();
    CHECK_FALSE(written);
    CHECK(ring.isClosed());
}

TEST_CASE("SnapshotRing close wakes a blocked consumer", "[snapshot_ring]") {
    SnapshotRing ring(2);
    std::latch aboutToRead(1);
    bool gotSnapshot = true;
    std::jthread consumer([&] {
        aboutToRead.count_down();
        gotSnapshot = ring.beginRead({}) != nullptr;
    });
    aboutToRead.wait();
    ring.close();
    consumer.join();
    CHECK_FALSE(gotSnapshot);
}

TEST_CASE("A closed SnapshotRing still delivers the published snapshots", "[snapshot_ring]") {
    SnapshotRing ring(3);
    REQUIRE(writeFrame(ring, 1));
    REQUIRE(writeFrame(ring, 2));
    ring.close();
    CHECK_FALSE(writeFrame(ring, 3));
    for (const std::uint64_t expected : {1U, 2U}) {
        const RenderSnapshot* snapshot = ring.beginRead({});
        REQUIRE(snapshot != nullptr);
        CHECK(snapshot->frameIndex == expected);
        ring.endRead();
    }
    CHECK(ring.beginRead({}) == nullptr);
}

TEST_CASE("SnapshotRing returns nullptr when stop is requested", "[snapshot_ring]") {
    SECTION("blocked producer") {
        SnapshotRing ring(2);
        REQUIRE(writeFrame(ring, 1));
        REQUIRE(writeFrame(ring, 2));
        std::latch aboutToWrite(1);
        bool written = true;
        std::jthread producer([&](const std::stop_token& stop) {
            aboutToWrite.count_down();
            written = writeFrame(ring, 3, stop);
        });
        aboutToWrite.wait();
        producer.request_stop();
        producer.join();
        CHECK_FALSE(written);
        CHECK_FALSE(ring.isClosed());
    }
    SECTION("blocked consumer") {
        SnapshotRing ring(2);
        std::latch aboutToRead(1);
        bool gotSnapshot = true;
        std::jthread consumer([&](const std::stop_token& stop) {
            aboutToRead.count_down();
            gotSnapshot = ring.beginRead(stop) != nullptr;
        });
        aboutToRead.wait();
        consumer.request_stop();
        consumer.join();
        CHECK_FALSE(gotSnapshot);
    }
}

TEST_CASE("SnapshotRing hands out reset slots", "[snapshot_ring]") {
    SnapshotRing ring(2);
    for (std::uint64_t lap = 1; lap <= 4; ++lap) {
        RenderSnapshot* snapshot = ring.beginWrite({});
        REQUIRE(snapshot != nullptr);
        CHECK(snapshot->frameIndex == 0);
        CHECK(snapshot->captureId == 0);
        CHECK(snapshot->output == ezmu::renderer::OutputState{});
        CHECK(snapshot->clearColor == std::array<float, 4>{0.0F, 0.0F, 0.0F, 1.0F});
        snapshot->frameIndex = lap;
        snapshot->captureId = lap;
        snapshot->output.pixelSize = {.width = 8, .height = 8};
        snapshot->clearColor = {1.0F, 1.0F, 1.0F, 1.0F};
        ring.endWrite();

        const RenderSnapshot* read = ring.beginRead({});
        REQUIRE(read != nullptr);
        CHECK(read->captureId == lap);
        ring.endRead();
    }
}

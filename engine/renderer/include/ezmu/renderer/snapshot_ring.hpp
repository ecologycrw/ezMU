#pragma once

#include <ezmu/renderer/snapshot.hpp>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <vector>

namespace ezmu::renderer {

/// Bounded FIFO of snapshot slots between exactly one producer thread (game) and one consumer
/// thread (render). With 2 slots the game thread is at most one frame ahead of the render
/// thread: while the render thread reads snapshot N, the game thread writes N+1, and it blocks
/// in beginWrite() until N is released (back-pressure: the game loop is paced by the render
/// thread and, through it, by vsync).
///
/// Snapshot contents are written only between beginWrite() and endWrite() and read only between
/// beginRead() and endRead(); the ring's mutex orders the two.
/// Thread-safety: *Write from the producer thread, *Read from the consumer thread, close() and
/// the accessors from any thread.
/// Allocation: the constructor allocates the slots; nothing else allocates.
class SnapshotRing {
public:
    static constexpr std::uint32_t kMinSlots = 2;
    static constexpr std::uint32_t kMaxSlots = 4;

    /// `slotCount` must be in [kMinSlots, kMaxSlots] (EZMU_ASSERT; clamped in release builds).
    explicit SnapshotRing(std::uint32_t slotCount = kMinSlots);

    SnapshotRing(const SnapshotRing&) = delete;
    SnapshotRing& operator=(const SnapshotRing&) = delete;
    SnapshotRing(SnapshotRing&&) = delete;
    SnapshotRing& operator=(SnapshotRing&&) = delete;
    ~SnapshotRing() = default;

    /// Producer: waits for a free slot and returns it reset(), or nullptr once closed or when
    /// `stop` is requested. A non-null result must be followed by endWrite() before the next
    /// beginWrite() (EZMU_ASSERT).
    [[nodiscard]] RenderSnapshot* beginWrite(const std::stop_token& stop);
    /// Producer: publishes the slot returned by beginWrite() to the consumer.
    void endWrite();

    /// Consumer: waits for the oldest published snapshot and returns it, or nullptr when `stop`
    /// is requested or the ring is closed and every published snapshot was read. A non-null
    /// result must be followed by endRead() before the next beginRead() (EZMU_ASSERT).
    [[nodiscard]] const RenderSnapshot* beginRead(const std::stop_token& stop);
    /// Consumer: returns the slot returned by beginRead() to the producer.
    void endRead();

    /// Wakes both sides; later beginWrite() calls return nullptr, and beginRead() returns
    /// nullptr once the published snapshots were read. Used on shutdown and when the render
    /// thread fails, so the game thread never blocks forever. Idempotent.
    void close();

    [[nodiscard]] bool isClosed() const;
    [[nodiscard]] std::uint32_t slotCount() const noexcept;

private:
    mutable std::mutex m_mutex;
    std::condition_variable_any m_changed; ///< Any state change below.
    std::vector<RenderSnapshot> m_slots;
    std::uint32_t m_readSlot = 0;  ///< Oldest published slot (being read, or next to read).
    std::uint32_t m_published = 0; ///< Published slots not yet released by endRead().
    bool m_writing = false;
    bool m_reading = false;
    bool m_closed = false;
};

} // namespace ezmu::renderer

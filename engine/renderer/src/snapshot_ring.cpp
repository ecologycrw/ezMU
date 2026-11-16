#include <ezmu/foundation/assert.hpp>
#include <ezmu/renderer/snapshot_ring.hpp>

#include <algorithm>

namespace ezmu::renderer {

SnapshotRing::SnapshotRing(std::uint32_t slotCount)
    : m_slots(std::clamp(slotCount, kMinSlots, kMaxSlots)) {
    [[maybe_unused]] const bool inRange = slotCount >= kMinSlots && slotCount <= kMaxSlots;
    EZMU_ASSERT_MSG(inRange, "SnapshotRing slot count must be in [2, 4]");
}

RenderSnapshot* SnapshotRing::beginWrite(const std::stop_token& stop) {
    RenderSnapshot* slot = nullptr;
    {
        std::unique_lock lock(m_mutex);
        EZMU_ASSERT_MSG(!m_writing, "beginWrite() without endWrite()");
        const auto slotCount = static_cast<std::uint32_t>(m_slots.size());
        const bool hasSlot =
            m_changed.wait(lock, stop, [&] { return m_closed || m_published < slotCount; });
        if (!hasSlot || m_closed) {
            return nullptr;
        }
        m_writing = true;
        slot = &m_slots.at((m_readSlot + m_published) % slotCount);
    }
    // The slot is free: the consumer does not touch it until endWrite() publishes it.
    slot->reset();
    return slot;
}

void SnapshotRing::endWrite() {
    {
        const std::scoped_lock lock(m_mutex);
        EZMU_ASSERT_MSG(m_writing, "endWrite() without beginWrite()");
        if (!m_writing) {
            return;
        }
        m_writing = false;
        ++m_published;
    }
    m_changed.notify_all();
}

const RenderSnapshot* SnapshotRing::beginRead(const std::stop_token& stop) {
    std::unique_lock lock(m_mutex);
    EZMU_ASSERT_MSG(!m_reading, "beginRead() without endRead()");
    const bool ready = m_changed.wait(lock, stop, [&] { return m_closed || m_published > 0; });
    if (!ready || m_published == 0) {
        return nullptr; // Stop requested, or closed and drained.
    }
    m_reading = true;
    return &m_slots.at(m_readSlot);
}

void SnapshotRing::endRead() {
    {
        const std::scoped_lock lock(m_mutex);
        EZMU_ASSERT_MSG(m_reading, "endRead() without beginRead()");
        if (!m_reading) {
            return;
        }
        m_reading = false;
        m_readSlot = (m_readSlot + 1) % static_cast<std::uint32_t>(m_slots.size());
        --m_published;
    }
    m_changed.notify_all();
}

void SnapshotRing::close() {
    {
        const std::scoped_lock lock(m_mutex);
        m_closed = true;
    }
    m_changed.notify_all();
}

bool SnapshotRing::isClosed() const {
    const std::scoped_lock lock(m_mutex);
    return m_closed;
}

std::uint32_t SnapshotRing::slotCount() const noexcept {
    return static_cast<std::uint32_t>(m_slots.size());
}

} // namespace ezmu::renderer

#pragma once

#include <ezmu/rhi/handles.hpp>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace ezmu::rhi::detail {

/// Slot storage behind generational handles. Destroyed slots are reused; each reuse bumps the
/// generation (never 0), so stale handles stop resolving. Not thread-safe (owned by Device).
template <typename Tag, typename T>
class HandlePool {
public:
    using HandleType = Handle<Tag>;

    /// Stores `value` and returns its handle. May allocate when no free slot exists.
    [[nodiscard]] HandleType insert(T value) {
        std::uint32_t index = 0;
        if (!m_free.empty()) {
            index = m_free.back();
            m_free.pop_back();
        } else {
            index = static_cast<std::uint32_t>(m_slots.size());
            m_slots.emplace_back();
        }
        Slot& slot = m_slots.at(index);
        slot.value = std::move(value);
        slot.alive = true;
        ++m_size;
        return HandleType(index, slot.generation);
    }

    /// The object of `handle`, or nullptr when the handle is invalid or stale.
    [[nodiscard]] T* get(HandleType handle) noexcept {
        if (!handle.isValid() || handle.index() >= m_slots.size()) {
            return nullptr;
        }
        Slot& slot = m_slots.at(handle.index());
        return slot.alive && slot.generation == handle.generation() ? &slot.value : nullptr;
    }

    /// Removes and returns the object of `handle` (nullopt when invalid or stale).
    [[nodiscard]] std::optional<T> remove(HandleType handle) {
        T* value = get(handle);
        if (value == nullptr) {
            return std::nullopt;
        }
        Slot& slot = m_slots.at(handle.index());
        std::optional<T> result(std::move(*value));
        slot.value = T{};
        slot.alive = false;
        slot.generation = slot.generation == UINT32_MAX ? 1 : slot.generation + 1;
        m_free.push_back(handle.index());
        --m_size;
        return result;
    }

    /// Calls `function(T&)` for every live object.
    template <typename Function>
    void forEach(const Function& function) {
        for (Slot& slot : m_slots) {
            if (slot.alive) {
                function(slot.value);
            }
        }
    }

    [[nodiscard]] std::uint32_t size() const noexcept { return m_size; }

private:
    struct Slot {
        T value{};
        std::uint32_t generation = 1;
        bool alive = false;
    };

    std::vector<Slot> m_slots;
    std::vector<std::uint32_t> m_free;
    std::uint32_t m_size = 0;
};

} // namespace ezmu::rhi::detail

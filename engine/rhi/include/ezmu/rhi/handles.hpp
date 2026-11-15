#pragma once

#include <cstdint>

// Generational handles for RHI resources. Spec: docs/specs/rhi.md.

namespace ezmu::rhi {

/// Reference to an object owned by a Device: a 32-bit slot index plus a 32-bit generation.
///
/// A default-constructed handle is invalid (generation 0). When an object is destroyed its slot's
/// generation changes, so an old handle no longer matches: using it is a programming error caught
/// by EZMU_ASSERT. Handles are plain values: trivially copyable, comparable, thread-agnostic.
/// Only the Device that created a handle may resolve it.
template <typename Tag>
class Handle {
public:
    constexpr Handle() noexcept = default;
    /// For the owning Device (and tests); games obtain handles from Device::create*().
    constexpr Handle(std::uint32_t index, std::uint32_t generation) noexcept
        : m_index(index), m_generation(generation) {}

    /// False for default-constructed handles. A valid handle may still be stale (destroyed).
    [[nodiscard]] constexpr bool isValid() const noexcept { return m_generation != 0; }
    [[nodiscard]] constexpr std::uint32_t index() const noexcept { return m_index; }
    [[nodiscard]] constexpr std::uint32_t generation() const noexcept { return m_generation; }

    friend constexpr bool operator==(const Handle&, const Handle&) noexcept = default;

private:
    std::uint32_t m_index = 0;
    std::uint32_t m_generation = 0;
};

using TextureHandle = Handle<struct TextureTag>;
using BufferHandle = Handle<struct BufferTag>;

} // namespace ezmu::rhi

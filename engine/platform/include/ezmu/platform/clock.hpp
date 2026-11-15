#pragma once

#include <chrono>

namespace ezmu::platform {

/// Monotonic, high-resolution clock. Its time base is the one used by event timestamps.
/// Stateless; thread-safe; usable without a Platform.
class Clock {
public:
    /// Time since an unspecified fixed point (process start, in practice).
    [[nodiscard]] std::chrono::nanoseconds now() const noexcept;
};

} // namespace ezmu::platform

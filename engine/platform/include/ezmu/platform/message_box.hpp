#pragma once

#include <ezmu/foundation/result.hpp>

#include <cstdint>
#include <string_view>

namespace ezmu::platform {

enum class MessageBoxKind : std::uint8_t { kInfo, kWarning, kError };

/// Shows a modal message box and blocks until it is dismissed. Works without a Platform (e.g.
/// for fatal errors in main). Any thread; fails when no display is available.
[[nodiscard]] foundation::Result<> showMessageBox(MessageBoxKind kind, std::string_view title,
                                                  std::string_view text);

} // namespace ezmu::platform

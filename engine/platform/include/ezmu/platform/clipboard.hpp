#pragma once

#include <ezmu/foundation/result.hpp>

#include <string>
#include <string_view>

namespace ezmu::platform {

/// System clipboard (text only). Owned by Platform; main thread only.
class Clipboard {
public:
    /// Current clipboard text (UTF-8); empty when the clipboard holds no text.
    [[nodiscard]] foundation::Result<std::string> text() const;

    /// Replaces the clipboard content with `utf8`.
    [[nodiscard]] foundation::Result<> setText(std::string_view utf8);
};

} // namespace ezmu::platform

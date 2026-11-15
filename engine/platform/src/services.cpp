// Clock, clipboard and message box: thin SDL wrappers.

#include <ezmu/platform/message_box.hpp>

#include <cstdint>
#include <memory>
#include <string>

#include "platform_impl.hpp"

namespace ezmu::platform {

namespace {

struct SdlFree {
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc): RAII deleter for SDL-allocated strings.
    void operator()(void* memory) const noexcept { SDL_free(memory); }
};

} // namespace

// Clock and Clipboard are instance APIs on purpose (services passed explicitly, replaceable
// later), even though the SDL-backed implementation needs no state.

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
std::chrono::nanoseconds Clock::now() const noexcept {
    return std::chrono::nanoseconds(static_cast<std::int64_t>(SDL_GetTicksNS()));
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
foundation::Result<std::string> Clipboard::text() const {
    if (!SDL_HasClipboardText()) {
        return std::string{};
    }
    const std::unique_ptr<char, SdlFree> text(SDL_GetClipboardText());
    if (text == nullptr) {
        return std::unexpected(detail::sdlError("Clipboard::text"));
    }
    return std::string(text.get());
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
foundation::Result<> Clipboard::setText(std::string_view utf8) {
    const std::string text(utf8);
    if (!SDL_SetClipboardText(text.c_str())) {
        return std::unexpected(detail::sdlError("Clipboard::setText"));
    }
    return {};
}

foundation::Result<> showMessageBox(MessageBoxKind kind, std::string_view title,
                                    std::string_view text) {
    SDL_MessageBoxFlags flags = SDL_MESSAGEBOX_INFORMATION;
    switch (kind) {
    case MessageBoxKind::kInfo:
        flags = SDL_MESSAGEBOX_INFORMATION;
        break;
    case MessageBoxKind::kWarning:
        flags = SDL_MESSAGEBOX_WARNING;
        break;
    case MessageBoxKind::kError:
        flags = SDL_MESSAGEBOX_ERROR;
        break;
    }
    const std::string titleString(title);
    const std::string textString(text);
    if (!SDL_ShowSimpleMessageBox(flags, titleString.c_str(), textString.c_str(), nullptr)) {
        return std::unexpected(detail::sdlError("showMessageBox"));
    }
    return {};
}

} // namespace ezmu::platform

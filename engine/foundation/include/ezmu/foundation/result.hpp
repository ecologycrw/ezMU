#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace ezmu::foundation {

/// Broad category of a recoverable error.
enum class ErrorCode : std::uint8_t {
    kUnknown = 0,
    kInvalidArgument,
    kNotFound,
    kOutOfRange,
    kIoError,
    kParseError,
    kUnsupported,
    kTimeout,
    kInternal,
};

/// Stable, human-readable name of an error code (e.g. "NotFound").
[[nodiscard]] std::string_view toString(ErrorCode code) noexcept;

/// A recoverable error: a category plus a context message.
class Error {
public:
    Error(ErrorCode code, std::string message) : m_code(code), m_message(std::move(message)) {}

    [[nodiscard]] ErrorCode code() const noexcept { return m_code; }
    [[nodiscard]] const std::string& message() const noexcept { return m_message; }

    friend bool operator==(const Error&, const Error&) = default;

private:
    ErrorCode m_code;
    std::string m_message;
};

/// Return type of every fallible engine operation (ADR 0018). Use `Result<>` for no value.
template <typename T = void>
using Result = std::expected<T, Error>;

/// Convenience to return an error from a function returning `Result<T>`.
[[nodiscard]] inline std::unexpected<Error> makeError(ErrorCode code, std::string message) {
    return std::unexpected<Error>(std::in_place, code, std::move(message));
}

} // namespace ezmu::foundation

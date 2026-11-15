#include <ezmu/foundation/result.hpp>

namespace ezmu::foundation {

std::string_view toString(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::kUnknown:
        return "Unknown";
    case ErrorCode::kInvalidArgument:
        return "InvalidArgument";
    case ErrorCode::kNotFound:
        return "NotFound";
    case ErrorCode::kOutOfRange:
        return "OutOfRange";
    case ErrorCode::kIoError:
        return "IoError";
    case ErrorCode::kParseError:
        return "ParseError";
    case ErrorCode::kUnsupported:
        return "Unsupported";
    case ErrorCode::kTimeout:
        return "Timeout";
    case ErrorCode::kInternal:
        return "Internal";
    }
    return "Unknown";
}

} // namespace ezmu::foundation

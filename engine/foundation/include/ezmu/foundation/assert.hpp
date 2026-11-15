#pragma once

#include <source_location>
#include <string_view>

// Assertions catch programming errors (broken invariants), not recoverable errors; use
// `Result<T>` for the latter (ADR 0018).
//
//   EZMU_ASSERT(expr)            checked in debug builds (or with EZMU_FORCE_ASSERTS);
//   EZMU_ASSERT_MSG(expr, msg)   compiled out otherwise — `expr` must have no side effects.
//   EZMU_VERIFY(expr)            always evaluated and checked, in every build.
//   EZMU_VERIFY_MSG(expr, msg)
//
// On failure the current assertion handler is called. The default handler prints the failure
// to stderr and aborts. A custom handler that returns lets execution continue (used by tests).

namespace ezmu::foundation {

struct AssertionInfo {
    std::string_view expression;
    std::string_view message;
    std::source_location location;
};

using AssertionHandler = void (*)(const AssertionInfo& info);

/// Installs `handler` (nullptr restores the default) and returns the previous handler.
/// The handler is process-wide; this is the one sanctioned piece of global state in foundation.
AssertionHandler setAssertionHandler(AssertionHandler handler) noexcept;

/// Currently installed handler (never nullptr).
[[nodiscard]] AssertionHandler assertionHandler() noexcept;

/// The default handler: reports to stderr and calls std::abort().
[[noreturn]] void defaultAssertionHandler(const AssertionInfo& info);

#if !defined(NDEBUG) || defined(EZMU_FORCE_ASSERTS)
inline constexpr bool kAssertsEnabled = true;
#else
inline constexpr bool kAssertsEnabled = false;
#endif

namespace detail {
void reportAssertionFailure(std::string_view expression, std::string_view message,
                            std::source_location location);
} // namespace detail

} // namespace ezmu::foundation

#define EZMU_DETAIL_CHECK(expr, msg)                                                               \
    do {                                                                                           \
        if (!(expr)) [[unlikely]] {                                                                \
            ::ezmu::foundation::detail::reportAssertionFailure(#expr, (msg),                       \
                                                               std::source_location::current());   \
        }                                                                                          \
    } while (false)

#define EZMU_VERIFY(expr) EZMU_DETAIL_CHECK(expr, "")
#define EZMU_VERIFY_MSG(expr, msg) EZMU_DETAIL_CHECK(expr, msg)

#if !defined(NDEBUG) || defined(EZMU_FORCE_ASSERTS)
#define EZMU_ASSERT(expr) EZMU_DETAIL_CHECK(expr, "")
#define EZMU_ASSERT_MSG(expr, msg) EZMU_DETAIL_CHECK(expr, msg)
#else
#define EZMU_ASSERT(expr)                                                                          \
    do {                                                                                           \
        static_cast<void>(sizeof(expr));                                                           \
    } while (false)
#define EZMU_ASSERT_MSG(expr, msg)                                                                 \
    do {                                                                                           \
        static_cast<void>(sizeof(expr));                                                           \
    } while (false)
#endif

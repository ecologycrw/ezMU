#include <ezmu/foundation/assert.hpp>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <print>

namespace ezmu::foundation {

namespace {

// The one sanctioned piece of mutable global state in foundation (see assert.hpp).
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<AssertionHandler> g_assertionHandler{&defaultAssertionHandler};

} // namespace

AssertionHandler setAssertionHandler(AssertionHandler handler) noexcept {
    if (handler == nullptr) {
        handler = &defaultAssertionHandler;
    }
    return g_assertionHandler.exchange(handler);
}

AssertionHandler assertionHandler() noexcept {
    return g_assertionHandler.load();
}

void defaultAssertionHandler(const AssertionInfo& info) {
    std::println(stderr, "[ezMU] Assertion failed: {}", info.expression);
    if (!info.message.empty()) {
        std::println(stderr, "  message: {}", info.message);
    }
    std::println(stderr, "  at {}:{} ({})", info.location.file_name(), info.location.line(),
                 info.location.function_name());
    std::fflush(stderr);
    std::abort();
}

namespace detail {

void reportAssertionFailure(std::string_view expression, std::string_view message,
                            std::source_location location) {
    const AssertionInfo info{.expression = expression, .message = message, .location = location};
    assertionHandler()(info);
}

} // namespace detail

} // namespace ezmu::foundation

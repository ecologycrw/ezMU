#include <ezmu/foundation/assert.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {

using ezmu::foundation::AssertionHandler;
using ezmu::foundation::AssertionInfo;

struct CapturedFailure {
    int count = 0;
    std::string expression;
    std::string message;
    unsigned line = 0;
};

CapturedFailure g_captured;

void capturingHandler(const AssertionInfo& info) {
    ++g_captured.count;
    g_captured.expression = info.expression;
    g_captured.message = info.message;
    g_captured.line = static_cast<unsigned>(info.location.line());
}

/// Installs the capturing handler for the lifetime of the object.
class ScopedCapture {
public:
    ScopedCapture() : m_previous(ezmu::foundation::setAssertionHandler(&capturingHandler)) {
        g_captured = {};
    }
    ~ScopedCapture() { ezmu::foundation::setAssertionHandler(m_previous); }

    ScopedCapture(const ScopedCapture&) = delete;
    ScopedCapture& operator=(const ScopedCapture&) = delete;

private:
    AssertionHandler m_previous;
};

} // namespace

TEST_CASE("EZMU_VERIFY reports failures to the installed handler", "[assert]") {
    const ScopedCapture capture;
    const int value = 3;

    EZMU_VERIFY(value == 3);
    CHECK(g_captured.count == 0);

    EZMU_VERIFY_MSG(value == 4, "value should be four");
    CHECK(g_captured.count == 1);
    CHECK(g_captured.expression == "value == 4");
    CHECK(g_captured.message == "value should be four");
    CHECK(g_captured.line > 0);
}

TEST_CASE("EZMU_VERIFY always evaluates its expression", "[assert]") {
    const ScopedCapture capture;
    int evaluations = 0;

    EZMU_VERIFY(++evaluations == 1);
    CHECK(evaluations == 1);
}

TEST_CASE("EZMU_ASSERT is active only when asserts are enabled", "[assert]") {
    const ScopedCapture capture;

    EZMU_ASSERT(1 + 1 == 3);
    CHECK(g_captured.count == (ezmu::foundation::kAssertsEnabled ? 1 : 0));
}

TEST_CASE("Installing a null handler restores the default", "[assert]") {
    const AssertionHandler original = ezmu::foundation::assertionHandler();
    ezmu::foundation::setAssertionHandler(&capturingHandler);
    ezmu::foundation::setAssertionHandler(nullptr);
    CHECK(ezmu::foundation::assertionHandler() == &ezmu::foundation::defaultAssertionHandler);
    ezmu::foundation::setAssertionHandler(original);
}

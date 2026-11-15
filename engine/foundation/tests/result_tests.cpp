#include <ezmu/foundation/result.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {

using ezmu::foundation::Error;
using ezmu::foundation::ErrorCode;
using ezmu::foundation::makeError;
using ezmu::foundation::Result;

Result<int> parsePositive(int value) {
    if (value <= 0) {
        return makeError(ErrorCode::kInvalidArgument, "value must be positive");
    }
    return value;
}

Result<> doNothing(bool fail) {
    if (fail) {
        return makeError(ErrorCode::kInternal, "failed");
    }
    return {};
}

} // namespace

TEST_CASE("Result holds a value on success", "[result]") {
    const auto result = parsePositive(42);
    REQUIRE(result.has_value());
    CHECK(*result == 42);
}

TEST_CASE("Result holds an error on failure", "[result]") {
    const auto result = parsePositive(-1);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::kInvalidArgument);
    CHECK(result.error().message() == "value must be positive");
}

TEST_CASE("Result<> represents success without a value", "[result]") {
    CHECK(doNothing(false).has_value());
    CHECK(doNothing(true).error().code() == ErrorCode::kInternal);
}

TEST_CASE("Result composes with monadic operations", "[result]") {
    const auto doubled = parsePositive(21).transform([](int v) { return v * 2; });
    CHECK(doubled.value() == 42);

    const auto failed = parsePositive(0).transform([](int v) { return v * 2; });
    CHECK_FALSE(failed.has_value());
}

TEST_CASE("Errors compare by code and message", "[result]") {
    CHECK(Error{ErrorCode::kNotFound, "a"} == Error{ErrorCode::kNotFound, "a"});
    CHECK_FALSE(Error{ErrorCode::kNotFound, "a"} == Error{ErrorCode::kNotFound, "b"});
    CHECK_FALSE(Error{ErrorCode::kNotFound, "a"} == Error{ErrorCode::kTimeout, "a"});
}

TEST_CASE("Error codes have stable names", "[result]") {
    CHECK(toString(ErrorCode::kUnknown) == "Unknown");
    CHECK(toString(ErrorCode::kNotFound) == "NotFound");
    CHECK(toString(ErrorCode::kInternal) == "Internal");
}

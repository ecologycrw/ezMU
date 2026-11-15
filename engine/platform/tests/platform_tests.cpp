#include <ezmu/platform/platform.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

#include "test_support.hpp"

namespace {

using namespace ezmu::platform;
using ezmu::foundation::ErrorCode;
using ezmu::platform::test::eventsOf;
using ezmu::platform::test::makeHeadlessPlatform;

TEST_CASE("A headless platform can be created and destroyed repeatedly", "[platform]") {
    for (int i = 0; i < 2; ++i) {
        auto platform = makeHeadlessPlatform();
        CHECK(platform->videoBackend() == VideoBackend::kHeadless);
    }
}

TEST_CASE("Only one platform may exist at a time", "[platform]") {
    auto first = makeHeadlessPlatform();
    auto second = Platform::create(PlatformDesc{.videoBackend = VideoBackend::kHeadless});
    REQUIRE_FALSE(second.has_value());
    CHECK(second.error().code() == ErrorCode::kInvalidArgument);
}

TEST_CASE("Injected events are delivered once, in order, before OS events", "[platform]") {
    auto platform = makeHeadlessPlatform();
    static_cast<void>(platform->pollEvents());

    platform->injectEvent(KeyDown{.scancode = Scancode::kW});
    platform->injectEvent(KeyUp{.scancode = Scancode::kW});
    platform->injectEvent(QuitRequested{});

    const auto events = platform->pollEvents();
    REQUIRE(events.size() >= 3);
    CHECK(std::get<KeyDown>(events[0]).scancode == Scancode::kW);
    CHECK(std::get<KeyUp>(events[1]).scancode == Scancode::kW);
    CHECK(std::holds_alternative<QuitRequested>(events[2]));

    CHECK(eventsOf<KeyDown>(platform->pollEvents()).empty());
}

TEST_CASE("Waiting returns injected events without blocking", "[platform]") {
    auto platform = makeHeadlessPlatform();
    platform->injectEvent(QuitRequested{});
    const auto events = platform->waitEvents(std::chrono::hours(1));
    CHECK(eventsOf<QuitRequested>(events).size() == 1);
}

TEST_CASE("Waiting with no events returns after the timeout", "[platform]") {
    auto platform = makeHeadlessPlatform();
    static_cast<void>(platform->pollEvents());
    const auto events = platform->waitEvents(std::chrono::milliseconds(0));
    CHECK(eventsOf<KeyDown>(events).empty());
}

TEST_CASE("Polling reuses its event buffer", "[platform]") {
    auto platform = makeHeadlessPlatform();
    const auto inject = [&platform] {
        for (int i = 0; i < 32; ++i) {
            platform->injectEvent(KeyDown{.scancode = Scancode::kA});
        }
    };
    inject();
    const auto* firstBuffer = platform->pollEvents().data();
    inject();
    const auto* secondBuffer = platform->pollEvents().data();
    CHECK(firstBuffer == secondBuffer);
}

TEST_CASE("The clock is monotonic", "[platform][clock]") {
    const Clock clock;
    const auto a = clock.now();
    const auto b = clock.now();
    CHECK(b >= a);
}

TEST_CASE("Clipboard text round-trips", "[platform][clipboard]") {
    auto platform = makeHeadlessPlatform();
    const std::string text = "ezMU — ação 日本";
    if (!platform->clipboard().setText(text)) {
        SKIP("the headless video driver has no clipboard");
    }
    auto read = platform->clipboard().text();
    REQUIRE(read.has_value());
    CHECK(*read == text);
}

} // namespace

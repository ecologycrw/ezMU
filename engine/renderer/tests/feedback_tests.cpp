#include <ezmu/foundation/image.hpp>
#include <ezmu/foundation/result.hpp>
#include <ezmu/renderer/feedback.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace {

using ezmu::foundation::ErrorCode;
using ezmu::renderer::CaptureResult;
using ezmu::renderer::FrameReport;
using ezmu::renderer::RenderFeedbackQueue;

} // namespace

TEST_CASE("RenderFeedbackQueue drains reports oldest first", "[feedback]") {
    RenderFeedbackQueue queue;
    for (std::uint64_t i = 1; i <= 3; ++i) {
        queue.pushReport({.snapshotFrameIndex = i, .presented = true});
    }
    RenderFeedbackQueue::Drained drained;
    queue.drain(drained);
    REQUIRE(drained.reports.size() == 3);
    for (std::uint64_t i = 0; i < 3; ++i) {
        CHECK(drained.reports.at(i).snapshotFrameIndex == i + 1);
    }
    CHECK(drained.droppedReports == 0);
    CHECK(drained.captures.empty());
    CHECK_FALSE(drained.fatalError.has_value());

    queue.drain(drained); // Nothing new: `drained` is cleared.
    CHECK(drained.reports.empty());
}

TEST_CASE("RenderFeedbackQueue drops the oldest reports when full", "[feedback]") {
    RenderFeedbackQueue queue(4);
    for (std::uint64_t i = 1; i <= 6; ++i) {
        queue.pushReport({.snapshotFrameIndex = i});
    }
    RenderFeedbackQueue::Drained drained;
    queue.drain(drained);
    REQUIRE(drained.reports.size() == 4);
    CHECK(drained.reports.front().snapshotFrameIndex == 3);
    CHECK(drained.reports.back().snapshotFrameIndex == 6);
    CHECK(drained.droppedReports == 2);

    queue.pushReport({.snapshotFrameIndex = 7});
    queue.drain(drained);
    REQUIRE(drained.reports.size() == 1);
    CHECK(drained.reports.front().snapshotFrameIndex == 7);
    CHECK(drained.droppedReports == 0); // Counted per drain.
}

TEST_CASE("RenderFeedbackQueue delivers captures and the first fatal error", "[feedback]") {
    RenderFeedbackQueue queue;
    queue.pushCapture(
        {.captureId = 1, .image = ezmu::foundation::Image({.width = 2, .height = 1})});
    queue.pushCapture(
        {.captureId = 2, .image = ezmu::foundation::makeError(ErrorCode::kUnsupported, "no")});
    queue.pushFatalError({ErrorCode::kInternal, "device lost"});
    queue.pushFatalError({ErrorCode::kIoError, "ignored"});

    RenderFeedbackQueue::Drained drained;
    queue.drain(drained);
    REQUIRE(drained.captures.size() == 2);
    CHECK(drained.captures.at(0).captureId == 1);
    REQUIRE(drained.captures.at(0).image.has_value());
    CHECK(drained.captures.at(0).image->width() == 2);
    CHECK(drained.captures.at(1).captureId == 2);
    REQUIRE_FALSE(drained.captures.at(1).image.has_value());
    CHECK(drained.captures.at(1).image.error().code() == ErrorCode::kUnsupported);
    REQUIRE(drained.fatalError.has_value());
    CHECK(drained.fatalError->code() == ErrorCode::kInternal);

    queue.drain(drained);
    CHECK(drained.captures.empty());
    CHECK_FALSE(drained.fatalError.has_value());
}

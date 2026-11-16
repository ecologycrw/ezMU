#include <ezmu/foundation/image.hpp>
#include <ezmu/foundation/result.hpp>
#include <ezmu/renderer/renderer.hpp>
#include <ezmu/renderer/snapshot.hpp>
#include <ezmu/renderer/snapshot_ring.hpp>
#include <ezmu/rhi/device.hpp>
#include <ezmu/rhi/resources.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "rhi_test_support.hpp"

// GPU tests feed synthetic snapshots straight to an offscreen Renderer (renderer spec, use case
// 4): no runtime, no window. Clear colors are exactly representable in UNORM8 (k/255), so
// readbacks are compared exactly.

namespace {

using ezmu::foundation::ErrorCode;
using ezmu::foundation::Extent2D;
using ezmu::foundation::Image;
using ezmu::foundation::Rgba8;
using ezmu::renderer::FrameResult;
using ezmu::renderer::Renderer;
using ezmu::renderer::RenderSnapshot;
using ezmu::rhi::Device;
using ezmu::rhi::Format;
using ezmu::rhi::test::makeTestDevice;
using ezmu::rhi::test::ValidationGuard;

using Color = std::array<float, 4>;

constexpr Color kBlue{0.2F, 0.4F, 0.6F, 1.0F}; // 51, 102, 153
constexpr Rgba8 kBlue8{.r = 51, .g = 102, .b = 153, .a = 255};
constexpr Color kRed{1.0F, 0.0F, 0.0F, 1.0F};
constexpr Rgba8 kRed8{.r = 255, .g = 0, .b = 0, .a = 255};
constexpr Color kGreen{0.0F, 0.8F, 0.0F, 1.0F}; // 204
constexpr Rgba8 kGreen8{.r = 0, .g = 204, .b = 0, .a = 255};

std::unique_ptr<Renderer> makeRenderer(Device& device, Format format = Format::kRgba8Unorm) {
    auto renderer = Renderer::create({.device = &device, .offscreenFormat = format});
    REQUIRE(renderer.has_value());
    return std::move(*renderer);
}

RenderSnapshot makeSnapshot(std::uint64_t frameIndex, Extent2D size, Color color,
                            std::uint64_t captureId = 0) {
    RenderSnapshot snapshot;
    snapshot.frameIndex = frameIndex;
    snapshot.output.pixelSize = size;
    snapshot.clearColor = color;
    snapshot.captureId = captureId;
    return snapshot;
}

FrameResult renderOk(Renderer& renderer, const RenderSnapshot& snapshot) {
    auto result = renderer.render(snapshot);
    if (!result) {
        FAIL("render failed: " << result.error().message());
    }
    return std::move(*result);
}

bool isFilled(const Image& image, Rgba8 color) {
    for (std::uint32_t y = 0; y < image.height(); ++y) {
        for (std::uint32_t x = 0; x < image.width(); ++x) {
            if (image.pixel(x, y) != color) {
                return false;
            }
        }
    }
    return !image.isEmpty();
}

Image readTarget(Device& device, const Renderer& renderer) {
    auto image = device.readTexture(renderer.offscreenTarget());
    REQUIRE(image.has_value());
    return std::move(*image);
}

} // namespace

TEST_CASE("Renderer creation requires a device", "[renderer]") {
    auto renderer = Renderer::create({});
    REQUIRE_FALSE(renderer.has_value());
    CHECK(renderer.error().code() == ErrorCode::kInvalidArgument);
}

TEST_CASE("Renderer creation rejects offscreen formats it cannot capture", "[renderer][gpu]") {
    ValidationGuard guard;
    auto device = makeTestDevice(guard);
    for (const Format format : {Format::kUndefined, Format::kDepth32Float, Format::kRgba16Float}) {
        auto renderer = Renderer::create({.device = device.get(), .offscreenFormat = format});
        REQUIRE_FALSE(renderer.has_value());
        CHECK(renderer.error().code() == ErrorCode::kInvalidArgument);
    }
}

TEST_CASE("Renderer clears the offscreen target to the snapshot color", "[renderer][gpu]") {
    ValidationGuard guard;
    auto device = makeTestDevice(guard);
    auto renderer = makeRenderer(*device);
    CHECK_FALSE(renderer->offscreenTarget().isValid());

    const FrameResult result = renderOk(*renderer, makeSnapshot(1, {16, 8}, kBlue));
    CHECK(result.report.presented);
    CHECK(result.report.snapshotFrameIndex == 1);
    CHECK(result.report.renderFrameIndex == 0);
    CHECK_FALSE(result.capture.has_value());

    const Image image = readTarget(*device, *renderer);
    CHECK(image.size() == Extent2D{16, 8});
    CHECK(isFilled(image, kBlue8));
}

TEST_CASE("Output size changes recreate the offscreen target", "[renderer][gpu]") {
    ValidationGuard guard;
    auto device = makeTestDevice(guard);
    auto renderer = makeRenderer(*device);

    static_cast<void>(renderOk(*renderer, makeSnapshot(1, {16, 16}, kRed)));
    const auto first = renderer->offscreenTarget();
    static_cast<void>(renderOk(*renderer, makeSnapshot(2, {32, 8}, kGreen)));
    const auto second = renderer->offscreenTarget();
    CHECK(second != first);
    const Image image = readTarget(*device, *renderer);
    CHECK(image.size() == Extent2D{32, 8});
    CHECK(isFilled(image, kGreen8));

    static_cast<void>(renderOk(*renderer, makeSnapshot(3, {32, 8}, kBlue)));
    CHECK(renderer->offscreenTarget() == second); // Same size: reused.
}

TEST_CASE("Empty or non-presentable output skips GPU work", "[renderer][gpu]") {
    ValidationGuard guard;
    auto device = makeTestDevice(guard);
    auto renderer = makeRenderer(*device);

    CHECK(renderOk(*renderer, makeSnapshot(1, {8, 8}, kRed)).report.renderFrameIndex == 0);

    const FrameResult empty = renderOk(*renderer, makeSnapshot(2, {0, 8}, kGreen));
    CHECK_FALSE(empty.report.presented);
    CHECK(empty.report.snapshotFrameIndex == 2);

    RenderSnapshot minimized = makeSnapshot(3, {8, 8}, kGreen);
    minimized.output.presentable = false;
    CHECK_FALSE(renderOk(*renderer, minimized).report.presented);

    CHECK(isFilled(readTarget(*device, *renderer), kRed8)); // Untouched by skipped frames.
    const FrameResult next = renderOk(*renderer, makeSnapshot(4, {8, 8}, kBlue));
    CHECK(next.report.presented);
    CHECK(next.report.renderFrameIndex == 1); // No rhi frame was spent on skipped snapshots.
}

TEST_CASE("A capture returns the output of the requesting frame", "[renderer][gpu]") {
    const Format format = GENERATE(Format::kRgba8Unorm, Format::kBgra8Unorm);
    CAPTURE(ezmu::rhi::toString(format));
    ValidationGuard guard;
    auto device = makeTestDevice(guard);
    auto renderer = makeRenderer(*device, format);

    const FrameResult result = renderOk(*renderer, makeSnapshot(1, {12, 6}, kBlue, 7));
    REQUIRE(result.capture.has_value());
    CHECK(result.capture->captureId == 7);
    CHECK_FALSE(result.capture->srgbEncoded);
    REQUIRE(result.capture->image.has_value());
    CHECK(result.capture->image->size() == Extent2D{12, 6});
    CHECK(isFilled(*result.capture->image, kBlue8)); // BGRA is swizzled to RGBA.

    CHECK_FALSE(renderOk(*renderer, makeSnapshot(2, {12, 6}, kRed)).capture.has_value());
}

TEST_CASE("Captures of sRGB outputs are flagged as sRGB-encoded", "[renderer][gpu]") {
    ValidationGuard guard;
    auto device = makeTestDevice(guard);
    auto renderer = makeRenderer(*device, Format::kRgba8Srgb);
    const FrameResult result = renderOk(*renderer, makeSnapshot(1, {4, 4}, kRed, 1));
    REQUIRE(result.capture.has_value());
    CHECK(result.capture->srgbEncoded);
    REQUIRE(result.capture->image.has_value());
    CHECK(isFilled(*result.capture->image, kRed8)); // 0 and 1 encode to themselves.
}

TEST_CASE("A capture requested on a skipped frame waits for the next rendered frame",
          "[renderer][gpu]") {
    ValidationGuard guard;
    auto device = makeTestDevice(guard);
    auto renderer = makeRenderer(*device);

    RenderSnapshot skipped = makeSnapshot(1, {8, 8}, kRed, 9);
    skipped.output.presentable = false;
    CHECK_FALSE(renderOk(*renderer, skipped).capture.has_value());

    const FrameResult next = renderOk(*renderer, makeSnapshot(2, {8, 8}, kGreen));
    REQUIRE(next.capture.has_value());
    CHECK(next.capture->captureId == 9);
    REQUIRE(next.capture->image.has_value());
    CHECK(isFilled(*next.capture->image, kGreen8));

    SECTION("or is cancelled") {
        static_cast<void>(renderOk(*renderer, makeSnapshot(3, {0, 0}, kRed, 10)));
        const auto cancelled = renderer->cancelPendingCapture();
        REQUIRE(cancelled.has_value());
        CHECK(cancelled->captureId == 10);
        REQUIRE_FALSE(cancelled->image.has_value());
        CHECK(cancelled->image.error().code() == ErrorCode::kUnsupported);
        CHECK_FALSE(renderer->cancelPendingCapture().has_value());
    }
}

TEST_CASE("A producer thread and the render thread exchange snapshots through the ring",
          "[renderer][gpu]") {
    constexpr std::uint64_t kFrames = 300;
    constexpr std::uint64_t kCaptureFrame = 150;
    constexpr std::array<Extent2D, 3> kSizes{{{16, 16}, {32, 8}, {8, 24}}};
    constexpr std::array<Color, 3> kColors{kRed, kGreen, kBlue};
    constexpr std::array<Rgba8, 3> kColors8{kRed8, kGreen8, kBlue8};

    ValidationGuard guard;
    auto device = makeTestDevice(guard);
    auto renderer = makeRenderer(*device);
    ezmu::renderer::SnapshotRing ring;

    // Game thread stand-in. The test thread plays the render thread (it owns the device).
    std::jthread producer([&ring, &kSizes, &kColors] {
        for (std::uint64_t i = 1; i <= kFrames; ++i) {
            RenderSnapshot* snapshot = ring.beginWrite({});
            if (snapshot == nullptr) {
                return;
            }
            snapshot->frameIndex = i;
            snapshot->output.pixelSize = kSizes.at((i / 10) % kSizes.size());
            snapshot->clearColor = kColors.at(i % kColors.size());
            snapshot->captureId = i == kCaptureFrame ? 42U : 0U;
            ring.endWrite();
        }
        ring.close();
    });

    std::vector<ezmu::renderer::FrameReport> reports;
    std::optional<ezmu::renderer::CaptureResult> capture;
    bool failed = false;
    while (const RenderSnapshot* snapshot = ring.beginRead({})) {
        auto result = renderer->render(*snapshot);
        ring.endRead();
        if (!result) {
            UNSCOPED_INFO("render failed: " << result.error().message());
            failed = true;
            ring.close();
            break;
        }
        reports.push_back(result->report);
        if (result->capture) {
            capture = std::move(result->capture);
        }
    }
    producer.join();

    REQUIRE_FALSE(failed);
    REQUIRE(reports.size() == kFrames);
    std::uint64_t ordered = 0;
    for (std::uint64_t i = 0; i < kFrames; ++i) {
        const auto& report = reports.at(i);
        ordered +=
            report.snapshotFrameIndex == i + 1 && report.renderFrameIndex == i && report.presented
                ? 1U
                : 0U;
    }
    CHECK(ordered == kFrames);
    REQUIRE(capture.has_value());
    CHECK(capture->captureId == 42);
    REQUIRE(capture->image.has_value());
    CHECK(capture->image->size() == kSizes.at((kCaptureFrame / 10) % kSizes.size()));
    CHECK(isFilled(*capture->image, kColors8.at(kCaptureFrame % kColors8.size())));
}

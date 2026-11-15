#include <ezmu/foundation/assert.hpp>
#include <ezmu/foundation/image.hpp>
#include <ezmu/rhi/command_list.hpp>
#include <ezmu/rhi/device.hpp>
#include <ezmu/rhi/frame.hpp>
#include <ezmu/rhi/resources.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>

#include "rhi_test_support.hpp"

namespace {

using namespace ezmu::rhi;
using ezmu::foundation::AssertionHandler;
using ezmu::foundation::AssertionInfo;
using ezmu::foundation::ErrorCode;
using ezmu::foundation::Rgba8;
using ezmu::rhi::test::makeTestDevice;
using ezmu::rhi::test::ValidationGuard;

int g_assertionFailures = 0; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): test.

void countingHandler(const AssertionInfo& /*info*/) {
    ++g_assertionFailures;
}

/// Installs a handler that counts (and survives) assertion failures for its lifetime.
class ScopedAssertCount {
public:
    ScopedAssertCount() : m_previous(ezmu::foundation::setAssertionHandler(&countingHandler)) {
        g_assertionFailures = 0;
    }
    ~ScopedAssertCount() { ezmu::foundation::setAssertionHandler(m_previous); }
    ScopedAssertCount(const ScopedAssertCount&) = delete;
    ScopedAssertCount& operator=(const ScopedAssertCount&) = delete;
    ScopedAssertCount(ScopedAssertCount&&) = delete;
    ScopedAssertCount& operator=(ScopedAssertCount&&) = delete;

private:
    AssertionHandler m_previous;
};

constexpr TextureDesc kTarget{.size = {.width = 16, .height = 16},
                              .format = Format::kRgba8Unorm,
                              .usage = TextureUsage::kRenderTarget | TextureUsage::kTransferSrc,
                              .debugName = "frame target"};

void clear(CommandList& cmd, TextureHandle texture, std::array<float, 4> color) {
    cmd.transition(texture, ResourceState::kRenderTarget);
    const ColorAttachment attachment{.texture = texture, .clearColor = color};
    cmd.beginRendering({.colorAttachments = {&attachment, 1}});
    cmd.endRendering();
}

TEST_CASE("Frames in flight must be between 1 and the maximum", "[frame]") {
    CHECK(DeviceDesc{}.framesInFlight == 2);
    for (const std::uint32_t frames : {0U, kMaxFramesInFlight + 1}) {
        auto device = Device::create({.requirePresent = false,
                                      .validation = ValidationMode::kOff,
                                      .framesInFlight = frames});
        REQUIRE_FALSE(device.has_value());
        CHECK(device.error().code() == ErrorCode::kInvalidArgument);
    }
}

TEST_CASE("Frames have monotonic indices and rotating slots", "[frame][gpu]") {
    ValidationGuard guard;
    auto device = makeTestDevice(guard, {.framesInFlight = 3});
    REQUIRE(device->framesInFlight() == 3);
    for (std::uint64_t i = 0; i < 7; ++i) {
        auto frame = device->beginFrame();
        REQUIRE(frame.has_value());
        CHECK((*frame)->index() == i);
        CHECK((*frame)->slot() == i % 3);
        REQUIRE(device->endFrame(**frame).has_value());
    }
}

TEST_CASE("Two hundred frames with resource churn leave no validation messages", "[frame][gpu]") {
    ValidationGuard guard;
    auto device = makeTestDevice(guard);

    // Resources are destroyed while frames that used them may still be on the GPU: deferred
    // destruction must keep them alive (validation reports use-after-free otherwise).
    std::deque<std::pair<TextureHandle, BufferHandle>> alive;
    std::size_t maxPending = 0;
    for (int i = 0; i < 200; ++i) {
        auto frame = device->beginFrame();
        REQUIRE(frame.has_value());
        auto texture = device->createTexture(kTarget);
        auto buffer = device->createBuffer({.size = 16 * 16 * 4,
                                            .location = MemoryLocation::kReadback,
                                            .debugName = "frame readback"});
        REQUIRE(texture.has_value());
        REQUIRE(buffer.has_value());

        CommandList& cmd = (*frame)->commandList();
        cmd.beginDebugLabel("churn");
        clear(cmd, *texture, {0.0F, 0.2F, 0.4F, 1.0F});
        cmd.transition(*texture, ResourceState::kTransferSrc);
        cmd.copyTextureToBuffer(*texture, *buffer);
        cmd.endDebugLabel();
        alive.emplace_back(*texture, *buffer);

        // Destroy the resources of the previous frame, still in flight, inside this frame.
        if (alive.size() > 1) {
            device->destroy(alive.front().first);
            device->destroy(alive.front().second);
            alive.pop_front();
        }
        REQUIRE(device->endFrame(**frame).has_value());
        // Destroying outside a frame is deferred too while earlier frames are in flight.
        if (i % 5 == 0) {
            device->destroy(alive.back().first);
            device->destroy(alive.back().second);
            alive.pop_back();
        }
        maxPending = std::max(maxPending, device->pendingDestructionCount());
    }
    // Freed at the latest when the frame slot comes around again.
    CHECK(maxPending > 0);
    CHECK(maxPending <= 4 * std::size_t{device->framesInFlight() + 1});

    // Pending destructions and the remaining resources are drained at shutdown.
    REQUIRE_FALSE(alive.empty());
    device->destroy(alive.back().first);
    device->destroy(alive.back().second);
}

TEST_CASE("waitIdle frees pending destructions", "[frame][gpu]") {
    ValidationGuard guard;
    auto device = makeTestDevice(guard);
    auto texture = device->createTexture(kTarget);
    REQUIRE(texture.has_value());

    // Without frames in flight, destruction is immediate.
    device->destroy(*texture);
    CHECK(device->pendingDestructionCount() == 0);

    texture = device->createTexture(kTarget);
    REQUIRE(texture.has_value());
    auto frame = device->beginFrame();
    REQUIRE(frame.has_value());
    clear((*frame)->commandList(), *texture, {1.0F, 0.0F, 0.0F, 1.0F});
    device->destroy(*texture); // Used by the frame being recorded: deferred.
    CHECK(device->pendingDestructionCount() == 1);
    REQUIRE(device->endFrame(**frame).has_value());
    device->waitIdle();
    CHECK(device->pendingDestructionCount() == 0);
}

TEST_CASE("A texture rendered in a frame reads back exactly", "[frame][gpu]") {
    ValidationGuard guard;
    auto device = makeTestDevice(guard);
    auto texture = device->createTexture(kTarget);
    REQUIRE(texture.has_value());

    for (int i = 0; i < 3; ++i) { // Several frames: the last clear wins.
        auto frame = device->beginFrame();
        REQUIRE(frame.has_value());
        const float green = i == 2 ? 0.6F : 1.0F;
        clear((*frame)->commandList(), *texture, {0.2F, green, 0.8F, 1.0F});
        REQUIRE(device->endFrame(**frame).has_value());
    }
    // No explicit wait: the readback is ordered after the frames on the queue.
    auto image = device->readTexture(*texture);
    REQUIRE(image.has_value());
    CHECK(image->pixel(0, 0) == Rgba8{.r = 51, .g = 153, .b = 204, .a = 255});
    CHECK(image->pixel(15, 15) == Rgba8{.r = 51, .g = 153, .b = 204, .a = 255});

    // The frame after a readback sees the restored state.
    auto frame = device->beginFrame();
    REQUIRE(frame.has_value());
    clear((*frame)->commandList(), *texture, {0.0F, 0.0F, 0.0F, 1.0F});
    REQUIRE(device->endFrame(**frame).has_value());
}

TEST_CASE("Frames reject misuse", "[frame][gpu]") {
    ValidationGuard guard;
    auto device = makeTestDevice(guard);
    auto texture = device->createTexture(kTarget);
    REQUIRE(texture.has_value());

    auto frame = device->beginFrame();
    REQUIRE(frame.has_value());
    clear((*frame)->commandList(), *texture, {1.0F, 1.0F, 1.0F, 1.0F});
    {
        const ScopedAssertCount count;
        auto nested = device->beginFrame();
        REQUIRE_FALSE(nested.has_value());
        CHECK(nested.error().code() == ErrorCode::kInvalidArgument);
        auto immediate = device->executeImmediate([](CommandList& /*cmd*/) {});
        REQUIRE_FALSE(immediate.has_value());
        CHECK(immediate.error().code() == ErrorCode::kInvalidArgument);
        auto read = device->readTexture(*texture);
        REQUIRE_FALSE(read.has_value());
        CHECK(read.error().code() == ErrorCode::kInvalidArgument);
        // Left open: endFrame closes the pass (after an assertion) so the frame stays valid.
        CommandList& cmd = (*frame)->commandList();
        const ColorAttachment attachment{.texture = *texture, .load = LoadOp::kLoad};
        cmd.beginRendering({.colorAttachments = {&attachment, 1}});
        REQUIRE(device->endFrame(**frame).has_value());
        if constexpr (ezmu::foundation::kAssertsEnabled) {
            CHECK(g_assertionFailures == 4);
        }
    }
    // The device is usable afterwards.
    REQUIRE(device
                ->executeImmediate([&](CommandList& cmd) {
                    cmd.transition(*texture, ResourceState::kTransferSrc);
                })
                .has_value());
}

} // namespace

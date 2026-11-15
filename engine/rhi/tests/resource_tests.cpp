#include <ezmu/foundation/assert.hpp>
#include <ezmu/rhi/command_list.hpp>
#include <ezmu/rhi/device.hpp>
#include <ezmu/rhi/resources.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "rhi_test_support.hpp"

namespace {

using namespace ezmu::rhi;
using ezmu::foundation::AssertionHandler;
using ezmu::foundation::AssertionInfo;
using ezmu::foundation::ErrorCode;
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

constexpr TextureDesc kTarget{.size = {.width = 16, .height = 8},
                              .format = Format::kRgba8Unorm,
                              .usage = TextureUsage::kRenderTarget | TextureUsage::kTransferSrc,
                              .debugName = "test target"};

TEST_CASE("Textures and buffers are created and destroyed", "[resources][gpu]") {
    ValidationGuard guard;
    auto device = makeTestDevice(guard);

    auto texture = device->createTexture(kTarget);
    REQUIRE(texture.has_value());
    CHECK(texture->isValid());

    auto depth = device->createTexture({.size = {.width = 16, .height = 16},
                                        .format = Format::kDepth32Float,
                                        .usage = TextureUsage::kDepthStencil,
                                        .debugName = "test depth"});
    REQUIRE(depth.has_value());

    auto gpu = device->createBuffer({.size = 256, .debugName = "gpu buffer"});
    auto upload = device->createBuffer({.size = 256, .location = MemoryLocation::kUpload});
    auto readback = device->createBuffer({.size = 256, .location = MemoryLocation::kReadback});
    REQUIRE(gpu.has_value());
    REQUIRE(upload.has_value());
    REQUIRE(readback.has_value());

    CHECK(device->mappedData(*gpu).empty());
    CHECK(device->mappedData(*upload).size() == 256);
    CHECK(device->mappedData(*readback).size() == 256);
    device->mappedData(*upload)[0] = std::byte{42}; // Persistently mapped and writable.

    device->destroy(*texture);
    device->destroy(*depth);
    device->destroy(*gpu);
    device->destroy(*upload);
    device->destroy(*readback);
    device->destroy(TextureHandle{}); // Invalid handles are ignored.
    device->destroy(BufferHandle{});
}

TEST_CASE("Invalid resource descriptions are rejected", "[resources][gpu]") {
    ValidationGuard guard;
    auto device = makeTestDevice(guard);

    const auto code = [&](const TextureDesc& desc) {
        auto result = device->createTexture(desc);
        return result ? ErrorCode::kUnknown : result.error().code();
    };
    TextureDesc empty = kTarget;
    empty.size = {};
    CHECK(code(empty) == ErrorCode::kInvalidArgument);
    TextureDesc undefined = kTarget;
    undefined.format = Format::kUndefined;
    CHECK(code(undefined) == ErrorCode::kInvalidArgument);
    TextureDesc noUsage = kTarget;
    noUsage.usage = TextureUsage::kNone;
    CHECK(code(noUsage) == ErrorCode::kInvalidArgument);
    TextureDesc depthTarget = kTarget;
    depthTarget.format = Format::kDepth32Float;
    CHECK(code(depthTarget) == ErrorCode::kInvalidArgument);
    TextureDesc colorDepth = kTarget;
    colorDepth.usage = TextureUsage::kDepthStencil;
    CHECK(code(colorDepth) == ErrorCode::kInvalidArgument);
    TextureDesc huge = kTarget;
    huge.size = {.width = 1U << 30U, .height = 1};
    CHECK(code(huge) == ErrorCode::kUnsupported);

    auto zero = device->createBuffer({.size = 0});
    REQUIRE_FALSE(zero.has_value());
    CHECK(zero.error().code() == ErrorCode::kInvalidArgument);
}

TEST_CASE("Destroyed handles become stale and slots are reused", "[resources][gpu]") {
    ValidationGuard guard;
    auto device = makeTestDevice(guard);

    auto first = device->createBuffer({.size = 64, .location = MemoryLocation::kReadback});
    REQUIRE(first.has_value());
    const BufferHandle stale = *first;
    device->destroy(stale);

    auto second = device->createBuffer({.size = 64, .location = MemoryLocation::kReadback});
    REQUIRE(second.has_value());
    CHECK(second->index() == stale.index());
    CHECK(second->generation() != stale.generation());
    CHECK(*second != stale);

    {
        const ScopedAssertCount count;
        device->destroy(stale); // Stale: asserts, then does nothing.
        CHECK(device->mappedData(stale).empty());
        if constexpr (ezmu::foundation::kAssertsEnabled) {
            CHECK(g_assertionFailures == 2);
        }
    }
    CHECK(device->mappedData(*second).size() == 64); // The live buffer is untouched.
    device->destroy(*second);
}

TEST_CASE("Resource churn leaves no validation messages", "[resources][gpu]") {
    ValidationGuard guard;
    auto device = makeTestDevice(guard);

    std::vector<TextureHandle> seen;
    for (int i = 0; i < 200; ++i) {
        auto texture = device->createTexture(kTarget);
        auto buffer = device->createBuffer({.size = 512, .location = MemoryLocation::kReadback});
        REQUIRE(texture.has_value());
        REQUIRE(buffer.has_value());
        const TextureHandle target = *texture;
        const BufferHandle readback = *buffer;
        auto executed = device->executeImmediate([&](CommandList& cmd) {
            cmd.transition(target, ResourceState::kRenderTarget);
            const ColorAttachment color{.texture = target};
            cmd.beginRendering({.colorAttachments = {&color, 1}});
            cmd.endRendering();
            cmd.transition(target, ResourceState::kTransferSrc);
            cmd.copyTextureToBuffer(target, readback);
        });
        REQUIRE(executed.has_value());
        if (i % 3 != 0) { // Leave every third pair for the device to destroy at shutdown.
            device->destroy(target);
            device->destroy(readback);
        }
        seen.push_back(target);
    }
    CHECK(seen.front() != seen.back());
}

} // namespace

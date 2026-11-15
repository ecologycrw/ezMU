#include <ezmu/rhi/device.hpp>

// Tests of the rhi module may use Vulkan directly (e.g. for the validation self-test).
#include <catch2/catch_test_macros.hpp>
#include <volk.h>

#include <string>

#include "rhi_test_support.hpp"

namespace {

using namespace ezmu::rhi;
using ezmu::foundation::ErrorCode;
using ezmu::foundation::Version;
using ezmu::rhi::test::environmentVariable;
using ezmu::rhi::test::makeTestDevice;
using ezmu::rhi::test::ValidationGuard;

TEST_CASE("Validation is on by default in debug builds only", "[device]") {
#ifdef NDEBUG
    STATIC_CHECK(kDefaultValidationMode == ValidationMode::kOff);
#else
    STATIC_CHECK(kDefaultValidationMode == ValidationMode::kEnabled);
#endif
}

TEST_CASE("Adapter types have stable names", "[device]") {
    CHECK(toString(AdapterType::kDiscrete) == "discrete");
    CHECK(toString(AdapterType::kIntegrated) == "integrated");
    CHECK(toString(AdapterType::kCpu) == "cpu");
}

TEST_CASE("A headless device runs on a Vulkan 1.3 adapter", "[device][gpu]") {
    ValidationGuard guard;
    auto device = makeTestDevice(guard);

    const AdapterInfo& adapter = device->adapterInfo();
    UNSCOPED_INFO("adapter: " << adapter.name << " (" << toString(adapter.type) << ", "
                              << adapter.driver << ")");
    CHECK_FALSE(adapter.name.empty());
    CHECK(adapter.apiVersion >= Version{.major = 1, .minor = 3, .patch = 0});
    CHECK(device->validationMode() == ValidationMode::kEnabledWithSync);
    CHECK(device->nativeInstance() != nullptr);
    CHECK(device->nativePhysicalDevice() != nullptr);
    CHECK(device->nativeDevice() != nullptr);

    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
    CHECK(device->validationWarningCount() == 0);
}

TEST_CASE("Validation errors reach the callback and are counted", "[device][gpu]") {
    // Self-test of the validation guard: if this fails, validation is not really active and
    // every other GPU test would pass vacuously.
    ValidationGuard guard;
    auto device = makeTestDevice(guard);

    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = 0; // Invalid: VUID-VkBufferCreateInfo-size-00912.
    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer buffer = VK_NULL_HANDLE;
    auto* vkDevice = static_cast<VkDevice>(device->nativeDevice());
    if (vkCreateBuffer(vkDevice, &info, nullptr, &buffer) == VK_SUCCESS) {
        vkDestroyBuffer(vkDevice, buffer, nullptr);
    }

    CHECK(device->validationErrorCount() >= 1);
    const auto messages = guard.recorder().messages();
    REQUIRE_FALSE(messages.empty());
    CHECK(messages.front().find("VkBufferCreateInfo") != std::string::npos);
    guard.recorder().clear(); // Expected messages; the guard must not fail this test.
}

TEST_CASE("Only one device may exist at a time", "[device][gpu]") {
    ValidationGuard guard;
    auto device = makeTestDevice(guard);
    auto second = Device::create(DeviceDesc{.requirePresent = false});
    REQUIRE_FALSE(second.has_value());
    CHECK(second.error().code() == ErrorCode::kInvalidArgument);
}

TEST_CASE("The adapter name filter selects or rejects adapters", "[device][gpu]") {
    if (!environmentVariable("EZMU_GPU").empty()) {
        SKIP("EZMU_GPU overrides the adapter name filter");
    }
    std::string name;
    {
        ValidationGuard guard;
        name = makeTestDevice(guard)->adapterInfo().name;
    }
    {
        ValidationGuard guard;
        auto device = makeTestDevice(guard, DeviceDesc{.adapterNameFilter = name});
        CHECK(device->adapterInfo().name == name);
    }
    auto none = Device::create(DeviceDesc{.requirePresent = false,
                                          .validation = ValidationMode::kOff,
                                          .adapterNameFilter = "no-such-adapter-ezmu"});
    REQUIRE_FALSE(none.has_value());
    CHECK(none.error().code() == ErrorCode::kUnsupported);
    CHECK(none.error().message().find("no-such-adapter-ezmu") != std::string::npos);
}

} // namespace

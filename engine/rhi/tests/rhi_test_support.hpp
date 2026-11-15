#pragma once

// GPU test support (ADR 0020). Every GPU test:
//   * is tagged [gpu] (CTest label "gpu");
//   * creates its device through makeTestDevice(), which enables validation (with
//     synchronization validation) and records every message;
//   * declares a ValidationGuard *before* the device, so messages emitted while the device is
//     destroyed are still checked: any warning or error fails the test.
// Without a usable Vulkan 1.3 adapter or validation layer the test is skipped, unless
// EZMU_REQUIRE_GPU=1, which turns the skip into a failure.
//
// Reference images: checkReferenceImage() compares with PNGs in engine/rhi/tests/reference/.

#include <ezmu/foundation/image.hpp>
#include <ezmu/foundation/image_compare.hpp>
#include <ezmu/foundation/image_io.hpp>
#include <ezmu/rhi/device.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace ezmu::rhi::test {

/// Thread-safe recorder of validation messages.
class ValidationRecorder {
public:
    void record(const ValidationMessage& message) {
        const std::scoped_lock lock(m_mutex);
        m_messages.push_back(std::string(message.messageIdName) + ": " + std::string(message.text));
    }

    [[nodiscard]] std::vector<std::string> messages() const {
        const std::scoped_lock lock(m_mutex);
        return m_messages;
    }

    void clear() {
        const std::scoped_lock lock(m_mutex);
        m_messages.clear();
    }

private:
    mutable std::mutex m_mutex;
    std::vector<std::string> m_messages;
};

/// Fails the current test if any validation message was recorded during its lifetime.
class ValidationGuard {
public:
    ValidationGuard() = default;
    ValidationGuard(const ValidationGuard&) = delete;
    ValidationGuard& operator=(const ValidationGuard&) = delete;
    ValidationGuard(ValidationGuard&&) = delete;
    ValidationGuard& operator=(ValidationGuard&&) = delete;
    ~ValidationGuard() {
        const auto messages = m_recorder.messages();
        for (const auto& message : messages) {
            UNSCOPED_INFO("validation: " << message);
        }
        CHECK(messages.empty());
    }

    [[nodiscard]] ValidationRecorder& recorder() noexcept { return m_recorder; }

private:
    ValidationRecorder m_recorder;
};

/// Value of an environment variable ("" when unset).
[[nodiscard]] inline std::string environmentVariable(const char* name) {
#ifdef _MSC_VER
    char* buffer = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&buffer, &length, name) != 0 || buffer == nullptr) {
        return {};
    }
    std::string value(buffer);
    std::free(buffer); // NOLINT(cppcoreguidelines-no-malloc): allocated by _dupenv_s.
    return value;
#else
    const char* value = std::getenv(name); // NOLINT(concurrency-mt-unsafe): test setup.
    return value != nullptr ? std::string(value) : std::string();
#endif
}

[[nodiscard]] inline bool gpuRequired() {
    return environmentVariable("EZMU_REQUIRE_GPU") == "1";
}

/// Compares `actual` with the reference PNG `<EZMU_RHI_REFERENCE_DIR>/<name>.png`.
///
/// On mismatch writes `actual.png` and `diff.png` (and `expected.png`) to
/// `<build>/test-output/rhi/<name>/` and fails the test. With EZMU_UPDATE_REFERENCES=1 the
/// reference is (re)written instead of compared; review the new PNG before committing it.
inline void checkReferenceImage(const foundation::Image& actual, const std::string& name,
                                const foundation::ImageCompareOptions& options = {}) {
    const std::filesystem::path reference =
        std::filesystem::path(EZMU_RHI_REFERENCE_DIR) / (name + ".png");
    if (environmentVariable("EZMU_UPDATE_REFERENCES") == "1") {
        auto written = foundation::writePng(reference, actual);
        REQUIRE(written.has_value());
        WARN("reference image updated: " << reference.string());
        return;
    }
    auto expected = foundation::readPng(reference);
    if (!expected) {
        FAIL("missing reference image " << reference.string() << " (" << expected.error().message()
                                        << "); run with EZMU_UPDATE_REFERENCES=1 to create it");
    }
    const auto comparison = foundation::compareImages(actual, *expected, options);
    if (comparison.matches) {
        return;
    }
    const std::filesystem::path output = std::filesystem::path(EZMU_RHI_TEST_OUTPUT_DIR) / name;
    static_cast<void>(foundation::writePng(output / "actual.png", actual));
    static_cast<void>(foundation::writePng(output / "expected.png", *expected));
    if (!comparison.diff.isEmpty()) {
        static_cast<void>(foundation::writePng(output / "diff.png", comparison.diff));
    }
    FAIL("image differs from " << reference.string()
                               << ": size matches = " << comparison.sizeMatches
                               << ", differing pixels = " << comparison.differingPixels
                               << ", max channel delta = " << int{comparison.maxChannelDelta}
                               << "; see " << output.string());
}

/// A validated headless device, or a skipped test (failed with EZMU_REQUIRE_GPU=1).
inline std::unique_ptr<Device> makeTestDevice(ValidationGuard& guard, DeviceDesc desc = {}) {
    desc.appName = "ezMU rhi tests";
    desc.requirePresent = false;
    desc.validation = ValidationMode::kEnabledWithSync;
    desc.onValidationMessage = [&recorder = guard.recorder()](const ValidationMessage& message) {
        recorder.record(message);
    };
    auto device = Device::create(desc);
    if (!device) {
        if (device.error().code() == foundation::ErrorCode::kUnsupported && !gpuRequired()) {
            SKIP("no usable Vulkan 1.3 device: " << device.error().message());
        }
        FAIL("Device::create failed: " << device.error().message());
    }
    return std::move(*device);
}

} // namespace ezmu::rhi::test

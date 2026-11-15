#pragma once

#include <ezmu/rhi/device.hpp>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "vulkan.hpp"

namespace ezmu::rhi {

struct Device::Impl {
    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
    /// Destroys whatever was created, in reverse order; safe on partially created devices.
    ~Impl();

    bool loaderInitialized = false;
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    std::uint32_t graphicsQueueFamily = 0;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;

    AdapterInfo adapter;
    ValidationMode validation = ValidationMode::kOff;
    ValidationCallback callback;
    std::atomic<std::uint32_t> errorCount{0};
    std::atomic<std::uint32_t> warningCount{0};
};

namespace detail {

/// A physical device that passed (or failed) the requirements check.
struct AdapterCandidate {
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    AdapterInfo info;
    std::uint32_t graphicsQueueFamily = 0;
    std::string rejection; ///< Empty when the adapter is usable.
};

/// Evaluates every physical device of `instance` against the M1 requirements.
[[nodiscard]] foundation::Result<std::vector<AdapterCandidate>>
evaluateAdapters(VkInstance instance, bool requirePresent);

/// Picks the best usable candidate for `desc`, or explains why none fits.
[[nodiscard]] foundation::Result<AdapterCandidate>
selectAdapter(const std::vector<AdapterCandidate>& candidates, const DeviceDesc& desc);

} // namespace detail

} // namespace ezmu::rhi

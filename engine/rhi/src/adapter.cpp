// Adapter (physical device) evaluation and selection.

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "device_impl.hpp"

namespace ezmu::rhi {

std::string_view toString(AdapterType type) noexcept {
    switch (type) {
    case AdapterType::kOther:
        return "other";
    case AdapterType::kIntegrated:
        return "integrated";
    case AdapterType::kDiscrete:
        return "discrete";
    case AdapterType::kVirtual:
        return "virtual";
    case AdapterType::kCpu:
        return "cpu";
    }
    return "other";
}

namespace detail {

namespace {

AdapterType toAdapterType(VkPhysicalDeviceType type) noexcept {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return AdapterType::kIntegrated;
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return AdapterType::kDiscrete;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return AdapterType::kVirtual;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return AdapterType::kCpu;
    default:
        return AdapterType::kOther;
    }
}

std::optional<std::string> environmentVariable(const char* name) {
#ifdef _MSC_VER
    char* buffer = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&buffer, &length, name) != 0 || buffer == nullptr) {
        return std::nullopt;
    }
    std::string value(buffer);
    std::free(buffer); // NOLINT(cppcoreguidelines-no-malloc): allocated by _dupenv_s.
    return value;
#else
    const char* value = std::getenv(name); // NOLINT(concurrency-mt-unsafe): read at startup.
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

/// Lists the required features that `physicalDevice` lacks ("" when all are present).
std::string missingFeatures(VkPhysicalDevice physicalDevice) {
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.pNext = &features13;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &features12;
    vkGetPhysicalDeviceFeatures2(physicalDevice, &features);

    struct Requirement {
        VkBool32 present;
        std::string_view name;
    };
    const std::array requirements{
        Requirement{.present = features13.dynamicRendering, .name = "dynamicRendering"},
        Requirement{.present = features13.synchronization2, .name = "synchronization2"},
        Requirement{.present = features12.timelineSemaphore, .name = "timelineSemaphore"},
        Requirement{.present = features12.descriptorIndexing, .name = "descriptorIndexing"},
        Requirement{.present = features12.runtimeDescriptorArray, .name = "runtimeDescriptorArray"},
        Requirement{.present = features12.descriptorBindingPartiallyBound,
                    .name = "descriptorBindingPartiallyBound"},
        Requirement{.present = features12.descriptorBindingSampledImageUpdateAfterBind,
                    .name = "descriptorBindingSampledImageUpdateAfterBind"},
    };
    std::string missing;
    for (const auto& requirement : requirements) {
        if (requirement.present != VK_TRUE) {
            missing += missing.empty() ? "" : ", ";
            missing += requirement.name;
        }
    }
    return missing;
}

bool hasDeviceExtension(VkPhysicalDevice physicalDevice, std::string_view name) {
    std::uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr) !=
        VK_SUCCESS) {
        return false;
    }
    std::vector<VkExtensionProperties> extensions(count);
    if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, extensions.data()) !=
        VK_SUCCESS) {
        return false;
    }
    return std::ranges::any_of(extensions, [name](const VkExtensionProperties& extension) {
        return name == fixedString(extension.extensionName);
    });
}

std::optional<std::uint32_t> findGraphicsQueueFamily(VkPhysicalDevice physicalDevice) {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, families.data());
    for (std::uint32_t index = 0; index < count; ++index) {
        if ((families.at(index).queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            return index;
        }
    }
    return std::nullopt;
}

AdapterCandidate evaluate(VkPhysicalDevice physicalDevice, bool requirePresent) {
    // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization): filled in by the driver.
    VkPhysicalDeviceDriverProperties driver{};
    driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
    VkPhysicalDeviceProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &driver;
    vkGetPhysicalDeviceProperties2(physicalDevice, &properties);
    const auto& props = properties.properties;

    AdapterCandidate candidate;
    candidate.physicalDevice = physicalDevice;
    candidate.info.name = fixedString(props.deviceName);
    candidate.info.type = toAdapterType(props.deviceType);
    candidate.info.vendorId = props.vendorID;
    candidate.info.deviceId = props.deviceID;
    candidate.info.driver =
        std::format("{} {}", fixedString(driver.driverName), fixedString(driver.driverInfo));
    candidate.info.apiVersion =
        foundation::Version{.major = VK_API_VERSION_MAJOR(props.apiVersion),
                            .minor = VK_API_VERSION_MINOR(props.apiVersion),
                            .patch = VK_API_VERSION_PATCH(props.apiVersion)};

    if (props.apiVersion < VK_API_VERSION_1_3) {
        candidate.rejection = "Vulkan 1.3 not supported";
        return candidate;
    }
    if (const std::string missing = missingFeatures(physicalDevice); !missing.empty()) {
        candidate.rejection = "missing features: " + missing;
        return candidate;
    }
    if (requirePresent && !hasDeviceExtension(physicalDevice, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
        candidate.rejection = "VK_KHR_swapchain not supported";
        return candidate;
    }
    const auto queueFamily = findGraphicsQueueFamily(physicalDevice);
    if (!queueFamily) {
        candidate.rejection = "no graphics queue";
        return candidate;
    }
    candidate.graphicsQueueFamily = *queueFamily;
    return candidate;
}

int rank(AdapterType type, AdapterPreference preference) noexcept {
    switch (preference) {
    case AdapterPreference::kHighPerformance:
        switch (type) {
        case AdapterType::kDiscrete:
            return 0;
        case AdapterType::kIntegrated:
            return 1;
        case AdapterType::kVirtual:
            return 2;
        case AdapterType::kCpu:
            return 3;
        case AdapterType::kOther:
            return 4;
        }
        break;
    case AdapterPreference::kLowPower:
        switch (type) {
        case AdapterType::kIntegrated:
            return 0;
        case AdapterType::kDiscrete:
            return 1;
        case AdapterType::kVirtual:
            return 2;
        case AdapterType::kCpu:
            return 3;
        case AdapterType::kOther:
            return 4;
        }
        break;
    case AdapterPreference::kSoftware:
        return type == AdapterType::kCpu ? 0 : 1;
    }
    return 5;
}

} // namespace

foundation::Result<std::vector<AdapterCandidate>> evaluateAdapters(VkInstance instance,
                                                                   bool requirePresent) {
    std::uint32_t count = 0;
    if (const VkResult result = driverEnumeratePhysicalDevices(instance, count, nullptr);
        result != VK_SUCCESS) {
        return std::unexpected(vkError(result, "vkEnumeratePhysicalDevices"));
    }
    std::vector<VkPhysicalDevice> devices(count);
    if (const VkResult result = driverEnumeratePhysicalDevices(instance, count, devices.data());
        result != VK_SUCCESS && result != VK_INCOMPLETE) {
        return std::unexpected(vkError(result, "vkEnumeratePhysicalDevices"));
    }
    std::vector<AdapterCandidate> candidates;
    candidates.reserve(count);
    for (VkPhysicalDevice device : devices) {
        candidates.push_back(evaluate(device, requirePresent));
    }
    return candidates;
}

foundation::Result<AdapterCandidate> selectAdapter(const std::vector<AdapterCandidate>& candidates,
                                                   const DeviceDesc& desc) {
    const auto envFilter = environmentVariable("EZMU_GPU");
    const std::string filter = toLowerAscii(envFilter ? *envFilter : desc.adapterNameFilter);

    const AdapterCandidate* best = nullptr;
    std::string report;
    for (const auto& candidate : candidates) {
        std::string reason = candidate.rejection;
        if (reason.empty() && !filter.empty() &&
            !toLowerAscii(candidate.info.name).contains(filter)) {
            reason = std::format("name does not match filter '{}'", filter);
        }
        report += std::format("\n  - {} ({}): {}", candidate.info.name,
                              toString(candidate.info.type), reason.empty() ? "usable" : reason);
        if (!reason.empty()) {
            continue;
        }
        if (best == nullptr || rank(candidate.info.type, desc.adapterPreference) <
                                   rank(best->info.type, desc.adapterPreference)) {
            best = &candidate;
        }
    }
    if (best == nullptr) {
        return foundation::makeError(
            foundation::ErrorCode::kUnsupported,
            std::format("Device::create: no usable Vulkan 1.3 adapter{}",
                        candidates.empty() ? std::string(" (no adapters found)") : report));
    }
    return *best;
}

} // namespace detail

} // namespace ezmu::rhi

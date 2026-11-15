#pragma once

// Private Vulkan include for the rhi module. Every source that needs Vulkan includes this file,
// never <vulkan/vulkan.h> directly: volk provides the function pointers (VK_NO_PROTOTYPES).

#include <ezmu/foundation/result.hpp>

#include <volk.h>
// VMA after volk: it uses the Vulkan types volk declares (VK_NO_PROTOTYPES).
#include <vk_mem_alloc.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace ezmu::rhi::detail {

/// Stable name of a VkResult (e.g. "VK_ERROR_DEVICE_LOST").
[[nodiscard]] std::string_view toString(VkResult result) noexcept;

/// Error for a failed Vulkan call: the operation, the call and the VkResult name.
[[nodiscard]] foundation::Error vkError(VkResult result, std::string_view what);

// Calls that initialize the Vulkan loader and driver. They are separate, non-inline functions so
// that LeakSanitizer can suppress the one-time allocations drivers make here
// (.github/lsan-suppressions.txt): the driver library is unloaded before LSan reports, so its
// own frames cannot be symbolized. Use them instead of calling the Vulkan functions directly.
[[nodiscard]] VkResult driverCreateInstance(const VkInstanceCreateInfo& info,
                                            VkInstance& instance) noexcept;
[[nodiscard]] VkResult driverEnumeratePhysicalDevices(VkInstance instance, std::uint32_t& count,
                                                      VkPhysicalDevice* devices) noexcept;

/// View of the NUL-terminated string stored in a fixed-size Vulkan char array.
[[nodiscard]] inline std::string_view fixedString(std::span<const char> text) noexcept {
    const auto end = std::ranges::find(text, '\0');
    return {text.data(), static_cast<std::size_t>(end - text.begin())};
}

/// A Vulkan handle as the 64-bit value debug utils expect (handles are pointers on 64-bit
/// platforms and integers otherwise).
template <typename T>
[[nodiscard]] std::uint64_t rawHandle(T handle) noexcept {
    if constexpr (std::is_pointer_v<T>) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): opaque handle value.
        return reinterpret_cast<std::uint64_t>(handle);
    } else {
        return static_cast<std::uint64_t>(handle);
    }
}

/// Inverse of rawHandle(): a non-dispatchable handle (e.g. VkSurfaceKHR) from its 64-bit value.
template <typename T>
[[nodiscard]] T fromRawHandle(std::uint64_t value) noexcept {
    if constexpr (std::is_pointer_v<T>) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
        return reinterpret_cast<T>(value); // Opaque handle value from interop.
    } else {
        return static_cast<T>(value);
    }
}

/// Lowercase ASCII copy (adapter name matching).
[[nodiscard]] std::string toLowerAscii(std::string_view text);

} // namespace ezmu::rhi::detail

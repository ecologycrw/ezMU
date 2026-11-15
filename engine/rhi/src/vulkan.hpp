#pragma once

// Private Vulkan include for the rhi module. Every source that needs Vulkan includes this file,
// never <vulkan/vulkan.h> directly: volk provides the function pointers (VK_NO_PROTOTYPES).

#include <ezmu/foundation/result.hpp>

#include <volk.h>
// VMA after volk: it uses the Vulkan types volk declares (VK_NO_PROTOTYPES).
#include <vk_mem_alloc.h>

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace ezmu::rhi::detail {

/// Stable name of a VkResult (e.g. "VK_ERROR_DEVICE_LOST").
[[nodiscard]] std::string_view toString(VkResult result) noexcept;

/// Error for a failed Vulkan call: the operation, the call and the VkResult name.
[[nodiscard]] foundation::Error vkError(VkResult result, std::string_view what);

/// View of the NUL-terminated string stored in a fixed-size Vulkan char array.
[[nodiscard]] inline std::string_view fixedString(std::span<const char> text) noexcept {
    const auto end = std::ranges::find(text, '\0');
    return {text.data(), static_cast<std::size_t>(end - text.begin())};
}

/// Lowercase ASCII copy (adapter name matching).
[[nodiscard]] std::string toLowerAscii(std::string_view text);

} // namespace ezmu::rhi::detail

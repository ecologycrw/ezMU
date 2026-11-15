#pragma once

#include <ezmu/rhi/command_list.hpp>
#include <ezmu/rhi/device.hpp>
#include <ezmu/rhi/resources.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "handle_pool.hpp"
#include "vulkan.hpp"

namespace ezmu::rhi {

namespace detail {

struct TextureRecord {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE; ///< Null for transfer-only textures.
    VmaAllocation allocation = VK_NULL_HANDLE;
    foundation::Extent2D size{};
    Format format = Format::kUndefined;
    TextureUsage usage = TextureUsage::kNone;
    ResourceState state = ResourceState::kUndefined; ///< Tracked at record time.
};

struct BufferRecord {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    std::uint64_t size = 0;
    MemoryLocation location = MemoryLocation::kGpu;
    void* mapped = nullptr; ///< Persistent mapping of kUpload/kReadback buffers.
};

/// Command pool, command buffer and fence of Device::executeImmediate (created on first use).
struct ImmediateContext {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
};

[[nodiscard]] VkFormat toVkFormat(Format format) noexcept;

} // namespace detail

struct Device::Impl {
    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
    /// Destroys whatever was created, in reverse order; safe on partially created devices.
    ~Impl();

    /// Gives `object` a debug name (no-op without VK_EXT_debug_utils).
    void setDebugName(VkObjectType type, std::uint64_t object, std::string_view name) const;

    /// Creates whatever part of the immediate context is missing.
    [[nodiscard]] foundation::Result<> createImmediateContext();

    /// Records with `record`, submits and waits (Device::executeImmediate).
    [[nodiscard]] foundation::Result<>
    executeImmediate(const std::function<void(CommandList&)>& record);

    void destroyTexture(detail::TextureRecord& texture) const noexcept;
    void destroyBuffer(detail::BufferRecord& buffer) const noexcept;

    bool loaderInitialized = false;
    bool debugUtils = false; ///< VK_EXT_debug_utils enabled (names and labels).
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    std::uint32_t graphicsQueueFamily = 0;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    std::uint32_t maxImageDimension2D = 0;

    detail::HandlePool<TextureTag, detail::TextureRecord> textures;
    detail::HandlePool<BufferTag, detail::BufferRecord> buffers;
    detail::ImmediateContext immediate;
    bool recording = false; ///< Inside executeImmediate().

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

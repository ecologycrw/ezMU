// Textures and buffers: creation through VMA, destruction, handle resolution.

#include <ezmu/foundation/assert.hpp>
#include <ezmu/rhi/resources.hpp>

#include <cstddef>
#include <format>
#include <string>
#include <utility>

#include "device_impl.hpp"

namespace ezmu::rhi {

std::string_view toString(Format format) noexcept {
    switch (format) {
    case Format::kUndefined:
        return "Undefined";
    case Format::kRgba8Unorm:
        return "Rgba8Unorm";
    case Format::kRgba8Srgb:
        return "Rgba8Srgb";
    case Format::kBgra8Unorm:
        return "Bgra8Unorm";
    case Format::kBgra8Srgb:
        return "Bgra8Srgb";
    case Format::kRgba16Float:
        return "Rgba16Float";
    case Format::kDepth32Float:
        return "Depth32Float";
    }
    return "Undefined";
}

namespace detail {

VkFormat toVkFormat(Format format) noexcept {
    switch (format) {
    case Format::kRgba8Unorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::kRgba8Srgb:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::kBgra8Unorm:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::kBgra8Srgb:
        return VK_FORMAT_B8G8R8A8_SRGB;
    case Format::kRgba16Float:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::kDepth32Float:
        return VK_FORMAT_D32_SFLOAT;
    case Format::kUndefined:
        return VK_FORMAT_UNDEFINED;
    }
    return VK_FORMAT_UNDEFINED;
}

} // namespace detail

namespace {

VkImageUsageFlags toVkUsage(TextureUsage usage) noexcept {
    VkImageUsageFlags flags = 0;
    if (hasAny(usage, TextureUsage::kRenderTarget)) {
        flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if (hasAny(usage, TextureUsage::kDepthStencil)) {
        flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if (hasAny(usage, TextureUsage::kSampled)) {
        flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (hasAny(usage, TextureUsage::kTransferSrc)) {
        flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    if (hasAny(usage, TextureUsage::kTransferDst)) {
        flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    return flags;
}

/// Name for debug utils: the caller's, or a generated one.
std::string objectName(std::string_view debugName, std::string_view kind, std::uint32_t index) {
    return debugName.empty() ? std::format("{} #{}", kind, index) : std::string(debugName);
}

foundation::Result<> validate(const TextureDesc& desc, std::uint32_t maxDimension) {
    using foundation::ErrorCode;
    if (desc.size.isEmpty()) {
        return foundation::makeError(ErrorCode::kInvalidArgument,
                                     "Device::createTexture: size must not be empty");
    }
    if (desc.format == Format::kUndefined) {
        return foundation::makeError(ErrorCode::kInvalidArgument,
                                     "Device::createTexture: format is kUndefined");
    }
    if (desc.usage == TextureUsage::kNone) {
        return foundation::makeError(ErrorCode::kInvalidArgument,
                                     "Device::createTexture: usage is kNone");
    }
    const bool depth = isDepthFormat(desc.format);
    if ((depth && hasAny(desc.usage, TextureUsage::kRenderTarget)) ||
        (!depth && hasAny(desc.usage, TextureUsage::kDepthStencil))) {
        return foundation::makeError(
            ErrorCode::kInvalidArgument,
            std::format("Device::createTexture: usage does not fit format {}",
                        toString(desc.format)));
    }
    if (desc.size.width > maxDimension || desc.size.height > maxDimension) {
        return foundation::makeError(
            ErrorCode::kUnsupported,
            std::format("Device::createTexture: {}x{} exceeds the adapter limit of {}",
                        desc.size.width, desc.size.height, maxDimension));
    }
    return {};
}

} // namespace

void Device::Impl::destroyTexture(detail::TextureRecord& texture) const noexcept {
    if (texture.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, texture.view, nullptr);
    }
    if (texture.image != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, texture.image, texture.allocation);
    }
    texture = {};
}

void Device::Impl::destroyBuffer(detail::BufferRecord& buffer) const noexcept {
    if (buffer.buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
    }
    buffer = {};
}

foundation::Result<TextureHandle> Device::createTexture(const TextureDesc& desc) {
    Impl& impl = *m_impl;
    if (auto valid = validate(desc, impl.maxImageDimension2D); !valid) {
        return std::unexpected(std::move(valid.error()));
    }
    const VkFormat format = detail::toVkFormat(desc.format);
    const VkImageUsageFlags usage = toVkUsage(desc.usage);

    VkImageFormatProperties properties{};
    if (const VkResult result = vkGetPhysicalDeviceImageFormatProperties(
            impl.physicalDevice, format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0,
            &properties);
        result != VK_SUCCESS) {
        if (result == VK_ERROR_FORMAT_NOT_SUPPORTED) {
            return foundation::makeError(
                foundation::ErrorCode::kUnsupported,
                std::format("Device::createTexture: format {} does not support the requested "
                            "usage on this adapter",
                            toString(desc.format)));
        }
        return std::unexpected(detail::vkError(result, "vkGetPhysicalDeviceImageFormatProperties"));
    }
    if (desc.size.width > properties.maxExtent.width ||
        desc.size.height > properties.maxExtent.height) {
        return foundation::makeError(foundation::ErrorCode::kUnsupported,
                                     std::format("Device::createTexture: {}x{} is too large for "
                                                 "format {} on this adapter",
                                                 desc.size.width, desc.size.height,
                                                 toString(desc.format)));
    }

    const VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {.width = desc.size.width, .height = desc.size.height, .depth = 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    detail::TextureRecord texture{.size = desc.size, .format = desc.format, .usage = desc.usage};
    if (const VkResult result = vmaCreateImage(impl.allocator, &imageInfo, &allocationInfo,
                                               &texture.image, &texture.allocation, nullptr);
        result != VK_SUCCESS) {
        return std::unexpected(detail::vkError(result, "Device::createTexture: vmaCreateImage"));
    }

    // Image views exist only for usages that need one (attachments, sampling).
    if (hasAny(desc.usage, TextureUsage::kRenderTarget | TextureUsage::kDepthStencil |
                               TextureUsage::kSampled)) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = texture.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange = {
            .aspectMask = isDepthFormat(desc.format)
                              ? VkImageAspectFlags{VK_IMAGE_ASPECT_DEPTH_BIT}
                              : VkImageAspectFlags{VK_IMAGE_ASPECT_COLOR_BIT},
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
        if (const VkResult result =
                vkCreateImageView(impl.device, &viewInfo, nullptr, &texture.view);
            result != VK_SUCCESS) {
            texture.view = VK_NULL_HANDLE;
            impl.destroyTexture(texture);
            return std::unexpected(
                detail::vkError(result, "Device::createTexture: vkCreateImageView"));
        }
    }

    VkImage image = texture.image;
    VkImageView view = texture.view;
    VmaAllocation allocation = texture.allocation;
    const TextureHandle handle = impl.textures.insert(texture);
    const std::string name = objectName(desc.debugName, "texture", handle.index());
    impl.setDebugName(VK_OBJECT_TYPE_IMAGE, detail::rawHandle(image), name);
    if (view != VK_NULL_HANDLE) {
        impl.setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, detail::rawHandle(view), name + " (view)");
    }
    vmaSetAllocationName(impl.allocator, allocation, name.c_str());
    return handle;
}

foundation::Result<BufferHandle> Device::createBuffer(const BufferDesc& desc) {
    Impl& impl = *m_impl;
    if (desc.size == 0) {
        return foundation::makeError(foundation::ErrorCode::kInvalidArgument,
                                     "Device::createBuffer: size must be > 0");
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = desc.size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // CPU-visible buffers are coherent so that mappedData() needs no flush/invalidate calls.
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
    switch (desc.location) {
    case MemoryLocation::kGpu:
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        break;
    case MemoryLocation::kUpload:
        allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                               VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocationInfo.requiredFlags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        break;
    case MemoryLocation::kReadback:
        allocationInfo.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocationInfo.requiredFlags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        allocationInfo.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        break;
    }

    detail::BufferRecord buffer{.size = desc.size, .location = desc.location};
    VmaAllocationInfo allocated{};
    if (const VkResult result = vmaCreateBuffer(impl.allocator, &bufferInfo, &allocationInfo,
                                                &buffer.buffer, &buffer.allocation, &allocated);
        result != VK_SUCCESS) {
        return std::unexpected(detail::vkError(result, "Device::createBuffer: vmaCreateBuffer"));
    }
    buffer.mapped = desc.location == MemoryLocation::kGpu ? nullptr : allocated.pMappedData;
    const bool mappedAsRequested =
        desc.location == MemoryLocation::kGpu || buffer.mapped != nullptr;
    EZMU_ASSERT(mappedAsRequested);

    VkBuffer vkBuffer = buffer.buffer;
    VmaAllocation allocation = buffer.allocation;
    const BufferHandle handle = impl.buffers.insert(buffer);
    const std::string name = objectName(desc.debugName, "buffer", handle.index());
    impl.setDebugName(VK_OBJECT_TYPE_BUFFER, detail::rawHandle(vkBuffer), name);
    vmaSetAllocationName(impl.allocator, allocation, name.c_str());
    return handle;
}

void Device::destroy(TextureHandle texture) {
    if (!texture.isValid()) {
        return;
    }
    EZMU_ASSERT_MSG(!m_impl->recording, "Device::destroy called while recording");
    auto record = m_impl->textures.remove(texture);
    EZMU_ASSERT_MSG(record.has_value(), "Device::destroy: stale TextureHandle");
    if (record) {
        m_impl->destroyTexture(*record);
    }
}

void Device::destroy(BufferHandle buffer) {
    if (!buffer.isValid()) {
        return;
    }
    EZMU_ASSERT_MSG(!m_impl->recording, "Device::destroy called while recording");
    auto record = m_impl->buffers.remove(buffer);
    EZMU_ASSERT_MSG(record.has_value(), "Device::destroy: stale BufferHandle");
    if (record) {
        m_impl->destroyBuffer(*record);
    }
}

std::span<std::byte> Device::mappedData(BufferHandle buffer) {
    const detail::BufferRecord* record = m_impl->buffers.get(buffer);
    EZMU_ASSERT_MSG(record != nullptr, "Device::mappedData: invalid or stale BufferHandle");
    if (record == nullptr || record->mapped == nullptr) {
        return {};
    }
    return {static_cast<std::byte*>(record->mapped), static_cast<std::size_t>(record->size)};
}

} // namespace ezmu::rhi

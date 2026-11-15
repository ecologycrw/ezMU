// Command recording (synchronization2 barriers, dynamic rendering, copies, debug labels), the
// immediate submission path and blocking texture readback.

#include <ezmu/foundation/assert.hpp>
#include <ezmu/rhi/command_list.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include "device_impl.hpp"

namespace ezmu::rhi {

namespace {

/// Layout, pipeline stages and accesses a resource state stands for.
struct StateInfo {
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
    bool writable = false;
};

StateInfo stateInfo(ResourceState state) noexcept {
    switch (state) {
    case ResourceState::kUndefined:
        // As a source: no accesses to wait for, but all stages, so that a transition after a
        // semaphore wait (swapchain acquire) is ordered after it.
        return {.layout = VK_IMAGE_LAYOUT_UNDEFINED,
                .stages = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    case ResourceState::kRenderTarget:
        return {.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .stages = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .access =
                    VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                .writable = true};
    case ResourceState::kDepthWrite:
        return {.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                .stages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                .access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .writable = true};
    case ResourceState::kShaderRead:
        return {.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .stages = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                          VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT};
    case ResourceState::kTransferSrc:
        return {.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .stages = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                .access = VK_ACCESS_2_TRANSFER_READ_BIT};
    case ResourceState::kTransferDst:
        return {.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .stages = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                .access = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .writable = true};
    case ResourceState::kPresent:
        // Presentation is ordered by semaphores; all stages keep the chain with their waits.
        return {.layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .stages = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    }
    return {};
}

VkImageAspectFlags aspectOf(Format format) noexcept {
    return isDepthFormat(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

VkAttachmentLoadOp toVk(LoadOp op) noexcept {
    switch (op) {
    case LoadOp::kLoad:
        return VK_ATTACHMENT_LOAD_OP_LOAD;
    case LoadOp::kClear:
        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case LoadOp::kDontCare:
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

VkAttachmentStoreOp toVk(StoreOp op) noexcept {
    return op == StoreOp::kStore ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

bool contains(const foundation::Extent2D& size, const foundation::Rect2D& area) noexcept {
    return area.offset.x >= 0 && area.offset.y >= 0 &&
           std::uint64_t{static_cast<std::uint32_t>(area.offset.x)} + area.extent.width <=
               size.width &&
           std::uint64_t{static_cast<std::uint32_t>(area.offset.y)} + area.extent.height <=
               size.height;
}

/// Fills `info` for `color`. An empty `area` becomes the whole of the first attachment. Returns
/// false (after an assertion) when the attachment cannot be used.
bool describeAttachment(const detail::TextureRecord* record, const ColorAttachment& color,
                        bool first, foundation::Rect2D& area, VkRenderingAttachmentInfo& info) {
    EZMU_ASSERT_MSG(record != nullptr,
                    "CommandList::beginRendering: invalid or stale TextureHandle");
    if (record == nullptr) {
        return false;
    }
    if (first && area.extent.isEmpty()) {
        area = {.offset = {}, .extent = record->size};
    }
    const bool renderTarget =
        record->view != VK_NULL_HANDLE && hasAny(record->usage, TextureUsage::kRenderTarget);
    const bool inState = record->state == ResourceState::kRenderTarget;
    const bool inside = contains(record->size, area);
    EZMU_ASSERT_MSG(renderTarget, "CommandList::beginRendering: texture lacks kRenderTarget usage");
    EZMU_ASSERT_MSG(inState,
                    "CommandList::beginRendering: attachment is not in kRenderTarget state");
    EZMU_ASSERT_MSG(inside, "CommandList::beginRendering: area outside an attachment");
    if (!renderTarget || !inState || !inside) {
        return false;
    }

    info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    info.imageView = record->view;
    info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    info.loadOp = toVk(color.load);
    info.storeOp = toVk(color.store);
    std::ranges::copy(color.clearColor, std::begin(info.clearValue.color.float32));
    return true;
}

} // namespace

CommandList::CommandList(Device::Impl& device, void* commandBuffer) noexcept
    : m_device(&device), m_commandBuffer(commandBuffer) {}

void CommandList::transition(TextureHandle texture, ResourceState newState) {
    EZMU_ASSERT_MSG(newState != ResourceState::kUndefined,
                    "CommandList::transition: kUndefined is not a target state");
    EZMU_ASSERT_MSG(!m_insideRendering, "CommandList::transition inside a rendering pass");
    detail::TextureRecord* record = m_device->textures.get(texture);
    EZMU_ASSERT_MSG(record != nullptr, "CommandList::transition: invalid or stale TextureHandle");
    if (record == nullptr || newState == ResourceState::kUndefined || m_insideRendering) {
        return;
    }
    const StateInfo from = stateInfo(record->state);
    const StateInfo to = stateInfo(newState);
    if (record->state == newState && !to.writable) {
        return; // Read-only states need no barrier among themselves.
    }

    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = from.stages;
    barrier.srcAccessMask = from.access;
    barrier.dstStageMask = to.stages;
    barrier.dstAccessMask = to.access;
    barrier.oldLayout = from.layout;
    barrier.newLayout = to.layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = record->image;
    barrier.subresourceRange = {.aspectMask = aspectOf(record->format),
                                .baseMipLevel = 0,
                                .levelCount = 1,
                                .baseArrayLayer = 0,
                                .layerCount = 1};

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(static_cast<VkCommandBuffer>(m_commandBuffer), &dependency);
    record->state = newState;
}

void CommandList::beginRendering(const RenderingDesc& desc) {
    EZMU_ASSERT_MSG(!m_insideRendering, "CommandList::beginRendering: passes do not nest");
    const bool validCount =
        !desc.colorAttachments.empty() && desc.colorAttachments.size() <= kMaxColorAttachments;
    EZMU_ASSERT_MSG(validCount,
                    "CommandList::beginRendering: 1..kMaxColorAttachments color attachments");
    if (m_insideRendering || !validCount) {
        return;
    }

    std::array<VkRenderingAttachmentInfo, kMaxColorAttachments> attachments{};
    foundation::Rect2D area = desc.area;
    std::size_t count = 0;
    for (const ColorAttachment& color : desc.colorAttachments) {
        if (!describeAttachment(m_device->textures.get(color.texture), color, count == 0, area,
                                attachments.at(count))) {
            return;
        }
        ++count;
    }

    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea = {.offset = {.x = area.offset.x, .y = area.offset.y},
                            .extent = {.width = area.extent.width, .height = area.extent.height}};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = static_cast<std::uint32_t>(desc.colorAttachments.size());
    rendering.pColorAttachments = attachments.data();
    vkCmdBeginRendering(static_cast<VkCommandBuffer>(m_commandBuffer), &rendering);
    m_insideRendering = true;
}

void CommandList::endRendering() {
    EZMU_ASSERT_MSG(m_insideRendering, "CommandList::endRendering without beginRendering");
    if (!m_insideRendering) {
        return;
    }
    vkCmdEndRendering(static_cast<VkCommandBuffer>(m_commandBuffer));
    m_insideRendering = false;
}

void CommandList::copyTextureToBuffer(TextureHandle src, BufferHandle dst) {
    EZMU_ASSERT_MSG(!m_insideRendering, "CommandList::copyTextureToBuffer inside rendering");
    const detail::TextureRecord* texture = m_device->textures.get(src);
    const detail::BufferRecord* buffer = m_device->buffers.get(dst);
    const bool resolved = texture != nullptr && buffer != nullptr;
    EZMU_ASSERT_MSG(resolved, "CommandList::copyTextureToBuffer: invalid or stale handle");
    if (m_insideRendering || !resolved) {
        return;
    }
    const std::uint64_t bytes = texture->size.area() * bytesPerPixel(texture->format);
    EZMU_ASSERT_MSG(texture->state == ResourceState::kTransferSrc,
                    "CommandList::copyTextureToBuffer: source is not in kTransferSrc state");
    EZMU_ASSERT_MSG(buffer->size >= bytes,
                    "CommandList::copyTextureToBuffer: destination buffer too small");
    if (texture->state != ResourceState::kTransferSrc || buffer->size < bytes) {
        return;
    }
    auto* commandBuffer = static_cast<VkCommandBuffer>(m_commandBuffer);

    // Orders the copy after earlier transfers that read or wrote the buffer.
    VkBufferMemoryBarrier2 before{};
    before.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    before.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    before.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
    before.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    before.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    before.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before.buffer = buffer->buffer;
    before.offset = 0;
    before.size = VK_WHOLE_SIZE;
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &before;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);

    VkBufferImageCopy2 region{};
    region.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
    region.bufferOffset = 0;
    region.bufferRowLength = 0; // Tightly packed.
    region.bufferImageHeight = 0;
    region.imageSubresource = {.aspectMask = aspectOf(texture->format),
                               .mipLevel = 0,
                               .baseArrayLayer = 0,
                               .layerCount = 1};
    region.imageExtent = {.width = texture->size.width, .height = texture->size.height, .depth = 1};
    VkCopyImageToBufferInfo2 copy{};
    copy.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2;
    copy.srcImage = texture->image;
    copy.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    copy.dstBuffer = buffer->buffer;
    copy.regionCount = 1;
    copy.pRegions = &region;
    vkCmdCopyImageToBuffer2(commandBuffer, &copy);

    if (buffer->location == MemoryLocation::kReadback) {
        // Fences only cover device accesses: make the copy visible to the host explicitly.
        VkBufferMemoryBarrier2 after = before;
        after.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        after.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        after.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        after.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        dependency.pBufferMemoryBarriers = &after;
        vkCmdPipelineBarrier2(commandBuffer, &dependency);
    }
}

void CommandList::beginDebugLabel(std::string_view name) {
    ++m_labelDepth;
    if (!m_device->debugUtils || vkCmdBeginDebugUtilsLabelEXT == nullptr) {
        return;
    }
    std::array<char, 128> text{}; // NUL-terminated copy without allocating.
    const std::size_t length = std::min(name.size(), text.size() - 1);
    std::copy_n(name.begin(), length, text.begin());
    VkDebugUtilsLabelEXT label{};
    label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    label.pLabelName = text.data();
    vkCmdBeginDebugUtilsLabelEXT(static_cast<VkCommandBuffer>(m_commandBuffer), &label);
}

void CommandList::endDebugLabel() {
    EZMU_ASSERT_MSG(m_labelDepth > 0, "CommandList::endDebugLabel without beginDebugLabel");
    if (m_labelDepth == 0) {
        return;
    }
    --m_labelDepth;
    if (m_device->debugUtils && vkCmdEndDebugUtilsLabelEXT != nullptr) {
        vkCmdEndDebugUtilsLabelEXT(static_cast<VkCommandBuffer>(m_commandBuffer));
    }
}

foundation::Result<> Device::Impl::createImmediateContext() {
    if (immediate.pool == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = graphicsQueueFamily;
        if (const VkResult result =
                vkCreateCommandPool(device, &poolInfo, nullptr, &immediate.pool);
            result != VK_SUCCESS) {
            immediate.pool = VK_NULL_HANDLE;
            return std::unexpected(detail::vkError(result, "vkCreateCommandPool"));
        }
        setDebugName(VK_OBJECT_TYPE_COMMAND_POOL, detail::rawHandle(immediate.pool),
                     "immediate command pool");
    }
    if (immediate.commandBuffer == VK_NULL_HANDLE) {
        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool = immediate.pool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        if (const VkResult result =
                vkAllocateCommandBuffers(device, &allocateInfo, &immediate.commandBuffer);
            result != VK_SUCCESS) {
            immediate.commandBuffer = VK_NULL_HANDLE;
            return std::unexpected(detail::vkError(result, "vkAllocateCommandBuffers"));
        }
        setDebugName(VK_OBJECT_TYPE_COMMAND_BUFFER, detail::rawHandle(immediate.commandBuffer),
                     "immediate command buffer");
    }
    if (immediate.fence == VK_NULL_HANDLE) {
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (const VkResult result = vkCreateFence(device, &fenceInfo, nullptr, &immediate.fence);
            result != VK_SUCCESS) {
            immediate.fence = VK_NULL_HANDLE;
            return std::unexpected(detail::vkError(result, "vkCreateFence"));
        }
        setDebugName(VK_OBJECT_TYPE_FENCE, detail::rawHandle(immediate.fence), "immediate fence");
    }
    return {};
}

foundation::Result<>
Device::Impl::executeImmediate(const std::function<void(CommandList&)>& record) {
    EZMU_ASSERT_MSG(!recording, "Device::executeImmediate must not be nested");
    EZMU_ASSERT_MSG(openFrame == nullptr, "Device::executeImmediate while a frame is recorded");
    if (recording || openFrame != nullptr) {
        return foundation::makeError(foundation::ErrorCode::kInvalidArgument,
                                     "Device::executeImmediate: nested call or open frame");
    }
    if (auto created = createImmediateContext(); !created) {
        return created;
    }
    if (const VkResult result = vkResetCommandPool(device, immediate.pool, 0);
        result != VK_SUCCESS) {
        return std::unexpected(detail::vkError(result, "vkResetCommandPool"));
    }
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (const VkResult result = vkBeginCommandBuffer(immediate.commandBuffer, &beginInfo);
        result != VK_SUCCESS) {
        return std::unexpected(detail::vkError(result, "vkBeginCommandBuffer"));
    }

    CommandList list(*this, immediate.commandBuffer);
    recording = true;
    record(list);
    recording = false;
    // Keep the command buffer valid even when the caller left a pass or label open.
    closeRecording(list);

    if (const VkResult result = vkEndCommandBuffer(immediate.commandBuffer); result != VK_SUCCESS) {
        return std::unexpected(detail::vkError(result, "vkEndCommandBuffer"));
    }
    VkCommandBufferSubmitInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandInfo.commandBuffer = immediate.commandBuffer;
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commandInfo;
    if (const VkResult result = vkQueueSubmit2(graphicsQueue, 1, &submit, immediate.fence);
        result != VK_SUCCESS) {
        return std::unexpected(detail::vkError(result, "vkQueueSubmit2"));
    }
    if (const VkResult result = vkWaitForFences(device, 1, &immediate.fence, VK_TRUE,
                                                std::numeric_limits<std::uint64_t>::max());
        result != VK_SUCCESS) {
        return std::unexpected(detail::vkError(result, "vkWaitForFences"));
    }
    if (const VkResult result = vkResetFences(device, 1, &immediate.fence); result != VK_SUCCESS) {
        return std::unexpected(detail::vkError(result, "vkResetFences"));
    }
    return {};
}

foundation::Result<> Device::executeImmediate(const std::function<void(CommandList&)>& record) {
    return m_impl->executeImmediate(record);
}

foundation::Result<foundation::Image> Device::readTexture(TextureHandle texture) {
    using foundation::ErrorCode;
    const bool idle = !m_impl->recording && m_impl->openFrame == nullptr;
    EZMU_ASSERT_MSG(idle, "Device::readTexture called while recording or inside a frame");
    const detail::TextureRecord* record = m_impl->textures.get(texture);
    EZMU_ASSERT_MSG(record != nullptr, "Device::readTexture: invalid or stale TextureHandle");
    if (record == nullptr || !idle) {
        return foundation::makeError(ErrorCode::kInvalidArgument,
                                     "Device::readTexture: invalid or stale handle, or called "
                                     "while recording or inside a frame");
    }
    bool swizzle = false;
    switch (record->format) {
    case Format::kRgba8Unorm:
    case Format::kRgba8Srgb:
        break;
    case Format::kBgra8Unorm:
    case Format::kBgra8Srgb:
        swizzle = true;
        break;
    default:
        return foundation::makeError(
            ErrorCode::kUnsupported,
            std::format("Device::readTexture: format {} cannot be read back as RGBA8",
                        toString(record->format)));
    }
    if (!hasAny(record->usage, TextureUsage::kTransferSrc)) {
        return foundation::makeError(ErrorCode::kInvalidArgument,
                                     "Device::readTexture: texture lacks kTransferSrc usage");
    }
    if (record->state == ResourceState::kUndefined) {
        return foundation::makeError(ErrorCode::kInvalidArgument,
                                     "Device::readTexture: texture was never written");
    }
    const foundation::Extent2D size = record->size;
    const ResourceState previous = record->state;
    const std::uint64_t bytes = size.area() * foundation::Image::kBytesPerPixel;

    auto staging = createBuffer(
        {.size = bytes, .location = MemoryLocation::kReadback, .debugName = "readTexture staging"});
    if (!staging) {
        return std::unexpected(std::move(staging.error()));
    }
    const BufferHandle buffer = *staging;
    auto executed = executeImmediate([&](CommandList& list) {
        list.transition(texture, ResourceState::kTransferSrc);
        list.copyTextureToBuffer(texture, buffer);
        list.transition(texture, previous);
    });
    if (!executed) {
        destroy(buffer);
        return std::unexpected(std::move(executed.error()));
    }

    const std::span<const std::byte> data = mappedData(buffer);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(bytes));
    std::memcpy(pixels.data(), data.data(), pixels.size());
    destroy(buffer);
    if (swizzle) {
        for (std::size_t i = 0; i < pixels.size(); i += foundation::Image::kBytesPerPixel) {
            std::swap(pixels.at(i), pixels.at(i + 2));
        }
    }
    return foundation::Image::fromBytes(size, std::move(pixels));
}

} // namespace ezmu::rhi

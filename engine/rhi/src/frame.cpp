// Frames in flight (timeline-semaphore pacing, per-slot command pools), submission and
// presentation, and deferred destruction.

#include <ezmu/foundation/assert.hpp>
#include <ezmu/rhi/frame.hpp>

#include <algorithm>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <utility>
#include <variant>

#include "device_impl.hpp"
#include "swapchain_impl.hpp"

namespace ezmu::rhi {

Frame::Frame(Device::Impl& device, void* commandBuffer, std::uint32_t slot) noexcept
    : m_commandList(device, commandBuffer), m_slot(slot) {}

namespace {

VkSemaphoreSubmitInfo semaphoreInfo(VkSemaphore semaphore, std::uint64_t value) noexcept {
    return {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext = nullptr,
            .semaphore = semaphore,
            .value = value,
            // Transitions out of kUndefined use ALL_COMMANDS as source stage, so they chain
            // with this wait; signals cover all work of the frame.
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .deviceIndex = 0};
}

} // namespace

foundation::Result<> Device::Impl::createFrameResources() {
    VkSemaphoreTypeCreateInfo type{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                                   .pNext = nullptr,
                                   .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
                                   .initialValue = 0};
    const VkSemaphoreCreateInfo semaphore{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &type, .flags = 0};
    if (const VkResult result = vkCreateSemaphore(device, &semaphore, nullptr, &timeline);
        result != VK_SUCCESS) {
        timeline = VK_NULL_HANDLE;
        return std::unexpected(detail::vkError(result, "vkCreateSemaphore (frame timeline)"));
    }
    setDebugName(VK_OBJECT_TYPE_SEMAPHORE, detail::rawHandle(timeline), "frame timeline");

    frameSlots.resize(framesInFlight);
    for (std::uint32_t index = 0; index < framesInFlight; ++index) {
        detail::FrameSlot& slot = frameSlots.at(index);
        const VkCommandPoolCreateInfo poolInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                               .pNext = nullptr,
                                               .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
                                               .queueFamilyIndex = graphicsQueueFamily};
        if (const VkResult result = vkCreateCommandPool(device, &poolInfo, nullptr, &slot.pool);
            result != VK_SUCCESS) {
            slot.pool = VK_NULL_HANDLE;
            return std::unexpected(detail::vkError(result, "vkCreateCommandPool (frame)"));
        }
        const VkCommandBufferAllocateInfo allocateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = slot.pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1};
        if (const VkResult result =
                vkAllocateCommandBuffers(device, &allocateInfo, &slot.commandBuffer);
            result != VK_SUCCESS) {
            slot.commandBuffer = VK_NULL_HANDLE;
            return std::unexpected(detail::vkError(result, "vkAllocateCommandBuffers (frame)"));
        }
        setDebugName(VK_OBJECT_TYPE_COMMAND_POOL, detail::rawHandle(slot.pool),
                     std::format("frame slot {} command pool", index));
        setDebugName(VK_OBJECT_TYPE_COMMAND_BUFFER, detail::rawHandle(slot.commandBuffer),
                     std::format("frame slot {} command buffer", index));
        // NOLINTNEXTLINE(modernize-make-unique): the constructor is private.
        slot.frame = std::unique_ptr<Frame>(new Frame(*this, slot.commandBuffer, index));
    }
    return {};
}

std::uint64_t Device::Impl::refreshCompletedValue() noexcept {
    std::uint64_t value = 0;
    if (timeline != VK_NULL_HANDLE &&
        vkGetSemaphoreCounterValue(device, timeline, &value) == VK_SUCCESS) {
        completedValue = std::max(completedValue, value);
    }
    return completedValue;
}

void Device::Impl::destroyRecord(
    std::variant<detail::TextureRecord, detail::BufferRecord>& record) const noexcept {
    if (auto* texture = std::get_if<detail::TextureRecord>(&record)) {
        destroyTexture(*texture);
    } else if (auto* buffer = std::get_if<detail::BufferRecord>(&record)) {
        destroyBuffer(*buffer);
    }
}

void Device::Impl::retire(std::variant<detail::TextureRecord, detail::BufferRecord> record) {
    // Work that may use the object: every submitted frame, plus the frame being recorded.
    const std::uint64_t value = openFrame != nullptr ? nextFrameIndex : submittedValue;
    if (value <= completedValue || (openFrame == nullptr && value <= refreshCompletedValue())) {
        destroyRecord(record);
        return;
    }
    pendingDestructions.push_back({.value = value, .record = record});
}

void Device::Impl::collectDestructions(std::uint64_t completed) noexcept {
    while (!pendingDestructions.empty() && pendingDestructions.front().value <= completed) {
        destroyRecord(pendingDestructions.front().record);
        pendingDestructions.pop_front();
    }
}

void Device::Impl::closeRecording(CommandList& list) {
    EZMU_ASSERT_MSG(!list.m_insideRendering, "rendering pass left open");
    EZMU_ASSERT_MSG(list.m_labelDepth == 0, "debug label left open");
    if (list.m_insideRendering) {
        list.endRendering();
    }
    while (list.m_labelDepth > 0) {
        list.endDebugLabel();
    }
}

foundation::Result<Frame*> Device::Impl::beginFrame() {
    EZMU_ASSERT_MSG(openFrame == nullptr, "Device::beginFrame: the previous frame is not ended");
    EZMU_ASSERT_MSG(!recording, "Device::beginFrame inside executeImmediate");
    if (openFrame != nullptr || recording) {
        return foundation::makeError(foundation::ErrorCode::kInvalidArgument,
                                     "Device::beginFrame: a frame or immediate recording is open");
    }
    const std::uint64_t index = nextFrameIndex;
    const detail::FrameSlot& slot = frameSlots.at(static_cast<std::size_t>(index % framesInFlight));

    // Frame `index` reuses the slot of frame `index - framesInFlight`, which signals
    // `index - framesInFlight + 1`. Never wait for a value no submission will signal.
    if (index >= framesInFlight) {
        const std::uint64_t wanted = std::min(index - framesInFlight + 1, submittedValue);
        if (wanted > completedValue) {
            const VkSemaphoreWaitInfo wait{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                                           .pNext = nullptr,
                                           .flags = 0,
                                           .semaphoreCount = 1,
                                           .pSemaphores = &timeline,
                                           .pValues = &wanted};
            if (const VkResult result =
                    vkWaitSemaphores(device, &wait, std::numeric_limits<std::uint64_t>::max());
                result != VK_SUCCESS) {
                return std::unexpected(detail::vkError(result, "Device::beginFrame: wait"));
            }
            completedValue = wanted;
        }
    }
    collectDestructions(refreshCompletedValue());

    if (const VkResult result = vkResetCommandPool(device, slot.pool, 0); result != VK_SUCCESS) {
        return std::unexpected(detail::vkError(result, "Device::beginFrame: vkResetCommandPool"));
    }
    const VkCommandBufferBeginInfo beginInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                             .pNext = nullptr,
                                             .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                                             .pInheritanceInfo = nullptr};
    if (const VkResult result = vkBeginCommandBuffer(slot.commandBuffer, &beginInfo);
        result != VK_SUCCESS) {
        return std::unexpected(detail::vkError(result, "Device::beginFrame: vkBeginCommandBuffer"));
    }
    slot.frame->m_index = index;
    nextFrameIndex = index + 1;
    openFrame = slot.frame.get();
    acquisitions.clear();
    return openFrame;
}

foundation::Result<> Device::Impl::endFrame(Frame& frame) {
    const bool current = openFrame == &frame;
    EZMU_ASSERT_MSG(current, "Device::endFrame: not the frame returned by beginFrame");
    if (!current) {
        return foundation::makeError(foundation::ErrorCode::kInvalidArgument,
                                     "Device::endFrame: not the open frame");
    }
    CommandList& list = frame.commandList();
    closeRecording(list);
    for (const detail::Acquisition& acquisition : acquisitions) {
        const detail::TextureRecord* image = textures.get(acquisition.texture);
        const bool presentable = image != nullptr && image->state == ResourceState::kPresent;
        EZMU_ASSERT_MSG(presentable,
                        "Device::endFrame: an acquired swapchain image is not in kPresent state");
        if (!presentable) {
            list.transition(acquisition.texture, ResourceState::kPresent);
        }
    }

    // The frame is over from here on, whatever happens to the submission.
    openFrame = nullptr;
    const std::uint64_t value = frame.index() + 1;
    auto* commandBuffer = static_cast<VkCommandBuffer>(list.m_commandBuffer);
    if (const VkResult result = vkEndCommandBuffer(commandBuffer); result != VK_SUCCESS) {
        return std::unexpected(detail::vkError(result, "Device::endFrame: vkEndCommandBuffer"));
    }

    scratch.waits.clear();
    scratch.signals.clear();
    scratch.signals.push_back(semaphoreInfo(timeline, value));
    for (const detail::Acquisition& acquisition : acquisitions) {
        scratch.waits.push_back(semaphoreInfo(acquisition.acquired, 0));
        scratch.signals.push_back(semaphoreInfo(acquisition.renderFinished, 0));
    }
    const VkCommandBufferSubmitInfo commandInfo{.sType =
                                                    VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                                                .pNext = nullptr,
                                                .commandBuffer = commandBuffer,
                                                .deviceMask = 0};
    const VkSubmitInfo2 submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .pNext = nullptr,
        .flags = 0,
        .waitSemaphoreInfoCount = static_cast<std::uint32_t>(scratch.waits.size()),
        .pWaitSemaphoreInfos = scratch.waits.data(),
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandInfo,
        .signalSemaphoreInfoCount = static_cast<std::uint32_t>(scratch.signals.size()),
        .pSignalSemaphoreInfos = scratch.signals.data()};
    if (const VkResult result = vkQueueSubmit2(graphicsQueue, 1, &submit, VK_NULL_HANDLE);
        result != VK_SUCCESS) {
        for (const detail::Acquisition& acquisition : acquisitions) {
            acquisition.swapchain->acquired = false;
        }
        return std::unexpected(detail::vkError(result, "Device::endFrame: vkQueueSubmit2"));
    }
    submittedValue = value;
    return present();
}

foundation::Result<> Device::Impl::present() {
    if (acquisitions.empty()) {
        return {};
    }
    scratch.swapchains.clear();
    scratch.imageIndices.clear();
    scratch.presentWaits.clear();
    for (const detail::Acquisition& acquisition : acquisitions) {
        scratch.swapchains.push_back(acquisition.swapchain->swapchain);
        scratch.imageIndices.push_back(acquisition.imageIndex);
        scratch.presentWaits.push_back(acquisition.renderFinished);
        acquisition.swapchain->acquired = false;
    }
    scratch.presentResults.assign(acquisitions.size(), VK_SUCCESS);
    const VkPresentInfoKHR info{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = static_cast<std::uint32_t>(scratch.presentWaits.size()),
        .pWaitSemaphores = scratch.presentWaits.data(),
        .swapchainCount = static_cast<std::uint32_t>(scratch.swapchains.size()),
        .pSwapchains = scratch.swapchains.data(),
        .pImageIndices = scratch.imageIndices.data(),
        .pResults = scratch.presentResults.data()};
    static_cast<void>(vkQueuePresentKHR(graphicsQueue, &info)); // Per-swapchain results below.

    foundation::Result<> outcome{};
    for (std::size_t i = 0; i < acquisitions.size(); ++i) {
        const VkResult result = scratch.presentResults.at(i);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            acquisitions.at(i).swapchain->needsRecreate = true;
        } else if (result != VK_SUCCESS && outcome) {
            outcome = std::unexpected(detail::vkError(result, "Device::endFrame: present"));
        }
    }
    acquisitions.clear();
    return outcome;
}

foundation::Result<Frame*> Device::beginFrame() {
    return m_impl->beginFrame();
}

foundation::Result<> Device::endFrame(Frame& frame) {
    return m_impl->endFrame(frame);
}

std::uint32_t Device::framesInFlight() const noexcept {
    return m_impl->framesInFlight;
}

std::size_t Device::pendingDestructionCount() const noexcept {
    return m_impl->pendingDestructions.size();
}

} // namespace ezmu::rhi

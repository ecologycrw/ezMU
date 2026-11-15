#pragma once

#include <ezmu/rhi/command_list.hpp>
#include <ezmu/rhi/device.hpp>
#include <ezmu/rhi/frame.hpp>
#include <ezmu/rhi/resources.hpp>
#include <ezmu/rhi/swapchain.hpp>

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
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
    /// Image owned by a swapchain: only the view is ours, and Device::destroy() rejects it.
    bool swapchainImage = false;
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

/// A destroyed resource whose GPU objects are freed once the frame timeline reaches `value`.
struct PendingDestruction {
    std::uint64_t value = 0;
    std::variant<TextureRecord, BufferRecord> record{};
};

/// A swapchain image acquired during the frame being recorded.
struct Acquisition {
    Swapchain::Impl* swapchain = nullptr;
    TextureHandle texture{};
    std::uint32_t imageIndex = 0;
    VkSemaphore acquired = VK_NULL_HANDLE;       ///< Per frame slot; waited by the submission.
    VkSemaphore renderFinished = VK_NULL_HANDLE; ///< Per image; waited by the present.
};

/// Per-slot resources of frames in flight.
struct FrameSlot {
    VkCommandPool pool = VK_NULL_HANDLE; ///< Reset when the slot is reused.
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    std::unique_ptr<Frame> frame;
};

/// Scratch arrays of Device::endFrame, kept to avoid per-frame allocations.
struct SubmitScratch {
    std::vector<VkSemaphoreSubmitInfo> waits;
    std::vector<VkSemaphoreSubmitInfo> signals;
    std::vector<VkSwapchainKHR> swapchains;
    std::vector<std::uint32_t> imageIndices;
    std::vector<VkSemaphore> presentWaits;
    std::vector<VkResult> presentResults;
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

    /// Creates the frame timeline semaphore and the per-slot command pools.
    [[nodiscard]] foundation::Result<> createFrameResources();

    [[nodiscard]] foundation::Result<Frame*> beginFrame();
    [[nodiscard]] foundation::Result<> endFrame(Frame& frame);

    /// Ends an open pass and open labels so the command buffer can be ended (after an assert).
    static void closeRecording(CommandList& list);
    /// Presents the images acquired by the frame just submitted.
    [[nodiscard]] foundation::Result<> present();

    /// Frees `record` now when the GPU is done with every frame submitted so far (and no frame is
    /// being recorded), otherwise queues it for collectDestructions().
    void retire(std::variant<detail::TextureRecord, detail::BufferRecord> record);
    /// Frees pending destructions whose timeline value has been reached.
    void collectDestructions(std::uint64_t completed) noexcept;
    void
    destroyRecord(std::variant<detail::TextureRecord, detail::BufferRecord>& record) const noexcept;
    /// Current timeline value (completed frames); kept monotonic in `completedValue`.
    std::uint64_t refreshCompletedValue() noexcept;

    void destroyTexture(detail::TextureRecord& texture) const noexcept;
    void destroyBuffer(detail::BufferRecord& buffer) const noexcept;

    bool loaderInitialized = false;
    bool debugUtils = false;     ///< VK_EXT_debug_utils enabled (names and labels).
    bool surfaceSupport = false; ///< VK_KHR_surface + VK_KHR_swapchain enabled.
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

    // Frames in flight: frame n signals timeline value n + 1 when the GPU finishes it.
    std::uint32_t framesInFlight = 2;
    VkSemaphore timeline = VK_NULL_HANDLE;
    std::vector<detail::FrameSlot> frameSlots;
    std::uint64_t nextFrameIndex = 0;
    std::uint64_t submittedValue = 0;              ///< Value of the last submitted frame.
    std::uint64_t completedValue = 0;              ///< Last timeline value observed as reached.
    Frame* openFrame = nullptr;                    ///< Between beginFrame() and endFrame().
    std::vector<detail::Acquisition> acquisitions; ///< Of the open frame.
    detail::SubmitScratch scratch;
    std::deque<detail::PendingDestruction> pendingDestructions; ///< Ordered by value.
    std::uint32_t swapchainCount = 0;

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

// Swapchain: surface format / present mode selection, (re)creation, image acquisition.
// Presentation itself happens in Device::endFrame (frame.cpp).

#include <ezmu/foundation/assert.hpp>
#include <ezmu/rhi/frame.hpp>
#include <ezmu/rhi/swapchain.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "device_impl.hpp"
#include "swapchain_impl.hpp"

namespace ezmu::rhi {

std::string_view toString(PresentMode mode) noexcept {
    switch (mode) {
    case PresentMode::kFifo:
        return "Fifo";
    case PresentMode::kMailbox:
        return "Mailbox";
    case PresentMode::kImmediate:
        return "Immediate";
    }
    return "Fifo";
}

namespace {

VkPresentModeKHR toVk(PresentMode mode) noexcept {
    switch (mode) {
    case PresentMode::kFifo:
        return VK_PRESENT_MODE_FIFO_KHR;
    case PresentMode::kMailbox:
        return VK_PRESENT_MODE_MAILBOX_KHR;
    case PresentMode::kImmediate:
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

Format fromVk(VkFormat format) noexcept {
    switch (format) {
    case VK_FORMAT_B8G8R8A8_SRGB:
        return Format::kBgra8Srgb;
    case VK_FORMAT_R8G8B8A8_SRGB:
        return Format::kRgba8Srgb;
    case VK_FORMAT_B8G8R8A8_UNORM:
        return Format::kBgra8Unorm;
    case VK_FORMAT_R8G8B8A8_UNORM:
        return Format::kRgba8Unorm;
    default:
        return Format::kUndefined;
    }
}

/// Best surface format among the four 8-bit RGBA/BGRA formats, sRGB first when requested.
foundation::Result<VkSurfaceFormatKHR>
chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& available, bool srgb) {
    constexpr std::array kSrgbFirst{VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB,
                                    VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
    constexpr std::array kUnormFirst{VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
                                     VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB};
    const auto& order = srgb ? kSrgbFirst : kUnormFirst;
    for (const VkFormat wanted : order) {
        const auto found = std::ranges::find_if(available, [wanted](VkSurfaceFormatKHR format) {
            return format.format == wanted &&
                   format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        if (found != available.end()) {
            return *found;
        }
    }
    return foundation::makeError(foundation::ErrorCode::kUnsupported,
                                 "Device::createSwapchain: the surface supports none of the "
                                 "8-bit RGBA/BGRA formats in the sRGB color space");
}

template <typename T, typename Query>
foundation::Result<std::vector<T>> enumerate(Query query, std::string_view what) {
    std::uint32_t count = 0;
    if (const VkResult result = query(&count, nullptr); result != VK_SUCCESS) {
        return std::unexpected(detail::vkError(result, what));
    }
    std::vector<T> values(count);
    if (const VkResult result = query(&count, values.data());
        result != VK_SUCCESS && result != VK_INCOMPLETE) {
        return std::unexpected(detail::vkError(result, what));
    }
    values.resize(count);
    return values;
}

VkCompositeAlphaFlagBitsKHR chooseCompositeAlpha(VkCompositeAlphaFlagsKHR supported) noexcept {
    constexpr std::array kOrder{
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR};
    for (const VkCompositeAlphaFlagBitsKHR bit : kOrder) {
        if ((supported & static_cast<VkCompositeAlphaFlagsKHR>(bit)) != 0) {
            return bit;
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

/// Image size for `caps`: the surface's own size, or `requested` clamped to the limits.
foundation::Extent2D chooseExtent(const VkSurfaceCapabilitiesKHR& caps,
                                  foundation::Extent2D requested) noexcept {
    if (caps.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
        return {.width = caps.currentExtent.width, .height = caps.currentExtent.height};
    }
    return {
        .width = std::clamp(requested.width, caps.minImageExtent.width, caps.maxImageExtent.width),
        .height =
            std::clamp(requested.height, caps.minImageExtent.height, caps.maxImageExtent.height)};
}

} // namespace

Swapchain::Impl::~Impl() {
    if (device == nullptr || device->device == VK_NULL_HANDLE) {
        return;
    }
    EZMU_ASSERT_MSG(!acquired, "Swapchain destroyed while a frame that acquired it is open");
    static_cast<void>(vkQueueWaitIdle(device->graphicsQueue)); // Best effort at destruction.
    try {
        releaseImages(); // Only the handle pool's free list can allocate (and throw) here.
    } catch (...) {      // NOLINT(bugprone-empty-catch): a destructor must not throw; views leak.
    }
    for (VkSemaphore semaphore : acquireSemaphores) {
        vkDestroySemaphore(device->device, semaphore, nullptr);
    }
    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device->device, swapchain, nullptr);
    }
    if (surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(device->instance, surface, nullptr);
    }
    --device->swapchainCount;
}

foundation::Result<> Swapchain::Impl::configure(const SwapchainDesc& desc) {
    VkBool32 supported = VK_FALSE;
    if (const VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(
            device->physicalDevice, device->graphicsQueueFamily, surface, &supported);
        result != VK_SUCCESS) {
        return std::unexpected(detail::vkError(result, "vkGetPhysicalDeviceSurfaceSupportKHR"));
    }
    if (supported != VK_TRUE) {
        return foundation::makeError(foundation::ErrorCode::kUnsupported,
                                     "Device::createSwapchain: the graphics queue cannot present "
                                     "to this surface");
    }

    auto formats = enumerate<VkSurfaceFormatKHR>(
        [this](std::uint32_t* count, VkSurfaceFormatKHR* values) {
            return vkGetPhysicalDeviceSurfaceFormatsKHR(device->physicalDevice, surface, count,
                                                        values);
        },
        "vkGetPhysicalDeviceSurfaceFormatsKHR");
    if (!formats) {
        return std::unexpected(std::move(formats.error()));
    }
    auto chosen = chooseSurfaceFormat(*formats, desc.srgb);
    if (!chosen) {
        return std::unexpected(std::move(chosen.error()));
    }
    surfaceFormat = *chosen;
    format = fromVk(surfaceFormat.format);

    auto modes = enumerate<VkPresentModeKHR>(
        [this](std::uint32_t* count, VkPresentModeKHR* values) {
            return vkGetPhysicalDeviceSurfacePresentModesKHR(device->physicalDevice, surface, count,
                                                             values);
        },
        "vkGetPhysicalDeviceSurfacePresentModesKHR");
    if (!modes) {
        return std::unexpected(std::move(modes.error()));
    }
    presentMode = std::ranges::find(*modes, toVk(desc.presentMode)) != modes->end()
                      ? desc.presentMode
                      : PresentMode::kFifo;

    for (std::uint32_t slot = 0; slot < device->framesInFlight; ++slot) {
        const VkSemaphoreCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0};
        VkSemaphore semaphore = VK_NULL_HANDLE;
        if (const VkResult result = vkCreateSemaphore(device->device, &info, nullptr, &semaphore);
            result != VK_SUCCESS) {
            return std::unexpected(detail::vkError(result, "vkCreateSemaphore"));
        }
        acquireSemaphores.push_back(semaphore);
        device->setDebugName(VK_OBJECT_TYPE_SEMAPHORE, detail::rawHandle(semaphore),
                             std::format("{} acquire #{}", name, slot));
    }
    return {};
}

void Swapchain::Impl::releaseImages() {
    for (const TextureHandle image : images) {
        if (auto record = device->textures.remove(image)) {
            if (record->view != VK_NULL_HANDLE) {
                vkDestroyImageView(device->device, record->view, nullptr);
            }
        }
    }
    images.clear();
    for (VkSemaphore semaphore : renderFinished) {
        vkDestroySemaphore(device->device, semaphore, nullptr);
    }
    renderFinished.clear();
}

foundation::Result<bool> Swapchain::Impl::rebuild() {
    // The old images may still be read by earlier frames or presentation. Recreation is rare,
    // so waiting for the queue keeps this simple (no per-frame cost).
    if (const VkResult result = vkQueueWaitIdle(device->graphicsQueue); result != VK_SUCCESS) {
        return std::unexpected(detail::vkError(result, "Swapchain: vkQueueWaitIdle"));
    }
    VkSurfaceCapabilitiesKHR caps{.minImageCount = 0,
                                  .maxImageCount = 0,
                                  .currentExtent = {},
                                  .minImageExtent = {},
                                  .maxImageExtent = {},
                                  .maxImageArrayLayers = 0,
                                  .supportedTransforms = 0,
                                  .currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
                                  .supportedCompositeAlpha = 0,
                                  .supportedUsageFlags = 0};
    if (const VkResult result =
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device->physicalDevice, surface, &caps);
        result != VK_SUCCESS) {
        return std::unexpected(
            detail::vkError(result, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
    }
    const foundation::Extent2D size = chooseExtent(caps, requested);
    if (size.isEmpty()) {
        return false; // Minimized: keep the old swapchain (if any) until there is an area again.
    }

    std::uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount != 0) {
        imageCount = std::min(imageCount, caps.maxImageCount);
    }
    VkImageUsageFlags vkUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    usage = TextureUsage::kRenderTarget;
    if (allowCapture && (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0) {
        vkUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        usage = usage | TextureUsage::kTransferSrc;
    }

    const VkSwapchainCreateInfoKHR info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .surface = surface,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = {.width = size.width, .height = size.height},
        .imageArrayLayers = 1,
        .imageUsage = vkUsage,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .preTransform = caps.currentTransform,
        .compositeAlpha = chooseCompositeAlpha(caps.supportedCompositeAlpha),
        .presentMode = toVk(presentMode),
        .clipped = VK_TRUE,
        .oldSwapchain = swapchain,
    };
    VkSwapchainKHR created = VK_NULL_HANDLE;
    if (const VkResult result = vkCreateSwapchainKHR(device->device, &info, nullptr, &created);
        result != VK_SUCCESS) {
        return std::unexpected(detail::vkError(result, "vkCreateSwapchainKHR"));
    }
    releaseImages();
    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device->device, swapchain, nullptr);
    }
    swapchain = created;
    extent = size;
    needsRecreate = false;
    device->setDebugName(VK_OBJECT_TYPE_SWAPCHAIN_KHR, detail::rawHandle(swapchain), name);

    auto vkImages = enumerate<VkImage>(
        [this](std::uint32_t* count, VkImage* values) {
            return vkGetSwapchainImagesKHR(device->device, swapchain, count, values);
        },
        "vkGetSwapchainImagesKHR");
    if (!vkImages) {
        return std::unexpected(std::move(vkImages.error()));
    }
    for (VkImage image : *vkImages) {
        const VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = surfaceFormat.format,
            .components = {.r = VK_COMPONENT_SWIZZLE_IDENTITY,
                           .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                           .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                           .a = VK_COMPONENT_SWIZZLE_IDENTITY},
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1},
        };
        VkImageView view = VK_NULL_HANDLE;
        if (const VkResult result = vkCreateImageView(device->device, &viewInfo, nullptr, &view);
            result != VK_SUCCESS) {
            return std::unexpected(detail::vkError(result, "Swapchain: vkCreateImageView"));
        }
        const std::size_t index = images.size();
        images.push_back(device->textures.insert({.image = image,
                                                  .view = view,
                                                  .size = size,
                                                  .format = format,
                                                  .usage = usage,
                                                  .swapchainImage = true}));
        const std::string imageName = std::format("{} image #{}", name, index);
        device->setDebugName(VK_OBJECT_TYPE_IMAGE, detail::rawHandle(image), imageName);
        device->setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, detail::rawHandle(view),
                             imageName + " (view)");

        const VkSemaphoreCreateInfo semaphoreInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0};
        VkSemaphore semaphore = VK_NULL_HANDLE;
        if (const VkResult result =
                vkCreateSemaphore(device->device, &semaphoreInfo, nullptr, &semaphore);
            result != VK_SUCCESS) {
            return std::unexpected(detail::vkError(result, "Swapchain: vkCreateSemaphore"));
        }
        renderFinished.push_back(semaphore);
        device->setDebugName(VK_OBJECT_TYPE_SEMAPHORE, detail::rawHandle(semaphore),
                             imageName + " render finished");
    }
    return true;
}

foundation::Result<bool> Swapchain::Impl::ensureBuilt() {
    if (!needsRecreate && swapchain != VK_NULL_HANDLE) {
        return true;
    }
    return rebuild();
}

TextureHandle Swapchain::Impl::recordAcquisition(std::uint32_t imageIndex, VkSemaphore semaphore) {
    const TextureHandle texture = images.at(imageIndex);
    detail::TextureRecord* record = device->textures.get(texture);
    EZMU_VERIFY(record != nullptr);
    record->state = ResourceState::kUndefined; // Previous contents are discarded.
    device->acquisitions.push_back({.swapchain = this,
                                    .texture = texture,
                                    .imageIndex = imageIndex,
                                    .acquired = semaphore,
                                    .renderFinished = renderFinished.at(imageIndex)});
    acquired = true;
    return texture;
}

foundation::Result<TextureHandle> Swapchain::Impl::acquire(Frame& frame) {
    const bool current = device->openFrame == &frame;
    EZMU_ASSERT_MSG(current, "Swapchain::acquire: frame is not the device's open frame");
    EZMU_ASSERT_MSG(!acquired, "Swapchain::acquire: already acquired in this frame");
    if (!current || acquired) {
        return foundation::makeError(foundation::ErrorCode::kInvalidArgument,
                                     "Swapchain::acquire: not the open frame, or acquired twice");
    }
    if (requested.isEmpty()) {
        return TextureHandle{}; // Nothing to present (minimized).
    }

    VkSemaphore semaphore = acquireSemaphores.at(frame.slot());
    // A second attempt after recreating covers an out-of-date swapchain detected by the acquire.
    for (int attempt = 0; attempt < 2; ++attempt) {
        auto built = ensureBuilt();
        if (!built || !*built) {
            return built ? foundation::Result<TextureHandle>{TextureHandle{}}
                         : std::unexpected(std::move(built.error()));
        }
        std::uint32_t imageIndex = 0;
        const VkResult result = vkAcquireNextImageKHR(device->device, swapchain,
                                                      std::numeric_limits<std::uint64_t>::max(),
                                                      semaphore, VK_NULL_HANDLE, &imageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            needsRecreate = true;
            continue;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            return std::unexpected(detail::vkError(result, "vkAcquireNextImageKHR"));
        }
        needsRecreate = result == VK_SUBOPTIMAL_KHR; // Present this image, recreate next time.
        return recordAcquisition(imageIndex, semaphore);
    }
    return TextureHandle{}; // Still out of date: skip this frame.
}

Swapchain::Swapchain(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {}

Swapchain::~Swapchain() = default;

void Swapchain::resize(foundation::Extent2D pixelSize) {
    m_impl->requested = pixelSize;
    if (pixelSize != m_impl->extent) {
        m_impl->needsRecreate = true;
    }
}

foundation::Result<TextureHandle> Swapchain::acquire(Frame& frame) {
    return m_impl->acquire(frame);
}

Format Swapchain::format() const noexcept {
    return m_impl->format;
}

foundation::Extent2D Swapchain::extent() const noexcept {
    return m_impl->extent;
}

PresentMode Swapchain::presentMode() const noexcept {
    return m_impl->presentMode;
}

TextureUsage Swapchain::imageUsage() const noexcept {
    return m_impl->usage;
}

foundation::Result<std::unique_ptr<Swapchain>> Device::createSwapchain(std::uint64_t rawSurface,
                                                                       const SwapchainDesc& desc) {
    using foundation::ErrorCode;
    Impl& impl = *m_impl;
    if (rawSurface == 0) {
        return foundation::makeError(ErrorCode::kInvalidArgument,
                                     "Device::createSwapchain: null surface");
    }
    if (!impl.surfaceSupport) {
        // Without VK_KHR_surface the surface cannot come from this instance; nothing to destroy.
        return foundation::makeError(ErrorCode::kUnsupported,
                                     "Device::createSwapchain: the device was created without "
                                     "presentation support (requirePresent and the window's "
                                     "instance extensions)");
    }
    auto swapchain = std::make_unique<Swapchain::Impl>(impl);
    swapchain->surface = detail::fromRawHandle<VkSurfaceKHR>(rawSurface); // Owned from here on.
    ++impl.swapchainCount;
    swapchain->name =
        desc.debugName.empty() ? std::string("swapchain") : std::string(desc.debugName);
    swapchain->requested = desc.pixelSize;
    swapchain->allowCapture = desc.allowCapture;
    if (auto configured = swapchain->configure(desc); !configured) {
        return std::unexpected(std::move(configured.error()));
    }
    if (!desc.pixelSize.isEmpty()) {
        if (auto built = swapchain->rebuild(); !built) {
            return std::unexpected(std::move(built.error()));
        }
    }
    // NOLINTNEXTLINE(modernize-make-unique): the constructor is private.
    return std::unique_ptr<Swapchain>(new Swapchain(std::move(swapchain)));
}

} // namespace ezmu::rhi

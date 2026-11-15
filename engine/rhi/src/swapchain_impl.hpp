#pragma once

#include <ezmu/rhi/swapchain.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "device_impl.hpp"
#include "vulkan.hpp"

namespace ezmu::rhi {

struct Swapchain::Impl {
    explicit Impl(Device::Impl& owner) noexcept : device(&owner) {}
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
    /// Waits for the queue, then destroys images, semaphores, the swapchain and the surface.
    /// Safe on partially created swapchains.
    ~Impl();

    /// Picks the surface format and present mode and creates the per-slot acquire semaphores.
    [[nodiscard]] foundation::Result<> configure(const SwapchainDesc& desc);
    /// (Re)creates the VkSwapchainKHR for the current surface size after the queue went idle.
    /// Returns false (nothing built) when the surface has no area (minimized).
    [[nodiscard]] foundation::Result<bool> rebuild();
    /// Removes the image textures and destroys their views and render-finished semaphores.
    void releaseImages();
    /// rebuild() when out of date or not built yet; true when images exist.
    [[nodiscard]] foundation::Result<bool> ensureBuilt();
    /// Registers image `imageIndex` as acquired by the open frame and returns its texture.
    TextureHandle recordAcquisition(std::uint32_t imageIndex, VkSemaphore semaphore);
    [[nodiscard]] foundation::Result<TextureHandle> acquire(Frame& frame);

    Device::Impl* device = nullptr;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkSurfaceFormatKHR surfaceFormat{};
    Format format = Format::kUndefined;
    PresentMode presentMode = PresentMode::kFifo;
    bool allowCapture = true;
    TextureUsage usage = TextureUsage::kRenderTarget;
    foundation::Extent2D extent{};    ///< Of the current images.
    foundation::Extent2D requested{}; ///< From the description and resize().
    bool needsRecreate = true;
    bool acquired = false; ///< An image is acquired by the open frame.
    std::vector<TextureHandle> images;
    std::vector<VkSemaphore> renderFinished;    ///< Per image.
    std::vector<VkSemaphore> acquireSemaphores; ///< Per frame slot.
    std::string name;
};

} // namespace ezmu::rhi

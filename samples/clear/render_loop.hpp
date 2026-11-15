#pragma once

#include <ezmu/foundation/log.hpp>
#include <ezmu/foundation/result.hpp>
#include <ezmu/rhi/device.hpp>
#include <ezmu/rhi/swapchain.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stop_token>

namespace ezmu::samples::clear {

class Mailbox;

struct RenderConfig {
    /// Linear RGBA clear color.
    std::array<float, 4> clearColor{0.1F, 0.2F, 0.4F, 1.0F};
    /// Stop after this many presented frames; 0 runs until stop is requested.
    std::uint64_t frameLimit = 0;
    /// Where captures are written (last frame with a frame limit, or on request).
    std::filesystem::path capturePath{};
    /// Capture the last frame when frameLimit is reached.
    bool captureLastFrame = false;
};

/// Body of the render thread: clears and presents every frame until `stop` is requested or
/// `config.frameLimit` frames were presented. Owns `swapchain` and destroys it (after waiting
/// for the GPU) before returning, so the main thread may then destroy the window. `device`
/// must be used by no other thread meanwhile.
[[nodiscard]] foundation::Result<> runRenderLoop(const std::stop_token& stop, rhi::Device& device,
                                                 std::unique_ptr<rhi::Swapchain> swapchain,
                                                 const RenderConfig& config, Mailbox& mailbox,
                                                 foundation::Logger& logger);

} // namespace ezmu::samples::clear

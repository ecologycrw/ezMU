#pragma once

#include <ezmu/foundation/image.hpp>
#include <ezmu/foundation/result.hpp>
#include <ezmu/foundation/version.hpp>
#include <ezmu/rhi/handles.hpp>
#include <ezmu/rhi/resources.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

// The RHI device: the only owner of Vulkan in ezMU (ADR 0006). No Vulkan, volk or VMA type
// appears in this header; interop uses opaque pointers. Spec: docs/specs/rhi.md.

namespace ezmu::rhi {

class CommandList;
class Frame;
class Swapchain;
struct SwapchainDesc;

enum class ValidationMode : std::uint8_t {
    kOff,
    kEnabled,         ///< VK_LAYER_KHRONOS_validation.
    kEnabledWithSync, ///< + synchronization validation (default in rendering tests).
};

/// Validation on in debug builds, off in release builds.
#ifdef NDEBUG
inline constexpr ValidationMode kDefaultValidationMode = ValidationMode::kOff;
#else
inline constexpr ValidationMode kDefaultValidationMode = ValidationMode::kEnabled;
#endif

enum class AdapterPreference : std::uint8_t {
    kHighPerformance, ///< Discrete, then integrated, then software.
    kLowPower,        ///< Integrated, then discrete, then software.
    kSoftware,        ///< CPU (e.g. Mesa lavapipe) first; for deterministic tests.
};

enum class ValidationSeverity : std::uint8_t { kInfo, kWarning, kError };

/// A message from the validation layer or the driver. Views are valid only during the callback.
struct ValidationMessage {
    ValidationSeverity severity = ValidationSeverity::kInfo;
    std::string_view messageIdName{};
    std::string_view text{};
};

/// Called for warnings and errors, on the thread that made the Vulkan call. Must be thread-safe.
using ValidationCallback = std::function<void(const ValidationMessage&)>;

/// Opaque Vulkan entry point (e.g. vkGetInstanceProcAddr from platform::Window).
using VulkanVoidFunction = void (*)();

struct DeviceDesc {
    std::string_view appName = "ezMU";
    /// Instance extensions needed for presentation (from platform::Window). Empty = headless.
    std::span<const char* const> instanceExtensions{};
    /// Require swapchain support (VK_KHR_swapchain). false for headless devices.
    bool requirePresent = true;
    ValidationMode validation = kDefaultValidationMode;
    ValidationCallback onValidationMessage{};
    AdapterPreference adapterPreference = AdapterPreference::kHighPerformance;
    /// Case-insensitive substring of the adapter name. The EZMU_GPU environment variable, when
    /// set, overrides it.
    std::string_view adapterNameFilter{};
    /// Optional vkGetInstanceProcAddr to load Vulkan from (e.g. the library SDL loaded). When
    /// null, the system Vulkan loader is used.
    VulkanVoidFunction vkGetInstanceProcAddr = nullptr;
    /// Frames the CPU may record ahead of the GPU, 1..kMaxFramesInFlight (more = more latency,
    /// fewer stalls).
    std::uint32_t framesInFlight = 2;
};

/// Upper bound of DeviceDesc::framesInFlight.
inline constexpr std::uint32_t kMaxFramesInFlight = 4;

enum class AdapterType : std::uint8_t { kOther, kIntegrated, kDiscrete, kVirtual, kCpu };

/// Description of the GPU a Device runs on.
struct AdapterInfo {
    std::string name{};
    AdapterType type = AdapterType::kOther;
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::string driver{}; ///< Driver name and version, as reported by the driver.
    foundation::Version apiVersion{};
};

/// Owns the Vulkan instance, debug messenger, logical device, queue and memory allocator.
///
/// At most one Device may exist at a time (the Vulkan loader functions are process-wide);
/// create() fails with kInvalidArgument otherwise. create() fails with kUnsupported when no
/// Vulkan loader is installed, when no adapter supports Vulkan 1.3 with the required features,
/// or when validation is requested but the validation layer is missing; with kInvalidArgument
/// when framesInFlight is outside 1..kMaxFramesInFlight.
///
/// Thread-safety: externally synchronized — one thread at a time. Ownership may move between
/// threads with a happens-before edge (e.g. created on the main thread, then used by the
/// render thread).
class Device {
public:
    [[nodiscard]] static foundation::Result<std::unique_ptr<Device>> create(const DeviceDesc& desc);

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;
    /// Waits for the GPU to finish, then destroys everything the device owns.
    ~Device();

    [[nodiscard]] const AdapterInfo& adapterInfo() const noexcept;
    [[nodiscard]] ValidationMode validationMode() const noexcept;

    /// Number of validation errors / warnings reported since creation. Thread-safe.
    [[nodiscard]] std::uint32_t validationErrorCount() const noexcept;
    [[nodiscard]] std::uint32_t validationWarningCount() const noexcept;

    /// Creates a 2D texture (one mip, one layer) in device-local memory, in
    /// ResourceState::kUndefined. Fails with kInvalidArgument for an empty size, kUndefined
    /// format, no usage, or a usage that does not fit the format (render target on a depth
    /// format, depth-stencil on a color format); with kUnsupported when the adapter does not
    /// support the format/usage combination or size. Allocates.
    [[nodiscard]] foundation::Result<TextureHandle> createTexture(const TextureDesc& desc);

    /// Creates a buffer. kUpload and kReadback buffers are persistently mapped (mappedData()).
    /// Fails with kInvalidArgument for size 0. Allocates.
    [[nodiscard]] foundation::Result<BufferHandle> createBuffer(const BufferDesc& desc);

    /// Destroys a resource. The handle is stale at once; the GPU object is freed once every
    /// frame that may use it has completed (immediately when no such frame is in flight, else
    /// at a later beginFrame(), waitIdle() or the Device's destruction). Invalid (default)
    /// handles are ignored; stale handles and swapchain images are an EZMU_ASSERT. Must not be
    /// called while executeImmediate() is recording. May allocate (deferred queue).
    void destroy(TextureHandle texture);
    void destroy(BufferHandle buffer);

    /// Number of destroyed resources whose GPU objects are still waiting for the GPU. For tests
    /// and diagnostics.
    [[nodiscard]] std::size_t pendingDestructionCount() const noexcept;

    /// CPU view of a kUpload or kReadback buffer (empty for kGpu buffers or stale handles).
    /// The memory is host-coherent: no flush/invalidate is needed, but the caller must not read
    /// GPU results before the submission that wrote them has completed. Valid until the buffer
    /// is destroyed.
    [[nodiscard]] std::span<std::byte> mappedData(BufferHandle buffer);

    /// Records commands with `record` into a one-shot command list, submits it and blocks until
    /// the GPU has executed it. For setup work, tests and capture; frames use their own command
    /// lists. Runs after every frame submitted before it. Must not be nested nor called while a
    /// frame is being recorded (kInvalidArgument after an EZMU_ASSERT: resource states are
    /// tracked in recording order). Fails (kInternal) when submission fails or the device is
    /// lost.
    [[nodiscard]] foundation::Result<>
    executeImmediate(const std::function<void(CommandList&)>& record);

    /// Blocking readback for tests and on-demand capture: copies `texture` to a readback buffer,
    /// waits, and returns tightly packed RGBA8 pixels, top row first. 8-bit formats are returned
    /// as stored (sRGB values stay sRGB-encoded); BGRA is swizzled to RGBA. The texture needs
    /// kTransferSrc usage and must have been written (state other than kUndefined); its tracked
    /// state is restored afterwards. Fails with kUnsupported for other formats and
    /// kInvalidArgument for stale handles, unmet requirements or a call while a frame is being
    /// recorded. Not for presented swapchain images (copy them inside the frame instead). Stalls
    /// the GPU; never call per frame.
    [[nodiscard]] foundation::Result<foundation::Image> readTexture(TextureHandle texture);

    /// Creates a swapchain on a surface made by platform::Window::createVulkanSurface() from
    /// nativeInstance(). Takes ownership of `rawSurface` (a VkSurfaceKHR): it is destroyed with
    /// the swapchain, or right away when creation fails (when the device has surface support).
    /// Fails with kInvalidArgument for a null surface; kUnsupported when the device was created
    /// without presentation support (requirePresent plus the window's instance extensions), the
    /// queue cannot present to the surface or no surface format fits. Startup work.
    [[nodiscard]] foundation::Result<std::unique_ptr<Swapchain>>
    createSwapchain(std::uint64_t rawSurface, const SwapchainDesc& desc);

    /// Starts frame `n`: waits until the GPU has finished frame `n - framesInFlight()` (the
    /// previous use of this frame slot), frees resources destroyed before that frame, and resets
    /// the slot's command list. Frames do not nest (kInvalidArgument after an EZMU_ASSERT).
    /// Fails (kInternal) when the device is lost. No allocation in steady state.
    [[nodiscard]] foundation::Result<Frame*> beginFrame();

    /// Ends `frame` (the one beginFrame() returned): submits its command list, waiting for the
    /// swapchain images it acquired, then presents them. Acquired images left in a state other
    /// than kPresent are an EZMU_ASSERT (they are transitioned anyway); open passes and labels
    /// are closed. Out-of-date swapchains are recreated on their next acquire. Fails (kInternal)
    /// when submission or presentation fails (device or surface lost); the frame is over either
    /// way. No allocation in steady state.
    [[nodiscard]] foundation::Result<> endFrame(Frame& frame);

    /// DeviceDesc::framesInFlight.
    [[nodiscard]] std::uint32_t framesInFlight() const noexcept;

    /// Blocks until the GPU has finished all submitted work, then frees pending destructions.
    /// Never call per frame.
    void waitIdle();

    /// Opaque raw handles for interop (platform surface creation, tools, tests): VkInstance,
    /// VkPhysicalDevice and VkDevice. Valid for the Device's lifetime.
    [[nodiscard]] void* nativeInstance() const noexcept;
    [[nodiscard]] void* nativePhysicalDevice() const noexcept;
    [[nodiscard]] void* nativeDevice() const noexcept;

    struct Impl;

private:
    explicit Device(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

[[nodiscard]] std::string_view toString(AdapterType type) noexcept;

} // namespace ezmu::rhi

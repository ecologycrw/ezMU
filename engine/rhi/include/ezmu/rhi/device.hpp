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
};

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
/// or when validation is requested but the validation layer is missing.
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

    /// Destroys a resource. Invalid (default) handles are ignored; stale handles are an
    /// EZMU_ASSERT. Must not be called while executeImmediate() is recording. Until frames in
    /// flight exist every submission is waited for, so the object is freed immediately; with
    /// frames, destruction is deferred until the GPU no longer uses it.
    void destroy(TextureHandle texture);
    void destroy(BufferHandle buffer);

    /// CPU view of a kUpload or kReadback buffer (empty for kGpu buffers or stale handles).
    /// The memory is host-coherent: no flush/invalidate is needed, but the caller must not read
    /// GPU results before the submission that wrote them has completed. Valid until the buffer
    /// is destroyed.
    [[nodiscard]] std::span<std::byte> mappedData(BufferHandle buffer);

    /// Records commands with `record` into a one-shot command list, submits it and blocks until
    /// the GPU has executed it. For setup work, tests and capture; frames use their own command
    /// lists. Must not be nested. Fails (kInternal) when submission fails or the device is lost.
    [[nodiscard]] foundation::Result<>
    executeImmediate(const std::function<void(CommandList&)>& record);

    /// Blocking readback for tests and on-demand capture: copies `texture` to a readback buffer,
    /// waits, and returns tightly packed RGBA8 pixels, top row first. 8-bit formats are returned
    /// as stored (sRGB values stay sRGB-encoded); BGRA is swizzled to RGBA. The texture needs
    /// kTransferSrc usage and must have been written (state other than kUndefined); its tracked
    /// state is restored afterwards. Fails with kUnsupported for other formats and
    /// kInvalidArgument for stale handles or unmet requirements. Stalls the GPU; never call per
    /// frame.
    [[nodiscard]] foundation::Result<foundation::Image> readTexture(TextureHandle texture);

    /// Blocks until the GPU has finished all submitted work. Never call per frame.
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

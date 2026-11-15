#include <ezmu/foundation/assert.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>
#include <format>
#include <limits>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "device_impl.hpp"

namespace ezmu::rhi {

namespace {

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

ValidationSeverity toSeverity(VkDebugUtilsMessageSeverityFlagBitsEXT severity) noexcept {
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        return ValidationSeverity::kError;
    }
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) {
        return ValidationSeverity::kWarning;
    }
    return ValidationSeverity::kInfo;
}

VKAPI_ATTR VkBool32 VKAPI_CALL onDebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data,
                                              void* userData) {
    auto& impl = *static_cast<Device::Impl*>(userData);
    const ValidationMessage message{
        .severity = toSeverity(severity),
        .messageIdName = data->pMessageIdName != nullptr ? data->pMessageIdName : "",
        .text = data->pMessage != nullptr ? data->pMessage : "",
    };
    if (message.severity == ValidationSeverity::kError) {
        impl.errorCount.fetch_add(1, std::memory_order_relaxed);
    } else if (message.severity == ValidationSeverity::kWarning) {
        impl.warningCount.fetch_add(1, std::memory_order_relaxed);
    }
    // Called from C code inside the driver/layer: nothing may escape.
    try {
        if (impl.callback) {
            impl.callback(message);
        } else {
            std::println(stderr, "[ezMU] [rhi] validation {}: {}",
                         message.severity == ValidationSeverity::kError ? "error" : "warning",
                         message.text);
        }
    } catch (...) { // NOLINT(bugprone-empty-catch): must not unwind into Vulkan.
    }
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT messengerInfo(Device::Impl& impl) noexcept {
    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = &onDebugMessage;
    info.pUserData = &impl;
    return info;
}

foundation::Result<std::vector<VkLayerProperties>> instanceLayers() {
    std::uint32_t count = 0;
    if (const VkResult r = vkEnumerateInstanceLayerProperties(&count, nullptr); r != VK_SUCCESS) {
        return std::unexpected(detail::vkError(r, "vkEnumerateInstanceLayerProperties"));
    }
    std::vector<VkLayerProperties> layers(count);
    if (const VkResult r = vkEnumerateInstanceLayerProperties(&count, layers.data());
        r != VK_SUCCESS && r != VK_INCOMPLETE) {
        return std::unexpected(detail::vkError(r, "vkEnumerateInstanceLayerProperties"));
    }
    return layers;
}

std::vector<VkExtensionProperties> instanceExtensions(const char* layer) {
    std::uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(layer, &count, nullptr) != VK_SUCCESS) {
        return {};
    }
    std::vector<VkExtensionProperties> extensions(count);
    if (vkEnumerateInstanceExtensionProperties(layer, &count, extensions.data()) != VK_SUCCESS) {
        return {};
    }
    return extensions;
}

bool contains(const std::vector<VkExtensionProperties>& extensions, std::string_view name) {
    return std::ranges::any_of(extensions, [name](const VkExtensionProperties& extension) {
        return name == detail::fixedString(extension.extensionName);
    });
}

foundation::Result<> initializeLoader(Device::Impl& impl, const DeviceDesc& desc) {
    if (volkGetLoadedInstance() != VK_NULL_HANDLE) {
        return foundation::makeError(foundation::ErrorCode::kInvalidArgument,
                                     "Device::create: a Device already exists (only one Device "
                                     "may exist at a time)");
    }
    if (desc.vkGetInstanceProcAddr != nullptr) {
        // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast): opaque entry point.
        volkInitializeCustom(
            reinterpret_cast<PFN_vkGetInstanceProcAddr>(desc.vkGetInstanceProcAddr));
        // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
    } else if (const VkResult result = volkInitialize(); result != VK_SUCCESS) {
        return foundation::makeError(foundation::ErrorCode::kUnsupported,
                                     "Device::create: no Vulkan loader found (install a Vulkan "
                                     "driver; on Windows the loader is vulkan-1.dll)");
    }
    impl.loaderInitialized = true;
    if (volkGetInstanceVersion() < VK_API_VERSION_1_3) {
        return foundation::makeError(foundation::ErrorCode::kUnsupported,
                                     "Device::create: the Vulkan loader does not support 1.3");
    }
    return {};
}

foundation::Result<> createInstance(Device::Impl& impl, const DeviceDesc& desc) {
    std::vector<const char*> extensions(desc.instanceExtensions.begin(),
                                        desc.instanceExtensions.end());
    std::vector<const char*> layers;
    const bool validation = desc.validation != ValidationMode::kOff;
    const auto available = instanceExtensions(nullptr);
    const bool debugUtils = contains(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    bool syncValidation = false;
    if (validation) {
        auto allLayers = instanceLayers();
        if (!allLayers) {
            return std::unexpected(std::move(allLayers.error()));
        }
        const bool hasLayer = std::ranges::any_of(*allLayers, [](const VkLayerProperties& layer) {
            return detail::fixedString(layer.layerName) == kValidationLayer;
        });
        if (!hasLayer) {
            return foundation::makeError(foundation::ErrorCode::kUnsupported,
                                         "Device::create: validation requested but "
                                         "VK_LAYER_KHRONOS_validation is not installed (Vulkan "
                                         "SDK on Windows, vulkan-validationlayers on Linux)");
        }
        if (!debugUtils) {
            return foundation::makeError(foundation::ErrorCode::kUnsupported,
                                         "Device::create: validation needs VK_EXT_debug_utils");
        }
        layers.push_back(kValidationLayer);
        const auto layerExtensions = instanceExtensions(kValidationLayer);
        syncValidation = desc.validation == ValidationMode::kEnabledWithSync &&
                         contains(layerExtensions, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        if (syncValidation) {
            extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        }
    }
    if (debugUtils) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    impl.debugUtils = debugUtils;

    const std::string appName(desc.appName);
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = appName.c_str();
    app.pEngineName = "ezMU";
    app.engineVersion =
        VK_MAKE_API_VERSION(0, foundation::kEngineVersion.major, foundation::kEngineVersion.minor,
                            foundation::kEngineVersion.patch);
    app.apiVersion = VK_API_VERSION_1_3;

    VkDebugUtilsMessengerCreateInfoEXT messenger = messengerInfo(impl);
    constexpr std::array kEnabledFeatures{
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT};
    VkValidationFeaturesEXT features{};
    features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
    features.enabledValidationFeatureCount = static_cast<std::uint32_t>(kEnabledFeatures.size());
    features.pEnabledValidationFeatures = kEnabledFeatures.data();
    if (syncValidation) {
        messenger.pNext = &features;
    }

    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pNext = validation ? &messenger : nullptr; // Catches create/destroy-time messages.
    info.pApplicationInfo = &app;
    info.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    info.ppEnabledLayerNames = layers.data();
    info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();

    if (const VkResult result = detail::driverCreateInstance(info, impl.instance);
        result != VK_SUCCESS) {
        impl.instance = VK_NULL_HANDLE;
        if (result == VK_ERROR_INCOMPATIBLE_DRIVER || result == VK_ERROR_EXTENSION_NOT_PRESENT ||
            result == VK_ERROR_LAYER_NOT_PRESENT) {
            return foundation::makeError(foundation::ErrorCode::kUnsupported,
                                         detail::vkError(result, "vkCreateInstance").message());
        }
        return std::unexpected(detail::vkError(result, "vkCreateInstance"));
    }
    volkLoadInstanceOnly(impl.instance);

    if (validation) {
        const VkDebugUtilsMessengerCreateInfoEXT persistent = messengerInfo(impl);
        if (const VkResult result = vkCreateDebugUtilsMessengerEXT(impl.instance, &persistent,
                                                                   nullptr, &impl.messenger);
            result != VK_SUCCESS) {
            impl.messenger = VK_NULL_HANDLE;
            return std::unexpected(detail::vkError(result, "vkCreateDebugUtilsMessengerEXT"));
        }
    }
    return {};
}

foundation::Result<> createDevice(Device::Impl& impl, const DeviceDesc& desc) {
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.pNext = &features13;
    features12.timelineSemaphore = VK_TRUE;
    features12.descriptorIndexing = VK_TRUE;
    features12.runtimeDescriptorArray = VK_TRUE;
    features12.descriptorBindingPartiallyBound = VK_TRUE;
    features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &features12;

    const float priority = 1.0F;
    VkDeviceQueueCreateInfo queue{};
    queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue.queueFamilyIndex = impl.graphicsQueueFamily;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;

    std::vector<const char*> extensions;
    if (desc.requirePresent) {
        extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    VkDeviceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.pNext = &features;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue;
    info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();

    if (const VkResult result = vkCreateDevice(impl.physicalDevice, &info, nullptr, &impl.device);
        result != VK_SUCCESS) {
        impl.device = VK_NULL_HANDLE;
        return std::unexpected(detail::vkError(result, "vkCreateDevice"));
    }
    volkLoadDevice(impl.device);
    vkGetDeviceQueue(impl.device, impl.graphicsQueueFamily, 0, &impl.graphicsQueue);
    impl.setDebugName(VK_OBJECT_TYPE_DEVICE, detail::rawHandle(impl.device), "ezMU device");
    impl.setDebugName(VK_OBJECT_TYPE_QUEUE, detail::rawHandle(impl.graphicsQueue),
                      "graphics queue");

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(impl.physicalDevice, &properties);
    impl.maxImageDimension2D = properties.limits.maxImageDimension2D;

    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = impl.physicalDevice;
    allocatorInfo.device = impl.device;
    allocatorInfo.instance = impl.instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.pVulkanFunctions = &functions;
    if (const VkResult result = vmaCreateAllocator(&allocatorInfo, &impl.allocator);
        result != VK_SUCCESS) {
        impl.allocator = VK_NULL_HANDLE;
        return std::unexpected(detail::vkError(result, "vmaCreateAllocator"));
    }
    return {};
}

} // namespace

Device::Impl::~Impl() {
    EZMU_ASSERT_MSG(swapchainCount == 0, "Device destroyed before its swapchains");
    if (device != VK_NULL_HANDLE) {
        static_cast<void>(vkDeviceWaitIdle(device));
        // Deferred destructions are drained first; then resources the application did not
        // destroy, which the device owns.
        collectDestructions(std::numeric_limits<std::uint64_t>::max());
        textures.forEach([this](detail::TextureRecord& texture) { destroyTexture(texture); });
        buffers.forEach([this](detail::BufferRecord& buffer) { destroyBuffer(buffer); });
        for (const detail::FrameSlot& slot : frameSlots) {
            if (slot.pool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device, slot.pool, nullptr); // Frees the command buffer.
            }
        }
        if (timeline != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, timeline, nullptr);
        }
        if (immediate.fence != VK_NULL_HANDLE) {
            vkDestroyFence(device, immediate.fence, nullptr);
        }
        if (immediate.pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, immediate.pool, nullptr); // Frees the command buffer.
        }
    }
    if (allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(allocator);
    }
    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, nullptr);
    }
    if (messenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
    }
    if (loaderInitialized) {
        volkFinalize();
    }
}

foundation::Result<std::unique_ptr<Device>> Device::create(const DeviceDesc& desc) {
    if (desc.framesInFlight == 0 || desc.framesInFlight > kMaxFramesInFlight) {
        return foundation::makeError(
            foundation::ErrorCode::kInvalidArgument,
            std::format("Device::create: framesInFlight must be 1..{} (got {})", kMaxFramesInFlight,
                        desc.framesInFlight));
    }
    auto impl = std::make_unique<Impl>();
    impl->validation = desc.validation;
    impl->callback = desc.onValidationMessage;
    impl->framesInFlight = desc.framesInFlight;
    // Surfaces need VK_KHR_surface (from the window's extensions) and swapchains VK_KHR_swapchain.
    impl->surfaceSupport = desc.requirePresent &&
                           std::ranges::any_of(desc.instanceExtensions, [](const char* extension) {
                               return extension != nullptr &&
                                      std::string_view(extension) == VK_KHR_SURFACE_EXTENSION_NAME;
                           });

    if (auto result = initializeLoader(*impl, desc); !result) {
        return std::unexpected(std::move(result.error()));
    }
    if (auto result = createInstance(*impl, desc); !result) {
        return std::unexpected(std::move(result.error()));
    }

    auto candidates = detail::evaluateAdapters(impl->instance, desc.requirePresent);
    if (!candidates) {
        return std::unexpected(std::move(candidates.error()));
    }
    auto selected = detail::selectAdapter(*candidates, desc);
    if (!selected) {
        return std::unexpected(std::move(selected.error()));
    }
    impl->physicalDevice = selected->physicalDevice;
    impl->graphicsQueueFamily = selected->graphicsQueueFamily;
    impl->adapter = std::move(selected->info);

    if (auto result = createDevice(*impl, desc); !result) {
        return std::unexpected(std::move(result.error()));
    }
    if (auto result = impl->createFrameResources(); !result) {
        return std::unexpected(std::move(result.error()));
    }
    // NOLINTNEXTLINE(modernize-make-unique): the constructor is private.
    return std::unique_ptr<Device>(new Device(std::move(impl)));
}

Device::Device(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {}

Device::~Device() = default;

const AdapterInfo& Device::adapterInfo() const noexcept {
    return m_impl->adapter;
}

ValidationMode Device::validationMode() const noexcept {
    return m_impl->validation;
}

std::uint32_t Device::validationErrorCount() const noexcept {
    return m_impl->errorCount.load(std::memory_order_relaxed);
}

std::uint32_t Device::validationWarningCount() const noexcept {
    return m_impl->warningCount.load(std::memory_order_relaxed);
}

void Device::waitIdle() {
    if (const VkResult result = vkDeviceWaitIdle(m_impl->device); result != VK_SUCCESS) {
        EZMU_VERIFY_MSG(result == VK_SUCCESS, "vkDeviceWaitIdle failed (device lost?)");
    }
    m_impl->collectDestructions(m_impl->refreshCompletedValue());
}

void Device::Impl::setDebugName(VkObjectType type, std::uint64_t object,
                                std::string_view name) const {
    if (!debugUtils || vkSetDebugUtilsObjectNameEXT == nullptr || object == 0) {
        return;
    }
    const std::string terminated(name);
    VkDebugUtilsObjectNameInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType = type;
    info.objectHandle = object;
    info.pObjectName = terminated.c_str();
    static_cast<void>(vkSetDebugUtilsObjectNameEXT(device, &info)); // Best effort.
}

void* Device::nativeInstance() const noexcept {
    return m_impl->instance;
}

void* Device::nativePhysicalDevice() const noexcept {
    return m_impl->physicalDevice;
}

void* Device::nativeDevice() const noexcept {
    return m_impl->device;
}

} // namespace ezmu::rhi

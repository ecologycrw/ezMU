# ezmu::rhi — layer L1. Thin Vulkan 1.3 RHI (ADR 0006). Vulkan, volk and VMA are private to this
# module: no Vulkan header or type appears in include/.

find_package(volk CONFIG REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

ezmu_add_module(
    NAME rhi
    LAYER 1
    SOURCES
        src/adapter.cpp
        src/device.cpp
        src/vk_mem_alloc.cpp
        src/vulkan.cpp
    PUBLIC_DEPS
        ezmu::foundation
    PRIVATE_DEPS
        volk::volk
        Vulkan::Headers
        GPUOpen::VulkanMemoryAllocator)

# Function pointers come from volk; VMA fetches its own through vkGet*ProcAddr.
target_compile_definitions(ezmu_rhi PRIVATE
    VK_NO_PROTOTYPES
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1)

# GPU tests are tagged [gpu] (CTest label "gpu") and skip without a Vulkan 1.3 device (ADR 0020).
# Tests may call Vulkan directly (validation self-test), so they link volk too.
ezmu_add_module_tests(
    NAME rhi
    SOURCES
        tests/device_tests.cpp
    DEPS
        volk::volk)
target_compile_definitions(ezmu_rhi_tests PRIVATE VK_NO_PROTOTYPES)

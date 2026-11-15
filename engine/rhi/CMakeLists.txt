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
        src/command_list.cpp
        src/device.cpp
        src/frame.cpp
        src/resources.cpp
        src/swapchain.cpp
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
# Tests may call Vulkan directly (validation self-test), so they link volk.
ezmu_add_module_tests(
    NAME rhi
    SOURCES
        tests/command_list_tests.cpp
        tests/device_tests.cpp
        tests/frame_tests.cpp
        tests/handle_tests.cpp
        tests/resource_tests.cpp
        tests/swapchain_tests.cpp
    DEPS
        volk::volk)
if(TARGET ezmu_rhi_tests)
    # Reference images live in the source tree; mismatches are written under the build tree.
    target_compile_definitions(ezmu_rhi_tests PRIVATE
        VK_NO_PROTOTYPES
        EZMU_RHI_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/reference"
        EZMU_RHI_TEST_OUTPUT_DIR="${CMAKE_BINARY_DIR}/test-output/rhi")
endif()

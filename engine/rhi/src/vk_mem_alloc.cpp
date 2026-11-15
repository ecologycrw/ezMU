// The single translation unit that compiles the Vulkan Memory Allocator implementation.
// VMA_STATIC_VULKAN_FUNCTIONS=0 / VMA_DYNAMIC_VULKAN_FUNCTIONS=1 come from the target's compile
// definitions, so every TU sees the same configuration; functions are fetched through volk.

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): VMA is configured through macros.
#define VMA_IMPLEMENTATION

#include "vulkan.hpp" // Includes volk.h, then vk_mem_alloc.h.

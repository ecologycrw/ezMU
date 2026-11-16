# ezmu::renderer — layer L2. Turns render snapshots into GPU work (docs/specs/renderer.md,
# ADR 0012). Producer headers (snapshot, feedback, ring) depend only on foundation; only
# renderer.hpp names rhi types.

ezmu_add_module(
    NAME renderer
    LAYER 2
    SOURCES
        src/feedback.cpp
        src/renderer.cpp
        src/snapshot.cpp
        src/snapshot_ring.cpp
    PUBLIC_DEPS
        ezmu::foundation
        ezmu::rhi)

# GPU tests are tagged [gpu] (CTest label "gpu") and skip without a Vulkan 1.3 device (ADR 0020).
# They reuse the rhi GPU test support header (validation guard, headless device).
ezmu_add_module_tests(
    NAME renderer
    SOURCES
        tests/feedback_tests.cpp
        tests/renderer_tests.cpp
        tests/snapshot_ring_tests.cpp)
if(TARGET ezmu_renderer_tests)
    target_include_directories(ezmu_renderer_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/engine/rhi/tests")
    target_compile_definitions(ezmu_renderer_tests PRIVATE
        EZMU_RHI_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/reference"
        EZMU_RHI_TEST_OUTPUT_DIR="${CMAKE_BINARY_DIR}/test-output/renderer")
endif()

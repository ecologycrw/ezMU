# ezmu_clear: a window cleared every frame by a dedicated render thread (milestone M1,
# docs/specs/rhi.md). Composition root wiring platform (window, surface) and rhi (device,
# swapchain) through their type-erased Vulkan boundary.

ezmu_add_executable(
    NAME ezmu_clear
    LAYER 6
    SOURCES
        main.cpp
        render_loop.cpp
    DEPS ezmu::engine)

if(EZMU_BUILD_TESTS)
    ezmu_add_executable(
        NAME ezmu_clear_check
        LAYER 6
        SOURCES tests/check_capture.cpp
        DEPS ezmu::foundation)

    # Needs a display and a Vulkan 1.3 device: skipped otherwise (ADR 0020). The color has
    # values that differ between sRGB and UNORM encodings, so a wrong conversion fails.
    add_test(NAME samples.clear
        COMMAND "${CMAKE_COMMAND}"
            "-DCLEAR_EXE=$<TARGET_FILE:ezmu_clear>"
            "-DCHECK_EXE=$<TARGET_FILE:ezmu_clear_check>"
            "-DOUTPUT=${CMAKE_BINARY_DIR}/test-output/samples/clear/capture.png"
            "-DCOLOR=0.1,0.2,0.4"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_clear_test.cmake")
    set_tests_properties(samples.clear PROPERTIES
        LABELS gpu
        SKIP_REGULAR_EXPRESSION "EZMU_TEST_SKIPPED"
        TIMEOUT 120)
endif()

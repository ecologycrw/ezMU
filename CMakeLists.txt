cmake_minimum_required(VERSION 3.28)

project(ezMU
    VERSION 0.1.0
    DESCRIPTION "Client-side engine for classic MMORPGs"
    LANGUAGES CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use a preset: cmake --preset <name>")
endif()

# --- Options -----------------------------------------------------------------
option(EZMU_BUILD_TESTS "Build ezMU unit tests" ${PROJECT_IS_TOP_LEVEL})
option(EZMU_BUILD_SAMPLES "Build ezMU samples" ${PROJECT_IS_TOP_LEVEL})
option(EZMU_ENABLE_SANITIZERS "Build with AddressSanitizer (and UBSan where supported)" OFF)
option(EZMU_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

# --- Language ----------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF) # No C++ named modules (ADR 0003).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(EzmuWarnings)
include(EzmuSanitizers)
include(EzmuModule)

# --- Testing -----------------------------------------------------------------
if(EZMU_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 CONFIG REQUIRED)
    include(Catch)

    # Architecture rules (layering conventions, forbidden includes, vcpkg isolation).
    add_test(NAME architecture.rules
        COMMAND "${CMAKE_COMMAND}"
            "-DEZMU_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            -P "${PROJECT_SOURCE_DIR}/scripts/checks/check_rules.cmake")
endif()

# --- Layers (order matters: lower layers first) -------------------------------
add_subdirectory(engine)

if(EZMU_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

# ezmu::foundation — layer L0. Depends on nothing else in ezMU.

# stb (ADR 0021): header-only, located through the module-mode FindStb shipped by its package.
find_package(Stb REQUIRED)

configure_file(src/version.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/include/ezmu/foundation/version.hpp" @ONLY)

ezmu_add_module(
    NAME foundation
    LAYER 0
    SOURCES
        src/assert.cpp
        src/image.cpp
        src/image_compare.cpp
        src/image_io.cpp
        src/jobs.cpp
        src/log.cpp
        src/result.cpp)

target_include_directories(ezmu_foundation PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated/include>")
target_include_directories(ezmu_foundation SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")

ezmu_add_module_tests(
    NAME foundation
    SOURCES
        tests/assert_tests.cpp
        tests/geometry_tests.cpp
        tests/image_compare_tests.cpp
        tests/image_io_tests.cpp
        tests/image_tests.cpp
        tests/jobs_tests.cpp
        tests/log_tests.cpp
        tests/result_tests.cpp
        tests/version_tests.cpp)

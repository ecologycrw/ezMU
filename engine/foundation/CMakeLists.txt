# ezmu::foundation — layer L0. Depends on nothing else in ezMU.

configure_file(src/version.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/include/ezmu/foundation/version.hpp" @ONLY)

ezmu_add_module(
    NAME foundation
    LAYER 0
    SOURCES
        src/assert.cpp
        src/result.cpp)

target_include_directories(ezmu_foundation PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated/include>")

ezmu_add_module_tests(
    NAME foundation
    SOURCES
        tests/assert_tests.cpp
        tests/result_tests.cpp
        tests/version_tests.cpp)

# Minimal sample: proves an application can link the engine.

ezmu_add_executable(
    NAME ezmu_hello
    LAYER 6
    SOURCES main.cpp
    DEPS ezmu::foundation)

if(EZMU_BUILD_TESTS)
    add_test(NAME samples.hello COMMAND ezmu_hello)
    set_tests_properties(samples.hello PROPERTIES
        PASS_REGULAR_EXPRESSION "^ezMU ${PROJECT_VERSION}")
endif()

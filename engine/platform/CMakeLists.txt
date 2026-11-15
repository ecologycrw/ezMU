# ezmu::platform — layer L1. Windows, raw input, text input/IME, clipboard, clock (ADR 0007).
# SDL3 is private to this module: no SDL header or type appears in include/.

find_package(SDL3 CONFIG REQUIRED)

ezmu_add_module(
    NAME platform
    LAYER 1
    SOURCES
        src/event_translation.cpp
        src/platform.cpp
        src/services.cpp
        src/window.cpp
    PUBLIC_DEPS
        ezmu::foundation
    PRIVATE_DEPS
        SDL3::SDL3)

# Tests push synthetic SDL events, so they link SDL3 directly.
ezmu_add_module_tests(
    NAME platform
    SOURCES
        tests/event_tests.cpp
        tests/platform_tests.cpp
        tests/window_tests.cpp
    DEPS
        SDL3::SDL3)

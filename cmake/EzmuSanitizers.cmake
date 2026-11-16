# Sanitizer support, enabled with EZMU_ENABLE_SANITIZERS and selected with EZMU_SANITIZER:
#   address (default): ASan on MSVC; ASan + UBSan on GCC/Clang.
#   thread:            ThreadSanitizer on GCC/Clang (Linux only; MSVC has no TSan).
# ASan and TSan cannot be combined in one build, hence one selection per preset.

include_guard(GLOBAL)

set(EZMU_SANITIZER "address" CACHE STRING
    "Sanitizer used when EZMU_ENABLE_SANITIZERS is ON: address or thread")
set_property(CACHE EZMU_SANITIZER PROPERTY STRINGS address thread)

if(EZMU_ENABLE_SANITIZERS)
    if(NOT EZMU_SANITIZER MATCHES "^(address|thread)$")
        message(FATAL_ERROR
            "EZMU_SANITIZER must be 'address' or 'thread' (got '${EZMU_SANITIZER}').")
    endif()
    if(EZMU_SANITIZER STREQUAL "thread" AND MSVC)
        message(FATAL_ERROR "EZMU_SANITIZER=thread is not supported by MSVC; use a Linux preset.")
    endif()
    message(STATUS "ezMU sanitizer: ${EZMU_SANITIZER}")
endif()

if(EZMU_ENABLE_SANITIZERS AND MSVC)
    # MSVC's AddressSanitizer is incompatible with runtime checks (/RTC1), which CMake adds to
    # Debug flags by default.
    foreach(flags_var CMAKE_CXX_FLAGS_DEBUG CMAKE_C_FLAGS_DEBUG)
        string(REPLACE "/RTC1" "" ${flags_var} "${${flags_var}}")
    endforeach()
endif()

function(ezmu_target_sanitizers target)
    if(NOT EZMU_ENABLE_SANITIZERS)
        return()
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
        target_link_options(${target} PRIVATE /INCREMENTAL:NO)
        # Third-party libraries from vcpkg are not built with ASan. MSVC's STL container
        # annotations must match across all objects, so disable them; ASan still detects heap,
        # stack and use-after-free errors.
        target_compile_definitions(${target} PRIVATE _DISABLE_STL_ANNOTATION)
    elseif(EZMU_SANITIZER STREQUAL "thread")
        # Third-party libraries are not instrumented; races inside them are not reported, and
        # GPU drivers (uninstrumented threads) are kept out of TSan runs (CI uses -LE gpu).
        set(flags -fsanitize=thread -fno-omit-frame-pointer)
        target_compile_options(${target} PRIVATE ${flags})
        target_link_options(${target} PRIVATE ${flags})
    else()
        set(flags -fsanitize=address,undefined -fno-sanitize-recover=undefined
            -fno-omit-frame-pointer)
        target_compile_options(${target} PRIVATE ${flags})
        target_link_options(${target} PRIVATE ${flags})
    endif()
endfunction()

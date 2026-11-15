# Sanitizer support: ASan on MSVC; ASan + UBSan on GCC/Clang.

include_guard(GLOBAL)

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
    else()
        set(flags -fsanitize=address,undefined -fno-sanitize-recover=undefined
            -fno-omit-frame-pointer)
        target_compile_options(${target} PRIVATE ${flags})
        target_link_options(${target} PRIVATE ${flags})
    endif()
endfunction()

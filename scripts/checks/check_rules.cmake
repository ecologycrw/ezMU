# Architecture rule checks (ADR 0006, 0007, 0009, 0011). Portable: runs anywhere CMake runs.
#
#   cmake -DEZMU_SOURCE_DIR=<repo root> -P scripts/checks/check_rules.cmake
#
# Also registered as the CTest test `architecture.rules`.

cmake_minimum_required(VERSION 3.28)

if(NOT EZMU_SOURCE_DIR)
    get_filename_component(EZMU_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

set(code_roots engine framework protocols tools samples)
set(violations "")

# Returns, in `out`, every file under the code roots matching `patterns` (relative paths).
function(_ezmu_collect out)
    set(result "")
    foreach(root IN LISTS code_roots)
        foreach(pattern IN LISTS ARGN)
            file(GLOB_RECURSE found RELATIVE "${EZMU_SOURCE_DIR}"
                "${EZMU_SOURCE_DIR}/${root}/${pattern}")
            list(APPEND result ${found})
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES result)
    set(${out} "${result}" PARENT_SCOPE)
endfunction()

# Appends to `violations` (in the caller's scope) every line of `file` matching `regex`.
function(_ezmu_check_lines file regex reason)
    file(STRINGS "${EZMU_SOURCE_DIR}/${file}" matches REGEX "${regex}")
    foreach(line IN LISTS matches)
        string(STRIP "${line}" line)
        # Semicolons would split the CMake list entry.
        string(REPLACE ";" "," line "${line}")
        list(APPEND violations "${file}: ${reason}\n      > ${line}")
    endforeach()
    set(violations "${violations}" PARENT_SCOPE)
endfunction()

# --- Rule 1: module build files never reference vcpkg (ADR 0009) -------------------------------
_ezmu_collect(build_files "CMakeLists.txt" "*.cmake")
foreach(file IN LISTS build_files)
    _ezmu_check_lines("${file}" "[Vv][Cc][Pp][Kk][Gg]"
        "module build files must not reference vcpkg, use find_package() (ADR 0009)")
endforeach()

# --- Rules 2–4: include restrictions -----------------------------------------------------------
_ezmu_collect(source_files "*.hpp" "*.cpp" "*.h" "*.inl")
set(include_prefix "^[ \t]*#[ \t]*include[ \t]*")

foreach(file IN LISTS source_files)
    # Rule 2: Vulkan headers only inside engine/rhi (ADR 0006).
    if(NOT file MATCHES "^engine/rhi/")
        _ezmu_check_lines("${file}" "${include_prefix}[<\"](vulkan/|volk\\.h|vk_mem_alloc\\.h)"
            "Vulkan headers are only allowed in engine/rhi (ADR 0006)")
    endif()

    # Rule 3: SDL headers only inside engine/platform (ADR 0007).
    if(NOT file MATCHES "^engine/platform/")
        _ezmu_check_lines("${file}" "${include_prefix}[<\"]SDL[0-9]*/"
            "SDL headers are only allowed in engine/platform (ADR 0007)")
    endif()

    # Rule 4: public headers never reach into private sources (ADR 0011).
    if(file MATCHES "/include/")
        _ezmu_check_lines("${file}" "${include_prefix}\"[^\"]*(src/|\\.\\./)"
            "public headers must not include private sources or use relative parent paths (ADR 0011)")
    endif()
endforeach()

# --- Report ---------------------------------------------------------------------------------------
list(LENGTH violations count)
if(count GREATER 0)
    foreach(v IN LISTS violations)
        message(NOTICE "  - ${v}")
    endforeach()
    message(FATAL_ERROR "[ezMU] ${count} architecture rule violation(s) found.")
endif()

list(LENGTH source_files source_count)
list(LENGTH build_files build_count)
message(STATUS "[ezMU] Architecture rules OK (${source_count} sources, ${build_count} build files).")

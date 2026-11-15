# Module helpers enforcing the ezMU layered architecture (ADR 0011).
#
#   ezmu_add_module(
#       NAME <name>                 # target ezmu_<name>, alias ezmu::<name>
#       LAYER <0..6>
#       [INTERFACE]                 # header-only module
#       [SOURCES <files...>]
#       [PUBLIC_DEPS <targets...>]
#       [PRIVATE_DEPS <targets...>]
#       [ALLOW_SAME_LAYER <ezmu modules...>])
#
#   ezmu_add_module_tests(NAME <module> SOURCES <files...> [DEPS <targets...>])
#
#   ezmu_add_executable(NAME <name> LAYER <5|6> SOURCES <files...> [DEPS <targets...>])
#
#   ezmu_add_aggregate(NAME <name> LAYER <0..6> MODULES <ezmu modules...>)
#                                   # INTERFACE target ezmu_<name>, alias ezmu::<name>
#
# Rules:
#   * An ezmu module may depend only on ezmu modules of a LOWER layer, unless the dependency is
#     listed in ALLOW_SAME_LAYER (same layer only). Violations fail at configure time.
#   * Dependencies must already be defined: add_subdirectory() lower layers first.
#   * Aggregates (e.g. ezmu::engine) bundle modules for executables and external consumers.
#     They are not modules: a module must depend on the specific modules it uses, never on an
#     aggregate. Violations fail at configure time.
#   * Non-ezmu targets (third-party imported targets such as SDL3::SDL3) are not layer-checked;
#     their ownership is governed by ADR 0008.

include_guard(GLOBAL)

set(EZMU_LAYER_NAMES "foundation;rhi-platform;services;runtime;framework;tools;games")

function(_ezmu_apply_defaults target)
    target_compile_features(${target} PUBLIC cxx_std_23)
    ezmu_target_warnings(${target})
    ezmu_target_sanitizers(${target})
endfunction()

# Validates that `owner` (at `owner_layer`) may depend on each entry of `deps`.
function(_ezmu_check_deps owner owner_layer allow_same_layer)
    foreach(dep IN LISTS ARGN)
        if(NOT dep MATCHES "^ezmu::")
            continue()
        endif()
        string(REPLACE "ezmu::" "" dep_name "${dep}")
        if(NOT TARGET "${dep}")
            message(FATAL_ERROR
                "[ezMU] '${owner}' depends on '${dep}', which is not defined (yet). "
                "Define lower layers before higher ones.")
        endif()
        get_target_property(dep_layer "ezmu_${dep_name}" EZMU_LAYER)
        if(dep_layer STREQUAL "dep_layer-NOTFOUND")
            message(FATAL_ERROR "[ezMU] '${dep}' is not an ezMU module (no EZMU_LAYER).")
        endif()
        if(dep_layer GREATER owner_layer)
            message(FATAL_ERROR
                "[ezMU] Layer violation: '${owner}' (L${owner_layer}) depends on '${dep}' "
                "(L${dep_layer}). Dependencies must point to lower layers (ADR 0011).")
        endif()
        if(dep_layer EQUAL owner_layer AND NOT dep_name IN_LIST allow_same_layer)
            message(FATAL_ERROR
                "[ezMU] '${owner}' depends on same-layer module '${dep}' (L${dep_layer}) "
                "without declaring it in ALLOW_SAME_LAYER (ADR 0011).")
        endif()
    endforeach()
endfunction()

# Fails if any dependency of `owner` is an aggregate (modules depend on specific modules).
function(_ezmu_reject_aggregates owner)
    foreach(dep IN LISTS ARGN)
        if(NOT dep MATCHES "^ezmu::" OR NOT TARGET "${dep}")
            continue()
        endif()
        string(REPLACE "ezmu::" "" dep_name "${dep}")
        get_target_property(is_aggregate "ezmu_${dep_name}" EZMU_AGGREGATE)
        if(is_aggregate)
            message(FATAL_ERROR
                "[ezMU] '${owner}' depends on aggregate '${dep}'. Modules must depend on the "
                "specific modules they use; aggregates are for executables and external users.")
        endif()
    endforeach()
endfunction()

function(ezmu_add_module)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "INTERFACE" "NAME;LAYER"
        "SOURCES;PUBLIC_DEPS;PRIVATE_DEPS;ALLOW_SAME_LAYER")

    if(NOT ARG_NAME)
        message(FATAL_ERROR "[ezMU] ezmu_add_module: NAME is required.")
    endif()
    if(NOT ARG_LAYER MATCHES "^[0-6]$")
        message(FATAL_ERROR "[ezMU] ezmu_add_module(${ARG_NAME}): LAYER must be 0..6.")
    endif()

    set(target "ezmu_${ARG_NAME}")
    _ezmu_check_deps("ezmu::${ARG_NAME}" ${ARG_LAYER} "${ARG_ALLOW_SAME_LAYER}"
        ${ARG_PUBLIC_DEPS} ${ARG_PRIVATE_DEPS})
    _ezmu_reject_aggregates("ezmu::${ARG_NAME}" ${ARG_PUBLIC_DEPS} ${ARG_PRIVATE_DEPS})

    if(ARG_INTERFACE)
        add_library(${target} INTERFACE)
        target_include_directories(${target} INTERFACE
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
        target_compile_features(${target} INTERFACE cxx_std_23)
        target_link_libraries(${target} INTERFACE ${ARG_PUBLIC_DEPS})
    else()
        add_library(${target} STATIC ${ARG_SOURCES})
        target_include_directories(${target}
            PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
            PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(${target} PUBLIC ${ARG_PUBLIC_DEPS} PRIVATE ${ARG_PRIVATE_DEPS})
        _ezmu_apply_defaults(${target})
    endif()

    add_library("ezmu::${ARG_NAME}" ALIAS ${target})
    set_target_properties(${target} PROPERTIES
        EZMU_LAYER ${ARG_LAYER}
        EXPORT_NAME ${ARG_NAME})
    set_property(GLOBAL APPEND PROPERTY EZMU_MODULES ${ARG_NAME})
endfunction()

function(ezmu_add_module_tests)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "NAME" "SOURCES;DEPS")
    if(NOT EZMU_BUILD_TESTS)
        return()
    endif()

    set(target "ezmu_${ARG_NAME}_tests")
    add_executable(${target} ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE "ezmu::${ARG_NAME}" ${ARG_DEPS} Catch2::Catch2WithMain)
    _ezmu_apply_defaults(${target})
    catch_discover_tests(${target} TEST_PREFIX "${ARG_NAME}." DISCOVERY_MODE PRE_TEST)
endfunction()

function(ezmu_add_executable)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "NAME;LAYER" "SOURCES;DEPS")
    if(NOT ARG_LAYER MATCHES "^[5-6]$")
        message(FATAL_ERROR "[ezMU] ezmu_add_executable(${ARG_NAME}): LAYER must be 5 or 6.")
    endif()

    _ezmu_check_deps("${ARG_NAME}" ${ARG_LAYER} "" ${ARG_DEPS})
    add_executable(${ARG_NAME} ${ARG_SOURCES})
    target_link_libraries(${ARG_NAME} PRIVATE ${ARG_DEPS})
    set_target_properties(${ARG_NAME} PROPERTIES EZMU_LAYER ${ARG_LAYER})
    _ezmu_apply_defaults(${ARG_NAME})
endfunction()

function(ezmu_add_aggregate)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "NAME;LAYER" "MODULES")
    if(NOT ARG_NAME)
        message(FATAL_ERROR "[ezMU] ezmu_add_aggregate: NAME is required.")
    endif()
    if(NOT ARG_LAYER MATCHES "^[0-6]$")
        message(FATAL_ERROR "[ezMU] ezmu_add_aggregate(${ARG_NAME}): LAYER must be 0..6.")
    endif()

    set(module_names "")
    foreach(module IN LISTS ARG_MODULES)
        if(NOT module MATCHES "^ezmu::")
            message(FATAL_ERROR
                "[ezMU] ezmu_add_aggregate(${ARG_NAME}): '${module}' is not an ezmu:: module.")
        endif()
        string(REPLACE "ezmu::" "" module_name "${module}")
        list(APPEND module_names "${module_name}")
    endforeach()

    # An aggregate may bundle modules of its own layer, so all of them are allowed same-layer.
    _ezmu_check_deps("ezmu::${ARG_NAME}" ${ARG_LAYER} "${module_names}" ${ARG_MODULES})
    _ezmu_reject_aggregates("ezmu::${ARG_NAME}" ${ARG_MODULES})

    set(target "ezmu_${ARG_NAME}")
    add_library(${target} INTERFACE)
    target_link_libraries(${target} INTERFACE ${ARG_MODULES})
    add_library("ezmu::${ARG_NAME}" ALIAS ${target})
    set_target_properties(${target} PROPERTIES
        EZMU_LAYER ${ARG_LAYER}
        EZMU_AGGREGATE TRUE
        EXPORT_NAME ${ARG_NAME})
endfunction()

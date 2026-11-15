# CTest driver for samples.clear (docs/specs/rhi.md, "Testing strategy" 3).
#
#   cmake -DCLEAR_EXE=... -DCHECK_EXE=... -DOUTPUT=<png> -DCOLOR=r,g,b -P run_clear_test.cmake
#
# Runs ezmu_clear for a few frames with synchronization validation, captures the last presented
# image and checks every pixel against the clear color. Prints EZMU_TEST_SKIPPED (the test's
# SKIP_REGULAR_EXPRESSION) when ezmu_clear exits with 77: no display or no Vulkan device.

foreach(var CLEAR_EXE CHECK_EXE OUTPUT COLOR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "run_clear_test.cmake: ${var} is not set")
    endif()
endforeach()

file(REMOVE "${OUTPUT}")
execute_process(
    COMMAND "${CLEAR_EXE}" --frames 10 --capture "${OUTPUT}" --color "${COLOR}"
            --validation sync
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
message("${output}")

if(result EQUAL 77)
    message("EZMU_TEST_SKIPPED: ezmu_clear could not run here (see above)")
    return()
endif()
if(NOT result EQUAL 0)
    message(FATAL_ERROR "ezmu_clear failed with exit code ${result}")
endif()

string(REGEX MATCH "capture: [0-9]+x[0-9]+ ([A-Za-z0-9]+) -> " capture_line "${output}")
if(NOT capture_line)
    message(FATAL_ERROR "ezmu_clear did not report a capture")
endif()
set(format "${CMAKE_MATCH_1}")

execute_process(
    COMMAND "${CHECK_EXE}" "${OUTPUT}" "${format}" "${COLOR}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "the capture does not match the clear color")
endif()

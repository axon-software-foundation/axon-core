cmake_minimum_required(VERSION 3.20)
execute_process(COMMAND "${AXON_EXE}" run "${SOURCE_ROOT}/examples/validation/python_errors/flow_timeout.axon"
    WORKING_DIRECTORY "${SOURCE_ROOT}" RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 8)
string(FIND "${output}${error}" "TimeoutError" diagnostic)
string(FIND "${output}${error}" "SHOULD_NOT_RUN" continued)
if(NOT "${status}" STREQUAL "1" OR diagnostic LESS 0 OR NOT continued LESS 0)
    message(FATAL_ERROR "Expected a controlled deadline failure before main continued: ${status}\n${output}\n${error}")
endif()
message(STATUS "PASS: Python flow budget terminated the child")

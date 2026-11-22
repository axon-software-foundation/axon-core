execute_process(COMMAND "${AXON_EXE}" version RESULT_VARIABLE result
    OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 5)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "version failed: ${result}\n${output}\n${error}")
endif()
string(FIND "${output}" "${EXPECTED_VERSION}" position)
if(position LESS 0)
    message(FATAL_ERROR "CLI does not report VERSION=${EXPECTED_VERSION}: ${output}")
endif()

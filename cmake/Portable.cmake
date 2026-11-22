# No installer or system PATH changes. Only this target creates release archives.
set(AXON_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist" CACHE PATH "Portable archive destination")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackagePortable.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PackagePortable.cmake" @ONLY)
add_custom_target(axon_portable
    COMMAND "${CMAKE_COMMAND}"
        "-DAXON_CONFIG=$<CONFIG>"
        "-DAXON_CLI=$<TARGET_FILE:axon>"
        "-DAXON_RUNTIME_TEST=$<TARGET_FILE:axon_test_runtime>"
        "-DAXON_INTERP_TEST=$<TARGET_FILE:axon_test_interp>"
        -P "${CMAKE_CURRENT_BINARY_DIR}/PackagePortable.cmake"
    DEPENDS axon axon_test_runtime axon_test_interp
    USES_TERMINAL VERBATIM)

if(BUILD_TESTING)
    add_test(NAME cli.version COMMAND "${CMAKE_COMMAND}"
        "-DAXON_EXE=$<TARGET_FILE:axon>" "-DEXPECTED_VERSION=${AXON_VERSION}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_version.cmake")
    set_tests_properties(cli.version PROPERTIES TIMEOUT 10)
endif()

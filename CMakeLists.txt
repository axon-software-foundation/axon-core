cmake_minimum_required(VERSION 3.20)
# VERSION is the authoritative release identifier (including prerelease).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" AXON_VERSION LIMIT_COUNT 2)
if(NOT AXON_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-(alpha|beta|rc)\\.(0|[1-9][0-9]*))?$")
    message(FATAL_ERROR "VERSION must contain one version: X.Y.Z[-alpha.N|-beta.N|-rc.N]")
endif()
set(AXON_BASE_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
set(AXON_CHANNEL "${CMAKE_MATCH_5}")
if(NOT AXON_CHANNEL)
    set(AXON_CHANNEL "stable")
endif()
project(axon_runtime VERSION ${AXON_BASE_VERSION} LANGUAGES C CXX)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
configure_file(cmake/axon_version.h.in generated/axon_version.h @ONLY)

option(AXON_STATIC_RUNTIME "Link MinGW runtime libraries statically for portable Windows packages" OFF)
if(AXON_STATIC_RUNTIME)
    if(NOT MINGW OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "AXON_STATIC_RUNTIME currently requires MinGW GCC on Windows")
    endif()
    add_link_options(-static -static-libgcc -static-libstdc++)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Platform detection ────────────────────────────────────────
if(WIN32)
    message(STATUS "Platform: Windows")
elseif(APPLE)
    message(STATUS "Platform: macOS")
elseif(UNIX)
    message(STATUS "Platform: Linux")
endif()

# ── Compiler flags ───────────────────────────────────────
# Suppress hnswlib size_t conversion warnings on MSVC
if(MSVC)
    add_compile_options(/wd4267 /wd4244 /wd4305)
endif()
if(MSVC)
    add_compile_options(
        /W3 /EHsc /MP
        /D_CRT_SECURE_NO_WARNINGS
        /DNOMINMAX
        /DWIN32_LEAN_AND_MEAN
        /wd4100 /wd4505
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
    if(APPLE)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>)
        add_link_options($<$<LINK_LANGUAGE:CXX>:-stdlib=libc++>)
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# Keep warning checks active. Explicitly opt out only for toolchain diagnosis.
option(AXON_WARNINGS_AS_ERRORS "Treat enabled compiler warnings as errors" ON)
if(AXON_WARNINGS_AS_ERRORS)
    if(MSVC)
        add_compile_options(/WX)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-Werror)
    endif()
endif()

# ── Per-build-type optimization (GCC/Clang only) ─────────────
if(NOT MSVC)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O2)
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-O0 -g)
    endif()
endif()

# ── Threading ─────────────────────────────────────────────────
find_package(Threads REQUIRED)
include(CTest)

# ── Core runtime (interface + header-only impls) ──────────────
add_library(axon_core INTERFACE)
target_include_directories(axon_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/interfaces
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/src
)
target_link_libraries(axon_core INTERFACE Threads::Threads)
# Upstream headers retain their code and licenses. Their diagnostics should not
# suppress warnings in Axon's own translation units or our local ZIP reader.
target_include_directories(axon_core SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/hnswlib
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
)
if(WIN32)
    # Required by both the CLI adapter and semantic_memory in interpreter tests.
    target_link_libraries(axon_core INTERFACE ws2_32)
    target_compile_definitions(axon_core INTERFACE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# ── axon CLI (the main executable) ───────────────────────────
add_executable(axon
    cli/main.cpp
    runtime/src/lexer.cpp
    runtime/src/axon_parser.cpp
    runtime/src/interpreter.cpp
    runtime/src/memory_store.cpp
    runtime/src/semantic_memory.cpp
    runtime/src/document_io.cpp
    runtime/src/office_reader.cpp
    runtime/src/pdf_reader.cpp
    runtime/src/modules.cpp
)
# zlib sources (vendored for Windows compatibility)
set(ZLIB_SRCS
    third_party/zlib/adler32.c
    third_party/zlib/crc32.c
    third_party/zlib/inflate.c
    third_party/zlib/inffast.c
    third_party/zlib/inftrees.c
    third_party/zlib/zutil.c
)

target_sources(axon PRIVATE ${ZLIB_SRCS})
target_link_libraries(axon PRIVATE axon_core)
target_include_directories(axon PRIVATE third_party/zlib "${CMAKE_CURRENT_BINARY_DIR}/generated")

# ── Test runner binary ────────────────────────────────────────
add_executable(axon_test_runtime tests/test_runtime.cpp)
target_link_libraries(axon_test_runtime PRIVATE axon_core)

# ── Interpreter test binary ───────────────────────────────────
add_executable(axon_test_interp
    tests/test_interpreter.cpp
    runtime/src/lexer.cpp
    runtime/src/axon_parser.cpp
    runtime/src/interpreter.cpp
    runtime/src/memory_store.cpp
    runtime/src/semantic_memory.cpp
    runtime/src/document_io.cpp
    runtime/src/office_reader.cpp
    runtime/src/pdf_reader.cpp
    runtime/src/modules.cpp
)
target_sources(axon_test_interp PRIVATE ${ZLIB_SRCS})
target_link_libraries(axon_test_interp PRIVATE axon_core)
target_include_directories(axon_test_interp PRIVATE third_party/zlib)

# ── Automated tests: no Python, Ollama or network required ─────
if(BUILD_TESTING)
    add_test(NAME runtime COMMAND axon_test_runtime)
    add_test(NAME interpreter COMMAND axon_test_interp)
    add_executable(axon_test_exit tests/test_exit.cpp)
    foreach(case_name suites missing_suite failing_suite smoke flows
            import_lex_error import_parse_error missing_import private_import
            nested_import duplicate_import circular_import unsupported_hash
            uncaught_raise function_raise caught_raise division_zero
            task_contract http_unavailable function_arity imported_function_arity)
        add_test(NAME cli.${case_name}
            COMMAND ${CMAKE_COMMAND}
                "-DAXON_EXE=$<TARGET_FILE:axon>"
                "-DEXIT_EXE=$<TARGET_FILE:axon_test_exit>"
                "-DTEST_CASE=${case_name}"
                "-DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/cli-cases"
                "-DVALIDATION_DIR=${CMAKE_CURRENT_SOURCE_DIR}/examples/validation"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.cmake")
        set_tests_properties(cli.${case_name} PROPERTIES TIMEOUT 20)
    endforeach()
    set_tests_properties(runtime interpreter PROPERTIES TIMEOUT 30)
    add_test(NAME docs.examples
        COMMAND ${CMAKE_COMMAND}
            "-DAXON_EXE=$<TARGET_FILE:axon>"
            "-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/docs-cases"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_docs.cmake")
    set_tests_properties(docs.examples PROPERTIES TIMEOUT 180)
endif()

# ── Windows: fix output directories ──────────────────────────
if(WIN32)
    foreach(target axon axon_test_runtime axon_test_interp)
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY         ${CMAKE_BINARY_DIR}
            RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_BINARY_DIR}
            RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}
        )
    endforeach()
endif()

# Developer install remains available; public archives use axon_portable instead.
install(TARGETS axon axon_test_runtime axon_test_interp DESTINATION bin)
install(DIRECTORY runtime/include/ runtime/interfaces/ DESTINATION include/axon)
install(FILES LICENSE VERSION THIRD_PARTY_NOTICES.md DESTINATION share/axon)
install(DIRECTORY licenses/ DESTINATION share/axon/licenses)
install(FILES docs/LANGUAGE.md docs/MODULES.md DESTINATION share/axon/docs)
install(DIRECTORY examples/ DESTINATION share/axon/examples)
# Portable packages use a curated payload and validate the extracted archive.
include(cmake/Portable.cmake)

# ── Optional system tools detection ─────────────────────────
# These are runtime dependencies — Axon works without them
# but specific features require them to be installed.

# Tesseract OCR
find_program(TESSERACT_EXE NAMES tesseract)
if(TESSERACT_EXE)
    execute_process(
        COMMAND ${TESSERACT_EXE} --version
        OUTPUT_VARIABLE TESSERACT_VERSION_OUT
        ERROR_VARIABLE  TESSERACT_VERSION_ERR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    string(REGEX MATCH "[0-9]+\.[0-9]+\.[0-9]+"
           TESSERACT_VERSION "${TESSERACT_VERSION_ERR}${TESSERACT_VERSION_OUT}")
    set(AXON_HAS_TESSERACT TRUE)
else()
    set(AXON_HAS_TESSERACT FALSE)
endif()

# Ollama
find_program(OLLAMA_EXE NAMES ollama)
if(OLLAMA_EXE)
    set(AXON_HAS_OLLAMA TRUE)
else()
    set(AXON_HAS_OLLAMA FALSE)
endif()

# Git (for FetchContent / future use)
find_program(GIT_EXE NAMES git)
if(GIT_EXE)
    set(AXON_HAS_GIT TRUE)
else()
    set(AXON_HAS_GIT FALSE)
endif()

# Pass detected tools to C++ as compile definitions
if(AXON_HAS_TESSERACT)
    target_compile_definitions(axon PRIVATE AXON_HAS_TESSERACT=1)
    if(TARGET axon_test_runtime)
        target_compile_definitions(axon_test_runtime PRIVATE AXON_HAS_TESSERACT=1)
    endif()
    if(TARGET axon_test_interp)
        target_compile_definitions(axon_test_interp PRIVATE AXON_HAS_TESSERACT=1)
    endif()
endif()

# ── Summary ───────────────────────────────────────────────────
message(STATUS "")
message(STATUS "  Axon Runtime v${AXON_VERSION}")
message(STATUS "  C++ standard  : C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler      : ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "  Build type    : ${CMAKE_BUILD_TYPE}")
message(STATUS "")
message(STATUS "  Executables   : axon + axon_test_runtime + axon_test_interp")
message(STATUS "")
message(STATUS "  Optional tools:")
if(AXON_HAS_TESSERACT)
message(STATUS "    Tesseract OCR : FOUND v${TESSERACT_VERSION}  (ocr-> enabled)")
else()
message(STATUS "    Tesseract OCR : NOT FOUND — ocr->read() will show install instructions")
message(STATUS "                    Windows: winget install UB-Mannheim.TesseractOCR")
message(STATUS "                    Linux:   sudo apt install tesseract-ocr")
message(STATUS "                    macOS:   brew install tesseract")
endif()
if(AXON_HAS_OLLAMA)
message(STATUS "    Ollama        : FOUND  (local LLM enabled)")
else()
message(STATUS "    Ollama        : NOT FOUND — https://ollama.com")
endif()
message(STATUS "")
message(STATUS "  Build         : cmake --build build --config Release")
message(STATUS "  Package       : bash scripts/package.sh [CMake configure flags...]")
message(STATUS "  Release guide : docs/RELEASING.md")
message(STATUS "")

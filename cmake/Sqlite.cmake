# Native static SQLite: no database server, sqlite CLI or Python dependency.
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(axon_sqlite
    URL https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip
    URL_HASH SHA256=0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9)
FetchContent_MakeAvailable(axon_sqlite)
add_library(axon_sqlite STATIC "${axon_sqlite_SOURCE_DIR}/sqlite3.c")
target_include_directories(axon_sqlite SYSTEM PUBLIC "${axon_sqlite_SOURCE_DIR}")
target_compile_definitions(axon_sqlite PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION=1 SQLITE_ENABLE_MATH_FUNCTIONS=1)
target_link_libraries(axon_sqlite PUBLIC Threads::Threads)
if(UNIX)
    target_link_libraries(axon_sqlite PUBLIC m)
endif()
# Keep project warnings strict without applying Axon's -Werror policy to upstream C.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(axon_sqlite PRIVATE -Wno-error)
endif()
add_library(axon_sql STATIC runtime/src/sql_client.cpp)
target_link_libraries(axon_sql PUBLIC axon_core axon_sqlite)

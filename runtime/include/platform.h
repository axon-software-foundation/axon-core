/**
 * runtime/include/platform.h
 * ───────────────────────────
 * Cross-platform compatibility layer for Axon runtime.
 * Include this before any platform-specific code.
 *
 * Supported platforms:
 *   - Linux   (GCC 11+ / Clang 13+)
 *   - macOS   (Apple Clang 14+ / Clang 13+)
 *   - Windows (MSVC 2019+ / MinGW-w64)
 */

#ifndef AXON_PLATFORM_H
#define AXON_PLATFORM_H

/* ─── Platform detection ────────────────────────────────────── */
#if defined(_WIN32) || defined(_WIN64)
    #define AXON_OS_WINDOWS 1
    #define AXON_OS_NAME    "Windows"
#elif defined(__APPLE__)
    #define AXON_OS_MACOS   1
    #define AXON_OS_NAME    "macOS"
#elif defined(__linux__)
    #define AXON_OS_LINUX   1
    #define AXON_OS_NAME    "Linux"
#else
    #error "Axon: unsupported operating system"
#endif

/* ─── Compiler detection ────────────────────────────────────── */
#if defined(_MSC_VER)
    #define AXON_COMPILER_MSVC   1
    #define AXON_COMPILER_NAME   "MSVC"
#elif defined(__clang__)
    #define AXON_COMPILER_CLANG  1
    #define AXON_COMPILER_NAME   "Clang"
#elif defined(__GNUC__)
    #define AXON_COMPILER_GCC    1
    #define AXON_COMPILER_NAME   "GCC"
#endif

/* ─── C++ version guard ─────────────────────────────────────── */
#if defined(_MSC_VER)
  #if _MSVC_LANG < 201703L
    #error "Axon requires C++17 or later."
  #endif
#else
  #if __cplusplus < 201703L
    #error "Axon requires C++17 or later."
  #endif
#endif

/* ─── Windows-specific fixes ────────────────────────────────── */
#ifdef AXON_OS_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX           /* prevent min/max macro conflicts */
    #endif
    #include <windows.h>

    /* MSVC: suppress secure CRT warnings */
    #ifdef AXON_COMPILER_MSVC
        #pragma warning(disable: 4996)
    #endif
#endif

/* ─── Force inline ──────────────────────────────────────────── */
#ifdef AXON_COMPILER_MSVC
    #define AXON_INLINE __forceinline
#else
    #define AXON_INLINE __attribute__((always_inline)) inline
#endif

/* ─── Likely / unlikely branch hints ───────────────────────── */
#ifdef AXON_COMPILER_MSVC
    #define AXON_LIKELY(x)   (x)
    #define AXON_UNLIKELY(x) (x)
#else
    #define AXON_LIKELY(x)   __builtin_expect(!!(x), 1)
    #define AXON_UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

/* ─── Thread-local storage ──────────────────────────────────── */
#ifdef AXON_COMPILER_MSVC
    #define AXON_THREAD_LOCAL __declspec(thread)
#else
    #define AXON_THREAD_LOCAL thread_local
#endif

/* ─── Shared library export/import ──────────────────────────── */
#ifdef AXON_OS_WINDOWS
    #ifdef AXON_BUILDING_DLL
        #define AXON_API __declspec(dllexport)
    #else
        #define AXON_API __declspec(dllimport)
    #endif
#else
    #define AXON_API __attribute__((visibility("default")))
#endif

/* ─── Filesystem path separator ─────────────────────────────── */
#ifdef AXON_OS_WINDOWS
    #define AXON_PATH_SEP "\\"
#else
    #define AXON_PATH_SEP "/"
#endif

/* ─── Sleep (milliseconds) ───────────────────────────────────── */
#include <chrono>
#include <thread>
namespace axon {
    inline void sleep_ms(int ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
}

/* ─── Timestamp (milliseconds since epoch) ──────────────────── */
#include <cstdint>
namespace axon {
    inline int64_t now_ms() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()).count();
    }
}

#endif /* AXON_PLATFORM_H */

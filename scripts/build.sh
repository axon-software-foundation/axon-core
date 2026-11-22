#!/usr/bin/env bash
# scripts/build.sh — Linux & macOS build script
# Usage: ./scripts/build.sh [debug|release]

set -e

MODE=${1:-debug}
BUILD_DIR="build"

case "$MODE" in
    debug)   CMAKE_BUILD_TYPE="Debug"   ;;
    release) CMAKE_BUILD_TYPE="Release" ;;
    *)       echo "Usage: $0 [debug|release]"; exit 1 ;;
esac

echo "╔══════════════════════════════════════╗"
echo "║  Axon Runtime Build — $CMAKE_BUILD_TYPE"
echo "╚══════════════════════════════════════╝"

# Detect OS
OS="$(uname -s)"
case "$OS" in
    Linux*)  echo "Platform: Linux" ;;
    Darwin*) echo "Platform: macOS" ;;
    *)       echo "Unknown platform: $OS" ; exit 1 ;;
esac

# Check for cmake
if ! command -v cmake &> /dev/null; then
    echo "❌ cmake not found."
    echo "   Linux: sudo apt install cmake  (Debian/Ubuntu)"
    echo "          sudo dnf install cmake  (Fedora/RHEL)"
    echo "   macOS: brew install cmake"
    exit 1
fi

# Check for C++17 capable compiler
if command -v c++ &> /dev/null; then
    CXX_VER=$(c++ --version 2>&1 | head -1)
    echo "Compiler: $CXX_VER"
fi

# Configure
mkdir -p "$BUILD_DIR"
cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE" \
    -DCMAKE_CXX_STANDARD=17

# Build
cmake --build "$BUILD_DIR" --parallel "$(nproc 2>/dev/null || sysctl -n hw.ncpu)"

echo ""
echo "✅ Build complete → $BUILD_DIR/axon_test_runtime"
echo ""
echo "Run tests:"
echo "  ./$BUILD_DIR/axon_test_runtime"

# cmake -S . -B build -DCMAKE_BUILD_TYPE="Debug" -DCMAKE_CXX_STANDARD=17
# cmake --build "build" --parallel "$(nproc 2>/dev/null || sysctl -n hw.ncpu)"

#
## Opción 1 — winget (Windows 11/10 moderno)
#winget install UB-Mannheim.TesseractOCR
#
## Opción 2 — instalador directo
## https://github.com/UB-Mannheim/tesseract/wiki
## Descarga el .exe, siguiente, siguiente, listo
#
## Verificar instalación
#tesseract --version

#
## Buscar el ejecutable
#Get-ChildItem -Path "C:\Program Files" -Recurse -Filter "tesseract.exe" -ErrorAction SilentlyContinue
#
## O en Program Files (x86)
#Get-ChildItem -Path "C:\Program Files (x86)" -Recurse -Filter "tesseract.exe" -ErrorAction SilentlyContinue
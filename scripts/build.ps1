# scripts/build.ps1 — Windows build script
# Usage: .\scripts\build.ps1 [debug|release]
# Requires: Visual Studio 2019+ or MinGW-w64 + CMake

param(
    [string]$Mode = "debug"
)

$BuildDir = "build"

switch ($Mode.ToLower()) {
    "debug"   { $CMakeBuildType = "Debug"   }
    "release" { $CMakeBuildType = "Release" }
    default   {
        Write-Host "Usage: .\build.ps1 [debug|release]"
        exit 1
    }
}

Write-Host "╔══════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║  Axon Runtime Build — $CMakeBuildType" -ForegroundColor Cyan
Write-Host "╚══════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host "Platform: Windows"

# Check cmake
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host "❌ cmake not found." -ForegroundColor Red
    Write-Host "   Install from: https://cmake.org/download/"
    Write-Host "   Or via winget: winget install Kitware.CMake"
    exit 1
}

# Check for Visual Studio or MinGW
$VSFound   = Get-Command cl   -ErrorAction SilentlyContinue
$MinGWFound = Get-Command g++ -ErrorAction SilentlyContinue

if ($VSFound) {
    Write-Host "Compiler: MSVC (Visual Studio)"
    $Generator = "Visual Studio 17 2022"
} elseif ($MinGWFound) {
    Write-Host "Compiler: MinGW-w64 GCC"
    $Generator = "MinGW Makefiles"
} else {
    Write-Host "❌ No C++ compiler found." -ForegroundColor Red
    Write-Host "   Option 1: Install Visual Studio 2019+ with C++ workload"
    Write-Host "   Option 2: Install MinGW-w64 via https://www.mingw-w64.org/"
    exit 1
}

# Configure
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
cmake -S . -B $BuildDir `
    -G $Generator `
    -DCMAKE_BUILD_TYPE=$CMakeBuildType `
    -DCMAKE_CXX_STANDARD=17

if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ CMake configuration failed." -ForegroundColor Red
    exit 1
}

# Build
cmake --build $BuildDir --config $CMakeBuildType

if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Build failed." -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "✅ Build complete → $BuildDir\axon_test_runtime.exe" -ForegroundColor Green
Write-Host ""
Write-Host "Run tests:"
Write-Host "  .\$BuildDir\axon_test_runtime.exe"

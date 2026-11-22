#!/usr/bin/env bash
# Git Bash / Linux. All additional arguments go to CMake configure.
set -euo pipefail
AXON_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AXON_BUILD="${AXON_PACKAGE_BUILD_DIR:-$AXON_ROOT/build-alpha}"
AXON_STATIC=OFF
case "${OSTYPE:-}" in msys*|cygwin*|win32*) AXON_STATIC=ON ;; esac
for AXON_TOOL in cmake ctest; do
    if ! command -v "$AXON_TOOL" >/dev/null 2>&1; then
        echo "Missing $AXON_TOOL in PATH. Install CMake (includes CTest)." >&2
        exit 1
    fi
done
cmake -S "$AXON_ROOT" -B "$AXON_BUILD" "$@" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DAXON_STATIC_RUNTIME="$AXON_STATIC"
cmake --build "$AXON_BUILD" --config Release --parallel 2
ctest --test-dir "$AXON_BUILD" -C Release --output-on-failure
cmake --build "$AXON_BUILD" --config Release --target axon_portable

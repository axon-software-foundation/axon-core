#!/usr/bin/env bash
# Run from Git Bash, Linux or macOS. Requires CMake, CTest and a C++17 compiler.
# Usage: bash scripts/validate.sh [Debug|Release] [extra CMake configure flags...]
set -euo pipefail
AXON_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AXON_MODE="${1:-Debug}"
if [[ $# -gt 0 ]]; then shift; fi
case "$AXON_MODE" in
    Debug|Release) ;;
    *) echo 'Usage: bash scripts/validate.sh [Debug|Release] [CMake flags...]' >&2; exit 2 ;;
esac
AXON_BUILD="${AXON_BUILD_DIR:-$AXON_ROOT/build-validation}"
for AXON_TOOL in cmake ctest; do
    if ! command -v "$AXON_TOOL" >/dev/null 2>&1; then
        echo "Missing $AXON_TOOL in PATH. Install CMake (includes CTest)." >&2
        exit 1
    fi
done
cmake -S "$AXON_ROOT" -B "$AXON_BUILD" \
    -DCMAKE_BUILD_TYPE="$AXON_MODE" -DBUILD_TESTING=ON "$@"
cmake --build "$AXON_BUILD" --config "$AXON_MODE" --parallel 2
ctest --test-dir "$AXON_BUILD" -C "$AXON_MODE" --output-on-failure
AXON_EXE="$AXON_BUILD/axon"
if [[ -f "$AXON_BUILD/axon.exe" ]]; then AXON_EXE="$AXON_BUILD/axon.exe"; fi
"$AXON_EXE" test
"$AXON_EXE" run "$AXON_ROOT/examples/validation/functions.axon"
"$AXON_EXE" run "$AXON_ROOT/examples/validation/main.axon"
"$AXON_EXE" run "$AXON_ROOT/examples/validation/tasks.axon"
"$AXON_EXE" run "$AXON_ROOT/examples/validation/delays.axon"
echo 'PASS: automated tests and manual validation examples completed.'

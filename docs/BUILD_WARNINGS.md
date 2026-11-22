# Compiler warning policy

`AXON_WARNINGS_AS_ERRORS` defaults to ON: enabled warnings fail GCC/Clang builds with `-Werror`
and MSVC builds with `/WX`. GCC/Clang retain `-Wall -Wextra`; existing targeted compiler flags remain.
Do not add global warning suppression to make CI pass. Temporary toolchain diagnosis may use OFF,
but release acceptance must explicitly restore ON, including in existing CMake caches.

HNSWlib/stb include directories are SYSTEM headers; their implementation is not thereby proven correct.
The local ZIP reader under `third_party/miniz` is not classified as SYSTEM. Vendored code retains its notices.
Earlier cleanup removed unused code/variables, protected MSVC-only pragmas, clarified PDF control flow
and replaced a truncating ZIP filename copy with an explicitly terminated buffer copy.

The cross-platform CI fix also checks OCR working-directory/process results and uses native filesystem
cleanup; graph tests initialize nested `AgentID` aggregates explicitly; Apple libc++ options apply only
to C++ compilation/linking, not vendored C sources. Do not reintroduce the old ignored `getcwd`/`system` returns.

The baseline CI at `7a1537f` passed Linux GCC Debug/Release, Windows UCRT64 Debug/Release,
experimental Apple Clang and Docker. Local Linux Release was also tested with fortified headers
(`-D_FORTIFY_SOURCE=2`) and strict warnings. This does not certify MSVC or every compiler/version.

Windows Git Bash packaging check:

```bash
bash scripts/package.sh -G Ninja \
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe \
  -DAXON_WARNINGS_AS_ERRORS=ON
```

If a compiler reports another warning, preserve the full diagnostic and fix its cause.
Runtime failures, Windows application-control blocks and compiler warnings require different diagnoses.

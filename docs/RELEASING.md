# Build and validate a prerelease

VERSION is the authoritative version for CMake, CLI and archive names. Current preparation:
`0.4.0-alpha.2`. The numeric project version is derived; do not edit a second CLI version macro.
Build scripts do not create tags or publish releases. See GITHUB_LAUNCH.md for the publication sequence.

## Windows Git Bash: complete build and validation

Requirements: native x64 MSYS2 UCRT64 GCC, CMake 3.20+/CTest, Ninja, OpenSSL for local TLS fixtures,
and Python 3.9+ for the full test suite. Native Axon/SQLite do not require Python at runtime.
Python/NSIS/WiX are not bundled; NSIS/WiX are not needed. Configure from the repository root:

```bash
export PATH="/c/msys64/ucrt64/bin:$PATH"
export AXON_PYTHON="$(python -c 'import sys; print(sys.executable)' | tr -d '\r')"
cmake -S . -B build-alpha2 -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe -DOPENSSL_ROOT_DIR=C:/msys64/ucrt64 -DPython3_EXECUTABLE="$AXON_PYTHON" -DAXON_WARNINGS_AS_ERRORS=ON
cmake --build build-alpha2 --config Release --parallel 4
ctest --test-dir build-alpha2 -C Release --output-on-failure
./build-alpha2/axon.exe version
./build-alpha2/axon.exe run examples/validation/flow_database.axon
./build-alpha2/axon.exe run examples/validation/python_interop.axon
./build-alpha2/axon.exe run examples/validation/sqlite_test.axon
```

Expected: 53/53 CTest and CLI version 0.4.0-alpha.2. Optional live Ollama, HTTP and scikit-learn
acceptance require their documented dependencies; they are separate from service-free CI.

## Windows portable package: complete command

This configures/builds/tests even if the package build directory was deleted:

```bash
export PATH="/c/msys64/ucrt64/bin:$PATH"
export AXON_PYTHON="$(python -c 'import sys; print(sys.executable)' | tr -d '\r')"
AXON_PACKAGE_BUILD_DIR="$PWD/build-alpha2-package" bash scripts/package.sh -G Ninja -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe -DOPENSSL_ROOT_DIR=C:/msys64/ucrt64 -DPython3_EXECUTABLE="$AXON_PYTHON" -DAXON_WARNINGS_AS_ERRORS=ON
```

The script selects Release and static MinGW runtime, runs CTest, inspects PE imports, stages notices,
creates/extracts the archive, compares hashes and runs native smoke tests with a reduced Windows PATH.
External Python acceptance also runs using the explicitly selected host interpreter. Python is not
bundled. Unknown DLL dependencies or missing toolchain notices fail packaging.

Outputs:
- dist/axon-v0.4.0-alpha.2-windows-x64.zip
- dist/axon-v0.4.0-alpha.2-windows-x64.zip.sha256

The archive includes LICENSE, VERSION, BUILD_INFO.txt, MANIFEST.sha256, references/examples and
third-party notices. Never rename an alpha.1 archive or reuse its checksum. An old file in dist is
not proof of a successful new build. Source commit and dirty state are recorded when Git is available.

## Linux and macOS

Linux requires GCC/Clang, CMake/CTest, a build tool, system libcurl/OpenSSL development files and
Python 3.9+ for tests. The first configure fetches pinned SQLite unless verified sources are supplied.

```bash
AXON_PACKAGE_BUILD_DIR="$PWD/build-alpha2-package" bash scripts/package.sh -G Ninja -DAXON_WARNINGS_AS_ERRORS=ON
```

The native archive is dist/axon-v0.4.0-alpha.2-linux-x86_64.tar.gz on x86_64, with a matching .sha256.
System library requirements remain; no cross-distribution baseline is claimed. Experimental macOS
CI runs native tests but portable packaging rejects macOS. MSVC/additional architectures are not
established. Docker is a separate Linux build/runtime workflow, not a registry publication.

## Exact-tag acceptance

After the version-preparation PR is merged, tag the reviewed clean commit and wait for its CI.
Accept freshly built artifacts from that exact tag, verify their hashes, version and BUILD_INFO
commit/state, and repeat PORTABLE.md, preferably on a second Windows machine without a toolchain.
Confirm optional live generation separately; automated fixtures do not prove model behavior.
Record actual results, attach only accepted assets, then publish the prerelease. Do not overwrite
alpha.1 assets or advertise macOS packages that do not exist.

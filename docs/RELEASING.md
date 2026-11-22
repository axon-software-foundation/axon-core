# Build an alpha release candidate

These commands build and test local candidates; they do not create tags, push or publish releases.
The official repository is `axon-software-foundation/axon-core`.
See [GITHUB_LAUNCH.md](GITHUB_LAUNCH.md) for the publication sequence.

## Version and toolchain

VERSION is the single full version (`0.4.0-alpha.1`). CMake derives its numeric project version and
CLI header. Do not edit an independent CLI version macro. Accepted forms are `X.Y.Z`,
`X.Y.Z-alpha.N`, `X.Y.Z-beta.N`, `X.Y.Z-rc.N`, with no leading zeroes.

Windows candidates require native x64 MSYS2 UCRT64 MinGW GCC, CMake 3.20+, Ninja and static runtime
libraries/notices. Python, NSIS and WiX are not needed. Only system DLL imports are accepted;
unknown dependencies fail packaging rather than creating an incomplete ZIP.

## Windows Git Bash, from the source root

```bash
bash scripts/package.sh -G Ninja \
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe \
  -DAXON_WARNINGS_AS_ERRORS=ON
```

Adjust compiler paths if necessary. The builder reads CRT/winpthreads notices from the actual toolchain's
`share/licenses/`; missing notices fail packaging. `build-alpha` is separate from validation builds.
Set `AXON_PACKAGE_BUILD_DIR` to a new path if changing compiler/generator.

The script configures Release, builds, runs CTest and calls `axon_portable`. Packaging stages the CLI,
two test executables, selected examples, current references and notices. It inspects Windows PE imports,
hashes contents, archives and extracts them, compares hashes, then runs version/suites/examples and a
controlled error from an unrelated directory containing spaces. Windows extracted checks use a PATH
restricted to Windows/System32.

Outputs in `dist/`:

- `axon-v0.4.0-alpha.1-windows-x64.zip`
- `axon-v0.4.0-alpha.1-windows-x64.zip.sha256`

The archive contains `LICENSE`, `VERSION`, `BUILD_INFO.txt`, `MANIFEST.sha256` and third-party notices.
Source commit/state are recorded when available; a source ZIP without Git metadata is marked unavailable.
Ollama, models, Tesseract, private fixtures and user files are not bundled.
An old archive left in dist is not evidence that a failed rebuild produced a new candidate.

## Linux and macOS

Linux can use `bash scripts/package.sh` to build a native `.tar.gz` validation archive. The archive
requires system libraries and has no declared cross-distribution compatibility guarantee.
The CI Docker runtime path is also tested; see [DOCKER_BUILD.md](../DOCKER_BUILD.md).
Experimental native macOS CI passes at the recorded baseline, but the portable target explicitly
rejects macOS. MSVC packaging and additional architectures are not established.

## Acceptance

Build from the exact reviewed tag commit with a clean checkout. Repeat [PORTABLE.md](PORTABLE.md) on
the exact new artifact and record OS/architecture/version/checksum/results. Finish third-party provenance
and private reporting-channel review before publication. Every tag/asset must match VERSION and the
changelog. A passing build is not code signing or bit-for-bit reproducibility across machines.

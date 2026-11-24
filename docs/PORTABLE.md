# Test a portable candidate

Keep the entire extracted folder, including `bin/`; the package is not just the main executable.
It does not require a global PATH installation. Use a folder outside the source checkout and,
for Windows acceptance, preferably a second Windows x64 computer without MSYS2/GCC/Git.

## Windows PowerShell, from the extracted root

```powershell
.\bin\axon.exe version
.\bin\axon.exe test
.\bin\axon.exe run .\examples\validation\functions.axon
.\bin\axon.exe run .\examples\validation\main.axon
.\bin\axon.exe run .\examples\validation\tasks.axon
.\bin\axon.exe run .\examples\validation\delays.axon
.\bin\axon.exe run .\examples\flows.axon
```

Version must match VERSION; both suites and the PASS examples must succeed. `axon test` requires
both test executables beside `axon.exe`. Optional real generation requires a separately installed,
running Ollama service and `ollama pull llama3.2`:

```powershell
.\bin\axon.exe run .\examples\validation\ollama_tasks.axon
```

Skipped calls/passthrough do not establish model integration.

## Linux native validation archive

```bash
./bin/axon version
./bin/axon test
./bin/axon run examples/validation/tasks.axon
```

System libraries are not bundled; a minimum glibc/distribution compatibility baseline is not established.
There is no macOS archive target yet.

## Verify integrity

From the directory containing the archive and its checksum, in Git Bash:

```bash
sha256sum -c axon-v0.4.0-alpha.1-windows-x64.zip.sha256
```

From the extracted root, Git Bash/Linux can check `sha256sum -c MANIFEST.sha256`.
`BUILD_INFO.txt` records compiler/platform and source commit/clean state when available.
A new build needs its own checksum and acceptance; do not reuse an earlier candidate's hash.
Checksums verify bytes, not publisher identity. Signing remains pending.
See [RELEASE_NOTES.md](RELEASE_NOTES.md) for limitations.

## HTTP runtime dependencies

The Windows HTTP client is a pinned static libcurl build using Schannel and the Windows certificate
store. OpenSSL is needed to build/run the source-tree TLS fixture tests, not to run the Windows CLI.
Unix archives use system libcurl/TLS libraries and system trust roots; install the matching runtime
packages. Docker's runtime image installs libcurl. The curl license is included in portable notices.

## Optional external Python

Python is not bundled. To execute Python blocks, install Python 3.9+ and set AXON_PYTHON to its
executable (or use the platform default on PATH). Install required packages into that interpreter.
Native Axon and SQLite do not require it. The packaging check uses the build host's discovered
Python to run the extracted Python acceptance example from an unrelated directory with spaces;
this verifies interop without claiming a self-contained Python distribution. See PYTHON_INTEROP.md.

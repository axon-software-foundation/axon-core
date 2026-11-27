# Changelog

## [Unreleased]

No changes recorded after the alpha.2 preparation boundary.

## [0.4.0-alpha.2]

### Added

- Cooperative flow/agent/task deadlines, host cancellation and interruptible sleep/provider calls.
- Native parallel blocks and pipeline groups with isolated snapshots, bounded workers, source-order
  publication and sibling cancellation. Failed groups do not commit branch bindings.
- Public HTTP/HTTPS GET, POST and JSON calls with typed responses, verified TLS, resource limits,
  inherited budgets and opt-in same-origin redirects.
- Native SQLite connections, named lookup, bound parameters, typed rows, explicit transactions,
  per-connection concurrency, cancellation and controlled lifecycle.
- Contextual flow.database registries supporting multiple configuration aliases; pure sql->validate.
- Explicit multiline Python inputs, ordinary Python return and typed context-local __result__.
  Every block runs in a fresh external process; Python tuples become Axon lists.
- Controlled Python/shell process lifetimes, separate stdout/stderr and actionable errors/tracebacks.
- Executable documentation checks, SQL/HTTP/process fixtures and complete Axon acceptance examples.

### Changed

- User-defined functions require exact positional arity before arguments/body side effects execute.
- Provider failures raise errors rather than silently returning the input or null.
- Shell blocks publish stdout/stderr/exit_code dictionaries. Existing shell aliases and the direct
  stdout-string return from shell->exec remain available.
- Python blocks require explicit parameter bindings and return; implicit injection, stdout sentinels
  and stringified results are replaced. Python 3.9+ is required for interop and full build tests,
  but not for native Axon/SQLite execution.
- flow.database now maps aliases to configurations; the old flat configuration must be wrapped
  in an alias such as primary. Registry declarations do not open connections.

### Fixed

- Native PDF progress regression, waiting-worker scheduler shutdown and portable warning failures.
- Canonical import diagnostics, controlled raises and existing task input/output validation.
- Temporary-directory collisions in external interop/OCR paths; synchronized remaining native services.
- Windows batch command echo contaminating shell stdout.
- Native Windows Python executable paths being parsed as CMake escapes during portable packaging.
- Flow-database and Python test source/script resolution without relying on sibling source folders.

### Documentation and packaging

- Align English language/module references with tested behavior; retain historical examples as such.
- Preserve the single root MIT license and dependency/provenance notices.
- Include current examples/references and checksums in extracted portable acceptance.
- Validate Linux/Windows Debug and Release, experimental macOS and Linux Docker in CI.
  Native platform/ABI limits and exact-tag artifact acceptance remain documented.

### Migration

See docs/PYTHON_INTEROP.md, docs/INTEROP.md and docs/RELEASE_NOTES.md. This is a prerelease:
syntax/APIs may change. DuckDB, PostgreSQL, MCP and other roadmap integrations are not included.

## [0.4.0-alpha.1]

First published prerelease. The scope and evidence below describe that historical baseline.

### Included

- One `VERSION` source for CLI, CMake and archive names.
- Functions/closures, canonical relative imports, diagnostics and CLI exit codes.
- Whole-agent, individual-task and pipeline calls; task input/output references and invocation isolation.
- Task/agent/flow post-delays and controlled uncaught-raise handling.
- Scheduler shutdown synchronization fix and waiting-worker regression.
- Strict warning builds, Linux fortified-header OCR checks, explicit graph-test aggregate initialization
  and C++-only Apple libc++ flags.
- Windows x64 static MinGW portable packaging, notices, checksums and extracted-package smoke tests.
- CI for Linux, Windows, experimental macOS and Docker; contribution/security/report templates.

### Evidence and limits

The public-main baseline `7a1537f` passed all six Axon CI jobs, including the experimental macOS job.
Baseline suites contained 31 runtime cases, 78 interpreter cases and 22 CTest entries.
Windows portable execution and live Ollama task chaining were separately confirmed manually.
New Markdown checks are listed under Unreleased until reviewed and merged.
Linux archives have no declared cross-distribution ABI baseline. macOS has no package target.
HTTP/REST, MCP, timeout enforcement, actual parallel language execution and additional providers
remain pending. Model weights, Ollama and Tesseract are not bundled. Signing is pending.

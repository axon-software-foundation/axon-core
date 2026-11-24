# Changelog

## [Unreleased]

- Add native SQLite connections with flow/main/function configuration, named lookup, bound parameters,
  typed results, query/execute/script, explicit transactions and close semantics.
- Coordinate connections per interpreter family and prevent sibling branches from interleaving an
  active transaction; support inherited query budgets/cancellation and bounded materialized results.
- Execute raw SQL blocks on an explicitly selected connection; preserve braces in SQL literals/comments.
  Replace the storage query placeholder with an explicit-connection SQL alias.
- Pin static SQLite source provenance and add native, interpreter and CLI regressions. DuckDB then
  PostgreSQL remain subsequent driver blocks; Python/shell completion is tracked independently.

- Implement public GET/POST/JSON and verified HTTPS using an independent libcurl transport, with structured
  responses, explicit errors, inherited deadlines/cancellation, limits and opt-in same-origin redirects.
- Add controlled local HTTP/TLS fixtures and interpreter request-overlap coverage; register PDF progress
  checks in CTest and normalize source/test paths for builds outside the source directory.
- Build pinned minimal static curl/Schannel on Windows; use system libcurl on Unix. Document build/test
  dependencies, runtime packaging and upstream notices. Published binaries and VERSION remain unchanged.

- Execute parallel blocks and pipeline groups using bounded native scheduler jobs, lexical/closure
  snapshots, source-order result publication and cooperative sibling cancellation.
- Preserve pipeline groups' last-declared-result convention; publish only top-level branch assignments.
  Parallel code must use independent branches, not rely on prior sequential sibling side effects.
- Synchronize existing native services and legacy provider adapters; Ollama opts into concurrent requests.
  Use unique temporary directories for OCR/Python interop and buffer branch print output in source order.
- Correct Python interop script indentation and avoid shadowing Python print with Axon's built-in marker;
  escape multiline injected strings; retain the existing string result contract and add a manual interop acceptance example.
- Add 21 interpreter concurrency cases, two scheduler cases, two CLI cases and parallel validation examples.

- Enforce cooperative flow/agent/task timeouts, including inherited deadlines and post-delays;
  add host cancellation and interruptible sleep/Ollama socket operations.
- Raise catchable provider failures instead of returning input passthrough or silent null. Preserve
  valid non-LLM null results and existing graph failure routes; unhandled graph raises fail the CLI.
- Use GET and HTTP status for Ollama health; reject incomplete response bodies and missing chat fields.
- Add 17 interpreter cases, nine real loopback transport cases, two CLI checks and a checked timing
  example. Document non-preemptible native/interop operations and add manual live-model recovery validation.

- Require exact positional arity for user-defined functions, including closures, anonymous/arrow
  functions and imported functions. Invalid calls raise a catchable `ArityError` before evaluating
  arguments or executing the body. Existing code that relied on missing arguments becoming `null`
  or extra arguments being ignored must pass the declared number of arguments explicitly.
- Add eight interpreter regressions, two CLI regressions and a checked documentation example
  for argument counts, call-line diagnostics and recovery.

- Replace outdated language claims with English references tied to the current interpreter and tests.
- Add CTest checks of actual README/language/module Markdown examples: execution for self-contained
  programs, syntax-only for examples requiring a model service.
- Keep one root MIT `LICENSE`; update installation, portable packaging and third-party references.
- Include current language/module references in portable archives.

## [0.4.0-alpha.1] — release candidate

This heading records candidate scope; it does not establish a published tag or release.

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

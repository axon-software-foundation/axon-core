# Changelog

## [Unreleased]

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

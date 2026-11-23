# Validation and evidence

Documentation baseline: public `main` commit `7a1537f57cb52fb022a8bccb839f527cbc971901`,
version `0.4.0-alpha.1`. This document describes the current repository workflow, not a sequence
of patches to copy over an old ZIP.

## Run the baseline

From the source root on Linux or experimental macOS:

```bash
bash scripts/validate.sh Release -DAXON_WARNINGS_AS_ERRORS=ON
```

Windows Git Bash with MSYS2 UCRT64 GCC/Ninja:

```bash
bash scripts/validate.sh Release -G Ninja \
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe \
  -DAXON_WARNINGS_AS_ERRORS=ON
```

CMake 3.20+, CTest, a C++17 compiler and the selected build tool must be installed.
Use a new `AXON_BUILD_DIR` when changing generator/compiler. The script configures/builds,
runs CTest and both suites through `axon test`, then executes functions, imports, tasks and
delays examples. It stops on failure. No model, Python, OCR tool or network service is required.
The baseline's external-service detection can print an informational Ollama-unavailable message.

## What the checks establish

- Runtime suite: 33 cases covering graph traversal, actor state, scheduler and runtime/session memory.
- Interpreter suite: 129 cases covering expressions, functions, task input/output and delays, among others.
  Eight new cases cover strict user-function arity, closures, recovery and rejection before side effects.
- CTest: 32 entries, including the two suites, CLI regressions/version and `docs.examples`.
  The new CLI cases check direct and imported arity failures, exit status and call-line diagnostics.
  CTest reuses suites; do not add these counts as independent unit tests.
- `docs.examples` reads the actual `axon` fences from README, LANGUAGE and MODULES. Each is marked
  `run` or `check`: self-contained examples execute with assertions, model-dependent examples receive
  syntax checks only. An unmarked fence, parser error, execution error or timeout fails the check.
- Seventeen execution-control cases cover budgets, inheritance, cancellation and provider failures.
- `provider.transport` adds nine real loopback socket cases; no model or external network is used.
- `cli.flow_timeout` and `cli.provider_missing` assert exit status 1 for unhandled errors.
- CLI negative cases must appear passed: the test asserts the expected failure/diagnostic of the program.
- `cli.http_invalid_url` rejects non-HTTP schemes without an external request.
- `http.transport` has 14 local HTTP/TLS cases, including trusted CA and failed trust/hostname checks.
  TLS fixtures generate a temporary PKI and serve a signed CRL over loopback for Schannel.
  The revoked-chain case checks OpenSSL CRL validation everywhere and the HTTP client on Schannel.
- Interpreter HTTP cases include strict JSON/options, catchable timeouts and rendezvous-proven request overlap.
- `pdf.progress` now registers the standalone PDF regression in CTest.
- A recording provider in interpreter tests verifies actual constructed messages; it is not a production
  provider and is not evidence of live model responses.

Run only documentation checks (use your configured build type):

```bash
ctest --test-dir build-validation -C Release --output-on-failure -R '^docs\.examples$'
```

Individual examples are in [examples/validation](../examples/validation).
`axon check` only checks the entry file; use `run` for import resolution and runtime behavior.
The runtime suite includes intentional invalid actor transitions; their diagnostic is expected
when the enclosing negative test passes.

## Recorded platform evidence

The public HTTP working-tree change passed strict local Linux GCC 14.2 Release tests. The HTTP/TLS
transport suite also passed against the pinned minimal curl 8.23.0 build. The existing 33 runtime
cases and expanded 129 interpreter cases remain part of CTest. These observations do not validate
Windows Schannel, macOS, Docker or the final packaged download; those require this change's CI.

The parallel change passed local strict Linux GCC 14.2 Release validation. Its added cases include
21 interpreter concurrency/isolation cases, two generic scheduler cases and two CLI checks.
The 124-case interpreter and 33-case runtime suites were also exercised with ThreadSanitizer;
no races were reported in those cases. Local optional integration checks also exercised real Tesseract
OCR and Python in parallel branches, including multiline context injection and print/result handling.
This is focused evidence, not proof that arbitrary native calls or third-party adapters are thread-safe.
Windows/macOS and the live Ollama parallel example remain manual/CI acceptance requirements.

The execution-control patch was built locally on Linux GCC 14.2 Release with warnings as errors
and fortified headers. Its 28 CTest entries passed, including 103 interpreter cases and nine transport
fixture cases. Windows/macOS and real Ollama failure/recovery require acceptance of this patch.

The strict user-function arity change was locally built on Linux with GCC 14.2, Release,
`AXON_WARNINGS_AS_ERRORS=ON` and `_FORTIFY_SOURCE=2`. All 25 CTest entries passed, including
31 runtime cases, 86 interpreter cases and the checked Markdown programs. Windows and macOS
validation of this change still belongs to the PR's CI; previous runs do not certify this patch.

The baseline [Axon CI run](https://github.com/axon-software-foundation/axon-core/actions/runs/44988638334)
at `7a1537f` passed all six jobs: Linux GCC Debug/Release, Windows UCRT64 Debug/Release,
experimental macOS Apple Clang and Linux Docker. That run had 22 CTest entries before `docs.examples`.
The documentation/package changes need a new CI run; old checks do not validate new files.

Windows strict GCC 16.2 builds and the portable were also tested manually on a second Windows machine.
Real Ollama task chaining was manually confirmed on Windows using `llama3.2`.
Local Linux OCR checks covered reading a synthetic image and rejecting failed/stale output;
OCR is not part of the baseline CI matrix. These observations do not certify every tool/model version.

## Optional integration checks and remaining gaps

```bash
./build-validation/axon run examples/validation/ollama_tasks.axon
```

Use `.exe` on Windows. Start Ollama and install `llama3.2` first. Confirm actual model calls and
inspect that the second task receives the first output; generated prose is nondeterministic.
A skipped call or passthrough result does not count as success.

Further integration coverage is needed for PDF/Office fixtures, OCR languages, images,
semantic retrieval/persistence, all memory TTL cases, shell/Python platforms and model/GPU options.
macOS packaging, Linux distro/ABI compatibility, signing and other architectures remain open.
A syntax-only sweep of all 31 existing `.axon` examples passed 30; `examples/flows_v1.axon`
failed parsing because it retains placeholders/old declarations. See [examples/README.md](../examples/README.md).
Historical examples outside the validation directory may require files/services or contain old syntax;
they are not silently treated as validated release examples. Their migration is tracked in the roadmap.

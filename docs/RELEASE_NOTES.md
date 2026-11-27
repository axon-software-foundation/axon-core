# Axon 0.4.0-alpha.2

This prerelease expands local orchestration with native parallel execution, HTTP/HTTPS, SQLite
and explicit Python interoperability. Syntax and APIs remain subject to change.

## Highlights

- Enforced flow/agent/task execution budgets, host cancellation and clear provider errors.
- Native parallel branches with isolated environments, ordered publication and sibling cancellation.
- Public HTTP/HTTPS GET, POST and JSON calls with typed responses, TLS verification and limits.
- Native SQLite connections, parameterized queries, transactions, named handles and typed results.
- Multiple database configuration aliases per flow, validated without opening connections.
- External Python blocks with explicit multiline inputs, ordinary return and typed __result__;
  tuples become lists. Independent processes support cancellation and report Python tracebacks.
- Shell stdout/stderr/exit status use a structured result, with Windows command-echo correction.
- PDF progress, strict cross-platform build, source-path and portable Python-path fixes.
- Executable documentation and expanded native/CLI acceptance fixtures.

## Migration from alpha.1

- Functions now require their declared number of positional arguments.
- Provider failures raise errors; missing providers no longer masquerade as successful generation.
- Parallel branches execute concurrently; do not depend on sequential sibling side effects.
- Wrap flat flow.database configs in aliases, for example
  `database={"primary":{"driver":"sqlite","database":":memory:"}}`, then open explicitly with
  `sql->connect(App.database["primary"])`. Multiple aliases are supported.
- Python inputs must be declared with `python [name=value] { ... }`. Use `return` inside Python,
  then read typed `__result__` in Axon. Assigning Python __result__ is not a return mechanism.
- Shell block __result__ is a dictionary: read stdout, stderr and exit_code explicitly.

See [INTEROP.md](INTEROP.md), [PYTHON_INTEROP.md](PYTHON_INTEROP.md),
[PARALLEL.md](PARALLEL.md) and [LANGUAGE.md](LANGUAGE.md) for the complete contracts.

## Runtime requirements and downloads

Native Axon and SQLite do not require Python, a database server or the sqlite3 command-line tool.
Python blocks require an external Python 3.9+ interpreter; select a virtual environment with
AXON_PYTHON and install your libraries there. Ollama/models and Tesseract remain optional external
integrations. These tools, model weights and Python packages are not bundled.

Download only assets attached to the official
[GitHub release](https://github.com/axon-software-foundation/axon-core/releases).
Verify each archive against its matching .sha256 file before extraction. An Actions artifact
container is not the portable archive; extract it first. Consult PORTABLE.md for acceptance.

## Platform boundaries

- Windows x64: portable MinGW/UCRT64 package; no installer, signing or automatic updater.
- Linux x86_64: native archive requiring system libraries; no cross-distribution ABI guarantee.
- macOS: experimental native CI; no downloadable macOS package or signing/universal-binary claim.
- Docker: Linux CI/runtime image workflow; no registry publication is implied.

## Known limitations

- DuckDB, PostgreSQL, MongoDB adapters, MCP, gRPC, scraping and additional model providers remain roadmap work.
- Configurable direct-program arguments, environment variables and stdin for shell are not implemented.
- No embedded CPython, persistent Python sessions or C++ interop backend. Python results accept only
  the documented compatible types; DataFrames/models/connections require explicit conversion/export.
- Child-process output is buffered; process isolation is not a security sandbox or a RAM quota.
- Document/image/OCR/semantic-memory coverage, graph policy and multi-flow ownership need further work.
- Key-value memory is string-based; storage operations beyond the documented SQL query alias and
  Whisper integrations are incomplete.

Only platforms/assets actually attached and accepted belong to the published release. CI success
alone does not establish a release, signing, universal compatibility or bit-for-bit reproducibility.

# Capability inventory — 0.4.0-alpha.1

This inventory replaces the old phase-based registry as the current capability summary.
**Implemented** means code exists. **Validated** refers to the evidence named below, not exhaustive
correctness. **Published** requires actual downloadable artifacts. Historical phase numbering in
[RFC-versioning.md](RFC-versioning.md) is not a release contract.

| Capability | Current status | Evidence / limits |
| --- | --- | --- |
| Lexer/parser and CLI check/run | Implemented, selected positive/negative tests | `tests/test_cli.cmake`; `check` does not recursively resolve imports |
| Functions, recursion, closures, positional calls | Implemented and tested, including strict user-function arity | `test_function_*` in `tests/test_interpreter.cpp`; keywords/variadics/default parameters pending |
| Basic control flow and values | Implemented and tested | Interpreter tests; functions currently report `typeof == "dict"` |
| Relative imports/private symbols | Implemented, CLI regressions | Canonical duplicate/circular loads are skipped, not a full cycle resolver |
| Three agent invocation forms | Implemented and tested | `Agent(value)`, `Agent->Task(value)`, `value >> Agent` |
| Task inputs/cached results | Implemented and tested | `test_task_*`; recording adapter only in tests, plus manual live Ollama check |
| Basic task/agent graphs | Selected chains validated | `test_task_graph_input_contract`, `test_graph_execution_order`; complete policies not certified |
| Post-delays | Implemented, measured tests | Task/agent/flow success paths; delays are not deadlines |
| Ollama generation | Adapter implemented; manual integration evidence | Live generation is manual; transport fixtures run in CI; missing services raise ProviderError |
| JSON/CSV/files/regex/dates | Implemented; selected Markdown execution checks | See `docs/MODULES.md`; malformed-input and calendar edge cases need broader coverage |
| SHA-256, MD5, codecs | Implemented; known vectors/roundtrips | SHA-1/SHA-512 explicitly unsupported |
| Key-value memory | Implemented; selected checks | String values, explicit persistence; no automatic typed object storage |
| Semantic memory/HNSW | Implemented, external-service integration needs expansion | Ollama embeddings, HNSW L2; not automatic RAG |
| PDF/DOCX/XLSX/images/OCR | Implemented readers/integrations, experimental coverage | Needs fixture matrix; Tesseract external; native integrations serialized across sibling interpreters; OCR temp output is unique |
| Shell/Python interop | External processes; platform acceptance pending | Explicit Python inputs/typed returns, context-local result, deadlines; host permissions, not a sandbox |
| C++ graph engine, actor FSM, scheduler | Runtime tests | Does not prove language-level concurrency or distributed orchestration |
| Windows x64 portable | Candidate packaging validated; second-machine manual check | New tag artifacts require their own acceptance |
| Linux native/Docker | CI validated on configured runner/image | No general distro/architecture compatibility guarantee or published image implied |
| macOS | Experimental native CI job passed | Non-blocking workflow job; no macOS archive, signing or universal-binary support |
| Flow database registry | Contextual schema validation and focused tests | Explicit alias configurations, no implicit connections; database remains an ordinary identifier elsewhere; docs/INTEROP.md |
| Native SQL / SQLite | Implemented in this working tree, native acceptance pending | Explicit connections, bound parameters, transactions and typed rows; docs/INTEROP.md. DuckDB then PostgreSQL remain pending |
| Whisper | Stub | Empty placeholder response; not transcription support |
| Public HTTP/HTTPS | Working-tree GET/POST/JSON implementation | Verified TLS, local HTTP/TLS fixtures, limits, deadlines and parallel request overlap; `docs/HTTP_STATUS.md` |
| MCP/gRPC/WebSockets/mesh | Pending public implementation/integration | Parser fields/interfaces are not operational transports |
| Timeouts and host cancellation | Cooperative implementation and focused regressions | See `docs/EXECUTION_CONTROL.md`; native/interop blocking calls are not preempted |
| Language parallelism | Native scheduler integration with focused concurrency tests | Snapshots, source-order join, linked cancellation; `docs/PARALLEL.md` |
| Additional providers, training, automatic RAG | Pending | See roadmap; no guarantees inferred from declarations |

Current references: [language](docs/LANGUAGE.md), [modules](docs/MODULES.md),
[validation](docs/VALIDATION.md), [roadmap](docs/ROADMAP.md).

- External Python: explicit `[name=value]` inputs, ordinary `return`, typed `__result__`, independent
  subprocesses, controlled cancellation and shell result envelopes. See docs/PYTHON_INTEROP.md;
  new-patch platform acceptance is pending. No embedded CPython or automatic package installation.

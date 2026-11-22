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
| Ollama generation | Adapter implemented; manual integration evidence | Not run by baseline CI; missing-service passthrough is not generation |
| JSON/CSV/files/regex/dates | Implemented; selected Markdown execution checks | See `docs/MODULES.md`; malformed-input and calendar edge cases need broader coverage |
| SHA-256, MD5, codecs | Implemented; known vectors/roundtrips | SHA-1/SHA-512 explicitly unsupported |
| Key-value memory | Implemented; selected checks | String values, explicit persistence; no automatic typed object storage |
| Semantic memory/HNSW | Implemented, external-service integration needs expansion | Ollama embeddings, HNSW L2; not automatic RAG |
| PDF/DOCX/XLSX/images/OCR | Implemented readers/integrations, experimental coverage | Needs fixture matrix; Tesseract external; fixed OCR temp names prevent safe concurrent calls |
| Shell/Python interop | Implemented, platform/external dependencies | Host permissions; no isolation or complete CI interop coverage |
| C++ graph engine, actor FSM, scheduler | Runtime tests | Does not prove language-level concurrency or distributed orchestration |
| Windows x64 portable | Candidate packaging validated; second-machine manual check | New tag artifacts require their own acceptance |
| Linux native/Docker | CI validated on configured runner/image | No general distro/architecture compatibility guarantee or published image implied |
| macOS | Experimental native CI job passed | Non-blocking workflow job; no macOS archive, signing or universal-binary support |
| Storage/Whisper | Stubs | Empty placeholder responses; not database/transcription support |
| HTTP/REST | Not implemented | Explicit unavailable diagnostic; test checks absence, not requests |
| MCP/gRPC/WebSockets/mesh | Pending public implementation/integration | Parser fields/interfaces are not operational transports |
| Timeouts/cancellation/parallelism | Pending | `parallel` remains sequential |
| Additional providers, training, automatic RAG | Pending | See roadmap; no guarantees inferred from declarations |

Current references: [language](docs/LANGUAGE.md), [modules](docs/MODULES.md),
[validation](docs/VALIDATION.md), [roadmap](docs/ROADMAP.md).

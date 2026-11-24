# Example inventory — 0.4.0-alpha.1

Use `validation/` and `flows.axon` for release acceptance. Historical examples are retained for
migration and are not all current executable language documentation. Canonical syntax is in
[LANGUAGE.md](../axon/docs/LANGUAGE.md); native APIs and integration limits are in [MODULES.md](../docs/MODULES.md).

A syntax-only sweep of all 31 existing `.axon` files at baseline `7a1537f` passed 30 and failed
`flows_v1.axon`. A passing parse does not prove runtime behavior, external integrations or graph policies.

| Files | Status and prerequisites |
| --- | --- |
| `validation/functions.axon`, `main.axon`, `math.axon`, `nested/format.axon` | Service-free validation entry points/import helpers; functions and canonical imports |
| `validation/tasks.axon`, `delays.axon`, `flows.axon` (in the parent directory) | Service-free runtime contracts and timing checks |
| `validation/ollama_tasks.axon` | Live Ollama and llama3.2 required; generation confirmed manually on Windows, not in baseline CI |
| `flows_v1.axon` | Historical non-executable sketch: contains `...` and old declarations; fails current parser. Use `flows.axon` instead |
| `axon_complete_v1.axon`, `axon_complete_v2.axon`, `showcase.axon`, `task_graph_demo.axon`, `research_flow.axon`, `real.axon` | Historical demonstrations; parse, but include policies/transports or external integrations that are not fully implemented/validated. Not release acceptance programs |
| `compute_use.axon`, `timeouts.axon`, `modules/*.axon` | Developer/provider experiments; require review and a live model where tasks call Ollama. Timeout declarations are not enforced |
| `docs.axon`, `io.axon`, `pdf.axon`, `pdf_images.axon`, `ocr_basic.axon` | Local document/image fixtures and writable output locations required; OCR also needs Tesseract/language data. Private fixtures are not bundled |
| `embed.axon` | Running Ollama embedding model and local persistence data/path as configured; integration not covered by baseline CI |
| `functions.axon` | Legacy function demonstration also executes Python through the shell and writes files; use `validation/functions.axon` for a dependency-free check |
| `memory.axon`, `dates.axon`, `text.axon` | Parse, but printed demonstrations are not an assertion-based acceptance suite; review paths, TTL/time-dependent expectations and string escapes before relying on output |

`prompt` and `prompts` are accepted aliases. A plural prompt field alone does not make an example
unsupported. Conversely, accepted `io`, learning/validation or transport declarations do not establish
a working implementation. Do not execute untrusted examples with sensitive files or credentials available.

The executable Markdown examples are checked directly by `docs.examples` in CTest. New reference
examples should include expected-value assertions and clearly declare any external dependencies.

Parallel validation: `validation/parallel.axon` runs without providers;
`validation/ollama_parallel.axon` requires real Ollama generation. See
[parallel execution](../axon/docs/PARALLEL.md) for snapshot/join semantics.

`validation/http.axon` is manual HTTPS acceptance against the public repository API; it requires
Internet access. Automated HTTP/TLS fixtures use loopback and are included in CTest.

- `validation/sqlite.axon`: native SQLite connection configuration, bound parameters, transactions,
  named lookup, functions, parallel reads and raw SQL. Requires no external database/Python/LLM.

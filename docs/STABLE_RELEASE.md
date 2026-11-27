# From alpha.2 to a stable release

The next publication is 0.4.0-alpha.2. Implementation baseline 6f34ba7 passed all six CI jobs,
including Windows portable checks. Version-preparation and exact-tag assets require separate
acceptance; see RELEASING.md and GITHUB_LAUNCH.md. Preparing a version does not publish it.

## Delivered implementation baseline

- Functions/closures, relative imports, strict arity, task chaining and diagnostics.
- Cooperative execution budgets, host cancellation, provider errors and post-delays.
- Native parallel branches/groups with isolated state, ordered publication and sibling cancellation.
- Public HTTP/HTTPS with verified TLS, structured responses and resource controls.
- Native SQLite connections, parameter binding, transactions, typed rows and multiple configuration
  aliases per flow. Declarations validate configuration without opening resources.
- External Python with explicit inputs, typed returns and independent processes; shell shares
  controlled execution and exposes stdout/stderr/exit status. See PYTHON_INTEROP.md.

These statements describe implementation and baseline CI, not a guarantee of every API/module,
platform or external integration. Preserve regression coverage as the language evolves.

## Freeze the scope before beta

Agree a finite supported surface, its syntax/APIs, documented dependencies, failure behavior and
platform matrix. Resolve release-blocking defects in that surface and migrate every example
advertised as runnable. Clearly classify unfinished integrations as experimental or later work.
A first stable release does not require completing the entire roadmap.

Move to 0.4.0-beta.1 when the agreed scope/contracts are fixed and remaining work is validation,
defect correction and documentation. Avoid incompatible additions during beta; if necessary,
record the migration and reassess the candidate. Use an rc candidate when final acceptance is ready.

Publish 0.4.0 only after exact candidate artifacts pass the agreed platform and clean-machine checks,
confirmed blockers are resolved, documentation matches behavior and compatibility/support policy is
written. Removing a prerelease suffix alone is not evidence of stability.

## Acceptance still required

- Final tag CI, exact portable hashes/manifests/source SHA and clean extraction/execution.
- Windows and Linux support boundaries; macOS packages/signing remain separate work.
- Live Ollama generation/task chaining on the final candidate, separate from fixture tests.
- Review document/OCR/image/semantic-memory behavior, graph policies and consistent module errors
  to decide what is supported versus experimental in the stable scope.
- Private reporting/contact channel, licenses/notices/provenance and actionable community issues.

## Subsequent features

Complete shell direct-program arguments, environment and stdin under an agreed public syntax.
Data-driver order remains DuckDB then PostgreSQL after SQLite; MongoDB requires its own adapter.
MCP, gRPC, scraping, additional providers, richer multi-flow ownership, RAG/context management,
training and mesh remain explicit roadmap items. Native computation and careful context transfer
should reduce unnecessary model-token usage throughout these designs.

The website is maintained separately. Release/download links should point to real artifacts;
GitHub remains the entry point for community issues/discussions. This Core patch does not deploy web changes.

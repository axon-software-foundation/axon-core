# Axon Core and Web roadmap

Current development version: **0.4.0-alpha.1**. Reviewed against public-main baseline `7a1537f`
on 22 November 2026. Implementation, validation and publication are separate states.
The [capability inventory](../FEATURES.md) records evidence; historical RFC phases do not establish status.

The first prerelease is now published. Further Core work targets a usable release without a
prerelease suffix; see [STABLE_RELEASE.md](STABLE_RELEASE.md) for the current execution order.
The historical first-alpha preparation list below is retained for context, not a request to repeat
completed publication or the maintainer's manual validation.

## 1. Stabilize the execution contract

Implemented with focused regressions: functions/closures, relative imports, CLI diagnostics,
three agent invocation forms, task input/output references, repeated-invocation isolation,
basic task graphs and post-delays. Scheduler shutdown and strict-build portability fixes are included.
Keep these contracts covered as the language evolves. Cooperative flow/agent/task deadlines, host
cancellation and provider-error propagation are implemented; see [EXECUTION_CONTROL.md](EXECUTION_CONTROL.md)
for scope and external blocking-call limits. Language-level parallel blocks and pipeline groups now
use native workers with isolated environments and synchronized services; see [PARALLEL.md](PARALLEL.md).
Pending: complete graph policy semantics and consistent native-module arity/type errors.

## 2. Publish the first alpha

Completed baseline: official organization repository and merged import, single VERSION source,
strict CI jobs for Linux/Windows/experimental macOS/Docker, Windows portable packaging and
second-machine manual execution. The current documentation change reconciles English reference
content with code, checks Markdown examples and removes the duplicate root license.

Remaining before release: review this change and rerun CI, verify private reporting/contact channels,
finish vendored provenance review, choose the tag commit, build/tag-test exact artifacts, repeat clean-machine
acceptance, publish a GitHub prerelease with correct checksums and link it from the web.
See [GITHUB_LAUNCH.md](GITHUB_LAUNCH.md). No release/image is considered published just because CI passed.

## 3. Platforms, packaging and beta

Linux native archives and Docker execution passed configured CI; establish a minimum system-library/ABI
baseline and distribution/architecture test matrix before promising portability.
Experimental native macOS CI passed, but the job remains non-blocking and packaging is not implemented.
Choose and test architectures before claiming Intel/Apple Silicon/universal support.
Add signing/notarization where applicable and document compatibility. Beta requires the committed scope
and declared platforms to be complete; RC/stable require resolved blockers and a compatibility policy.

## 4. Communications and integrations

- HTTP/REST: GET/POST/JSON with verified HTTPS, status/headers/body, errors, deadlines, bounded
  same-origin redirects and limits is implemented in the working tree, with real local HTTP/TLS
  fixtures. Cross-platform/package acceptance is required before publication. See [HTTP_STATUS.md](HTTP_STATUS.md).
- gRPC, WebSockets and sockets: reconcile parser/interface plans with working transports and tests.
- MCP: define client/server role, protocol version, capabilities, transport, authorization,
  connection lifecycle, cancellation and errors. MCP is not an alias for REST or gRPC.

The public HTTP module remains independent of the specialized Ollama HTTP clients. gRPC, WebSockets
and MCP remain future implementation blocks. HTML parsing/scraping follows HTTP and is not implemented here.

## 5. Language and multi-flow

Strict arity is implemented for user-defined functions, with catchable errors and regression coverage.
Specify remaining return semantics, function representation and diagnostics. Design variadics, named arguments,
unpacking, selective imports and optional annotations before adding syntax.
Complete agent ownership by flow, context isolation and explicit flow invocation/chaining.
Clarify module collisions and exports. Migrate remaining legacy examples; verify each against current code
and declare external prerequisites instead of retaining unsupported snippets as reference.

## 6. Providers and multimodality

Expand real Ollama integration tests separately from the service-free suite. Define additional providers
(OpenAI, Anthropic, Cohere; reconcile Google scope), authentication/TLS, limits, retries, streaming and
image inputs. Provider names in old examples are not implemented adapters.

## 7. Data, documents and interoperability

Expand memory/TTL/persistence, embeddings/HNSW, PDF, DOCX/XLSX, images and OCR integration fixtures.
Document native PDF architecture rather than claiming MuPDF. Remove fixed OCR temporary-file collisions
before concurrent use. Consolidate Python/shell typed exchange, result/error reporting, subprocess lifecycle and cancellation.
Native SQLite connections have Linux/Windows native/package acceptance. Contextual flow.database alias
registries and pure sql->validate are implemented in the working tree and require their own acceptance.
The agreed next driver order is DuckDB, then PostgreSQL, preserving explicit connection configuration
outside agent/task declarations (flow dictionaries, main and functions). MySQL/MongoDB/Arrow remain
separate proposals. See INTEROP.md for the current connection and transaction contract.
Specify remaining regex/error semantics and unsupported hashes without changing existing contracts silently.

## 8. RAG and learning

Design native ingestion, retrieval, context construction and evaluation. Specify `finetune`, datasets,
LoRA backend, checkpoints and export before implementation. Cognitive/Federated Imprinting need verifiable
specifications; historical learning declarations do not prove training or automatic RAG.

## 9. Distributed mesh

Discovery, topology, inter-machine transport, encryption, gossip and consensus remain future work.
Ultrasonic transport is a separate research track, not an available alpha transport.

## 10. Observability and ecosystem

Consistent metrics/traces, OpenTelemetry and cost estimates; editor grammar/VS Code/diagnostics;
Cython binding validation; registry and package/init/new/publish design. C++ interfaces alone do not
establish working tooling or runtime integration.

## 11. Axon Web

Align advertised capabilities and versions with the current reference and actual release assets.
Connect downloads/checksums, repository and community links. Review source/assets licensing.
The hosted playground is not operational: isolate execution and add limits before enabling public code.
Web code requires its own review; this Core change does not update hosting or deploy the website.

New feature proposals may refine these blocks. Mentioning a feature does not mark it implemented
or commit it to the next alpha.

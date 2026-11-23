# Next usable Core release

Work continues from public main `b2421999909c10972531396b346b7a2bb382382a`.
The maintainer has already tested the existing functionality, including Windows portable execution
on a second machine and real Ollama task chaining. Keep that acceptance as the baseline.
Automated regressions protect changed behavior; they do not replace those real integration checks.

The target is a release without an alpha/beta suffix. Keep the published tag and `VERSION`
unchanged during this implementation block. Select and validate the new release identifier when
the release scope is ready; changing the suffix alone does not complete that work.

## Execution order

1. **Function call contract — implemented in this change.** Exact positional arity for user-defined
   functions, catchable errors, expected/actual counts, call line, and rejection before argument/body
   side effects. Preserve valid calls and existing agent/task invocation contracts.
2. **Execution failures and limits — implemented cooperatively.** Flow/agent/task timeouts,
   interruptible delays, host cancellation and Ollama socket budgets are covered by regressions.
   Provider failures now propagate instead of returning `null` or input passthrough. See
   [EXECUTION_CONTROL.md](EXECUTION_CONTROL.md) for limits, including non-preemptible native/interop
   calls. Real Ollama failure/recovery acceptance remains a manual check on the candidate.
3. **Concurrent execution — implemented, acceptance pending.** Parallel blocks and pipeline groups
   use native scheduler jobs, lexical/closure snapshots, source-order publication and sibling
   cancellation. Existing native services and legacy adapters are synchronized; OCR/Python temporary
   directories are unique. See [PARALLEL.md](PARALLEL.md). Real Ollama and platform CI acceptance
   remain required before publication.
4. **Public HTTP.** Specify a small public GET/POST/JSON contract; implement verified HTTPS,
   status/headers/body, explicit errors, deadlines and resource limits. Test real requests against
   a local fixture server, plus controlled TLS verification. The specialized Ollama client does not
   already provide this public module. No new dependency is selected by this plan.
5. **Remaining release blockers.** Address confirmed failures in the selected modules, document
   explicit support boundaries for document formats/memory/interop, and migrate examples advertised
   as runnable. Add fixtures where they expose a concrete unsupported edge or protect a fix.
6. **Release acceptance.** Run strict CI for the declared platforms, build and extract the exact tagged
   packages, run their self-tests and examples, and verify checksums and dependency/notice payloads.
   Confirm live Ollama generation and chained task input on the final candidate. Publish matching
   notes, supported-platform details and a compatibility policy, then update download links.

Steps 2–5 are implementation priorities, not a claim that each must ship in the same release.
A pending feature may be scheduled later if the release clearly excludes it and does not silently
pretend to provide it. Agree the final supported scope before applying a stable version label.

## Later integrations and ecosystem

MCP, gRPC, WebSockets, additional providers, richer multi-flow ownership, RAG/training and distributed
mesh remain explicit roadmap work. MCP requires its own role, capabilities, transport and lifecycle
contract; it is not an alternate name for REST or gRPC. The broader inventory remains in
[ROADMAP.md](../axon/docs/ROADMAP.md) and [FEATURES.md](../axon/FEATURES.md).

The website is maintained separately. Its release/download/reference pages should reflect actual
Core releases. Issues and discussions should be created in the official GitHub repository;
displaying that activity on the website is a later web integration, outside this Core patch.

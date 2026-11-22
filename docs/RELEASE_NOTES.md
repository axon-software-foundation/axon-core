# Axon 0.4.0-alpha.1 — prerelease candidate

Draft notes for a future GitHub prerelease; this file does not establish publication.
Alpha syntax/APIs may change and there is no stable compatibility guarantee yet.

## Included

Functions/closures and relative imports; controlled diagnostics and exit codes; whole-agent,
individual-task and pipeline calls; task input/output references and invocation isolation;
post-delays; scheduler shutdown fix; strict cross-platform warning fixes; Windows portable
packaging with checksums and extracted execution checks.

The baseline CI passed Linux/Windows Debug and Release, experimental native macOS and Linux Docker.
Selected Markdown examples are now checked directly by CTest. See [VALIDATION.md](VALIDATION.md)
for what is executed versus syntax-only. Live Ollama task chaining was separately checked manually
on Windows. A previous Windows portable also ran on a second machine; the final tag artifact still
requires its own acceptance and checksum. Never copy a previous candidate's hash into new release assets.

## Known limitations

- Windows x64 uses MinGW GCC portable packaging; no installer, code signing or automatic updater.
- Linux native archives have no established cross-distribution ABI baseline.
- macOS CI is experimental/non-blocking; no macOS package, signing or architecture-wide support claim.
- HTTP/REST and MCP are not public implemented modules. Storage/Whisper calls are stubs.
- Timeouts/cancellation and actual parallel language execution are pending.
- Additional providers, complete multi-flow ownership, automatic RAG, training and mesh remain roadmap work.
- Document/image/OCR/semantic-memory integrations need expanded coverage; OCR temporary names collide
  under concurrent execution. Key-value memory stores strings, not arbitrary typed objects.
- Ollama, model weights and Tesseract are not included. Missing-provider passthrough is not LLM generation.

Before publishing, confirm the tag commit, trusted CI run, exact portable acceptance, notices/provenance,
working private reporting channel and all asset checksums. In the final release description, list only
assets actually attached and platforms actually tested. The website must link to those real downloads.

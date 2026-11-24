# Axon Language

A C++17 language runtime for defining agents, chaining tasks and orchestrating local LLM calls.

**Development version: 0.4.0-alpha.1.** APIs and syntax may change during alpha.
[VERSION](VERSION) is the authoritative release identifier. Passing CI does not publish a release.
See [Releases](https://github.com/axon-software-foundation/axon-core/releases) for actual downloads.

## Start without an LLM

Save this as `hello.axon`. It runs without Ollama or other external services.

<!-- axon-check: run -->
```axon
func twice(n) { return n * 2 }
main {
    result = twice(21)
    if result != 42 { raise "unexpected result" }
    print("Hello from Axon: ", result)
}
```

After building, run `./build-validation/axon run hello.axon` on Linux/macOS,
or `./build-validation/axon.exe run hello.axon` in Windows Git Bash.

## Build and validate

Requirements: CMake 3.20+ (including CTest), a C++17 compiler, Bash and Make/Ninja.
Ollama and Tesseract are optional and are not required for the automated baseline.

Windows, from Git Bash with MSYS2 UCRT64 GCC and Ninja installed:

```bash
bash scripts/validate.sh Debug -G Ninja \
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe \
  -DAXON_WARNINGS_AS_ERRORS=ON
```

Linux or experimental macOS, from the source root:

```bash
bash scripts/validate.sh Release -DAXON_WARNINGS_AS_ERRORS=ON
```

Use a new build directory when changing compilers or generators; set `AXON_BUILD_DIR` to override
`build-validation`. See [validation](docs/VALIDATION.md), [CI](docs/CI.md) and
[Docker](DOCKER_BUILD.md). For portable archives, use [releasing](docs/RELEASING.md).

HTTP build dependencies: Linux `libcurl4-openssl-dev libssl-dev`; macOS system libcurl and
OpenSSL from Homebrew; Windows MSYS2 `mingw-w64-ucrt-x86_64-openssl` for TLS fixture tests.
Windows downloads/builds the pinned minimal libcurl on first configure. See [HTTP](docs/HTTP_STATUS.md).

## Agent and task example

This example needs a running Ollama service and the `llama3.2` model (`ollama pull llama3.2`).
CI checks its syntax; it does not call the model. Missing providers and failed model requests
raise `ProviderError`; they do not substitute the input for generated output.

<!-- axon-check: check -->
```axon
flow Research { input = "Choose one fruit: banana, apple or mandarin." }
agent Processor {
    provider ollama
    model llama3.2
    task Analyze {
        input = __input__
        prompt = ["Choose exactly one fruit and name it."]
    }
    task Summarize {
        input = Analyze.output
        prompt = ["Give three characteristics of the chosen fruit."]
    }
}
main {
    result = Processor(flow.input)
    print("Analyze: ", Processor.Analyze)
    print("Summarize: ", result)
}
```

Agent invocation forms are `Processor(value)`, `Processor->Analyze(value)` and
`value >> Processor`. The [language reference](docs/LANGUAGE.md) explains their input,
output and timing contracts. The [module reference](docs/MODULES.md) documents implemented
APIs and their testing limits.

## Current scope

- Functions, closures, positional calls, control flow, lists/dictionaries and relative file imports.
- Full-agent calls, individual tasks, pipelines, cached task outputs and post-execution delays.
- Concurrent parallel blocks and pipeline groups with snapshot isolation; see [parallel execution](docs/PARALLEL.md).
- Ollama integration; native modules for files, JSON/CSV, hashes/codecs, dates, images and memory.
- Document readers and OCR integrations with narrower validation; see the module reference.

Execution budgets and host cancellation are documented in [execution control](docs/EXECUTION_CONTROL.md).

Public GET/POST/JSON and verified HTTPS are documented in [HTTP](docs/HTTP_STATUS.md).

**Pending:** MCP/gRPC/WebSockets, HTML scraping, additional LLM providers, complete multi-flow ownership
and distributed mesh.
A parser declaration or C++ interface is not evidence of a working language feature.

The CI baseline at public commit `7a1537f` passed Linux GCC Debug/Release, Windows UCRT64
Debug/Release, experimental macOS Apple Clang and the Linux Docker job. Windows portable
execution was also manually confirmed on a second Windows computer. This is not universal
platform support: macOS has no packaged download, and Linux archives have no established
cross-distribution ABI baseline. See [capability evidence](FEATURES.md) and [roadmap](docs/ROADMAP.md).

## Documentation and community

- [Language](docs/LANGUAGE.md) and [modules](docs/MODULES.md)
- [Validation and evidence](docs/VALIDATION.md)
- [Contributing](CONTRIBUTING.md), [code of conduct](CODE_OF_CONDUCT.md) and [security](SECURITY.md)
- [Changelog](CHANGELOG.md) and [release checklist](docs/GITHUB_LAUNCH.md)
- [Issues](https://github.com/axon-software-foundation/axon-core/issues) for reproducible bugs and proposals

English is the canonical documentation language. Reports in English or Spanish are welcome.
See the [example inventory](../examples/README.md) before running legacy samples.
Legacy examples outside `examples/validation/` may require external files, Python or services;
they are not all release acceptance examples. Historical RFCs are design records, not current API guarantees.

## License

Axon code is MIT licensed: see [LICENSE](LICENSE). Dependencies retain their licenses in
[licenses](licenses) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Native SQLite connections are available in the current source tree; see [interop and SQL](docs/INTEROP.md).
Configuration can live in flow dictionaries, main or functions. The first build fetches pinned SQLite
sources; the resulting runtime needs no external SQLite DLL, CLI, Python or server for this module.

Within a flow, `database` declares configurations by alias; opening is explicit. See
[the connection contract and migration](docs/INTEROP.md#contextual-flowdatabase-registry) and
`examples/validation/flow_database.axon` for executable acceptance.

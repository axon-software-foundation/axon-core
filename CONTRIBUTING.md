# Contributing to Axon

English is the canonical documentation language; reports in English or Spanish are welcome.
Axon Core is in alpha. Read the [roadmap](docs/ROADMAP.md) and open a proposal before investing
in new syntax, dependencies or architectural changes.

## Build and validation

Use CMake 3.20+, a C++17 compiler, Bash and Make/Ninja. Windows uses MSYS2 UCRT64 GCC with Ninja.
Ollama, Python and Tesseract are not required for the automated baseline.
From the source root in Windows Git Bash (adjust the compiler paths if needed):

```bash
bash scripts/validate.sh Debug -G Ninja \
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe \
  -DAXON_WARNINGS_AS_ERRORS=ON
```

On Linux or experimental macOS:

```bash
bash scripts/validate.sh Debug -DAXON_WARNINGS_AS_ERRORS=ON
```

Repeat with Release for execution, synchronization or packaging changes. See
[VALIDATION.md](docs/VALIDATION.md) for evidence and test boundaries. Model integration is a
separate manual check: [ollama_tasks.axon](examples/validation/ollama_tasks.axon).

## Pull requests

1. Use a branch focused on one problem; fork the repository if you do not have write access.
2. Explain the trigger and resulting behavior, with a minimal example where useful.
3. Add relevant regression coverage for runtime bugs and new language behavior. Do not change
   expectations to hide a failure. Test doubles belong in tests, not production implementations.
4. Record commands, results, OS and compiler. Identify services/platforms you could not test.
5. Update public documentation and the changelog when behavior changes.

Preserve full-agent, specific-task and pipeline invocation contracts. Changes to inputs, outputs,
imports and errors need coverage of existing behavior. Passing a mocked-provider test does not
validate live LLM generation.

## Documentation

The README is an introduction; [LANGUAGE.md](docs/LANGUAGE.md) and [MODULES.md](docs/MODULES.md)
are the current reference. Keep claims aligned with code and executable evidence. Clearly label
implemented, experimental, stub and pending behavior. Parser acceptance alone is not implementation.
Every `axon` fence in these three documents must be a complete program preceded by
`<!-- axon-check: run -->` or `<!-- axon-check: check -->`. CTest executes the actual Markdown;
`check` is syntax-only and must state its external requirements. Use assertions for expected results.

## Quality, dependencies and licensing

Keep `AXON_WARNINGS_AS_ERRORS=ON`; fix enabled warnings in Axon code. Do not globally suppress
warnings to hide problems. HNSWlib/stb are external headers; see [BUILD_WARNINGS.md](docs/BUILD_WARNINGS.md).
Explain new dependencies and preserve their licenses, notices and recorded provenance.
Do not commit builds, archives, model weights, secrets, personal data or private test documents.
Use small synthetic fixtures and review the diff.

A maintainer reviews contributions before merging. CI success is necessary but does not prove
all advertised features or platforms. Axon's code is MIT licensed; dependencies retain their own
terms. Disclose any licensing restrictions in your contribution. Follow [SECURITY.md](SECURITY.md)
and [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

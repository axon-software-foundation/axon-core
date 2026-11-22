# Linux builds with Docker

Requires Docker using Linux containers; Compose commands require Compose v2. Windows may use
Docker Desktop's Linux/WSL2 backend. This path does not build Windows or macOS binaries.
No registry image is published by the workflow.

## Build and test mounted source

From the repository root:

```bash
docker compose run --build --rm build
docker compose run --rm test
```

Builds use the `axon-linux-build` Docker volume rather than Windows build-alpha. Do not mix CMake
caches across platforms or run build/test simultaneously on the same volume. Compiler/test failures
return nonzero. Open a development shell with `docker compose run --build --rm shell`.

## Build the tested runtime image

```bash
docker build --target runtime -t axon:dev .
docker run --rm axon:dev version
docker run --rm axon:dev test
docker run --rm axon:dev run /opt/axon/share/axon/examples/validation/tasks.axon
```

The build stage runs version consistency, CTest and native archive/extracted-package checks.
Runtime includes the CLI, both suites, system libraries, examples, documents and notices. It runs as
a non-root user without compiler/source, Ollama, models or Tesseract. Provider examples need an accessible
service supported by the current adapter; container localhost is not host localhost.

Mount only the necessary directory, preferably read-only. PowerShell:

```powershell
docker run --rm --mount "type=bind,source=$($PWD.Path),target=/work,readonly" axon:dev run /work/examples/validation/tasks.axon
```

Git Bash, avoiding automatic conversion of container paths:

```bash
MSYS_NO_PATHCONV=1 docker run --rm --mount "type=bind,source=$(pwd -W),target=/work,readonly" axon:dev run /work/examples/validation/tasks.axon
```

Linux:

```bash
docker run --rm --mount "type=bind,source=$PWD,target=/work,readonly" axon:dev run /work/examples/validation/tasks.axon
```

Read-only mounts do not make hostile scripts safe. See [SECURITY.md](SECURITY.md).

## Export the native archive

With BuildKit/buildx local output:

```bash
docker build --target artifacts --output type=local,dest=dist-docker .
```

The `.tar.gz` and `.sha256` target the build's Linux architecture/system libraries. They do not
establish cross-distribution compatibility or a multi-architecture image.

The Linux Docker CI job passed at baseline `7a1537f`, including runtime version/suites/task execution.
That job does not exercise all Compose bind-mount examples; those remain environment-specific commands.
The Ubuntu 24.04 base uses a mutable tag rather than a pinned digest; bit-for-bit reproducibility is not claimed.

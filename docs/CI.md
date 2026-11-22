# Axon Core CI

[.github/workflows/ci.yml](../.github/workflows/ci.yml) runs on push, pull request and manual dispatch.
All six jobs passed on baseline public-main commit `7a1537f`; new changes require a new run.

| Job | Checks | Distribution boundary |
| --- | --- | --- |
| Linux GCC Debug/Release | CTest, suites and examples; Release also checks extracted archive | Native archive only; no distro-wide ABI guarantee |
| Windows UCRT64 Debug | CTest and examples with GCC | Source validation |
| Windows UCRT64 Release | Static portable, PE imports, hashes and extracted execution with reduced PATH | Windows candidate for review |
| macOS Apple Clang experimental | Native Release, CTest and examples | Non-blocking; no package or universal-binary claim |
| Linux Docker candidate | Multi-stage build/tests, then non-root execution | Local CI image; not published |

All builds use strict warnings. No baseline job requires Ollama, model weights, OCR or service credentials.
Markdown examples are checked through `docs.examples` in CTest; model-dependent snippets are syntax-only.
See [VALIDATION.md](VALIDATION.md) for counts and evidence boundaries.

macOS uses `continue-on-error: true`. A green workflow alone can hide a failing experimental job;
inspect its own conclusion. Do not claim architectures beyond the runner actually used.

## Permissions and integrity

Actions are pinned to full commits; Dependabot proposes updates that still need review and CI.
The workflow token has `contents: read` and checkout does not preserve credentials.
Fork PRs use `pull_request`, not privileged `pull_request_target` execution.
Candidate artifacts are retained for 14 days; CTest logs are uploaded on failure when available.
Compiler/configuration logs remain in the job log even when no CTest file was produced.

On tags, `check-release.sh` requires `v` followed by VERSION and a matching changelog heading.
The workflow does not create releases, push commits, sign binaries or publish images.

## Diagnose and release

Open the first failing step and preserve its complete diagnostic. A Windows BAD_COMMAND may mean
application-control policy prevented process startup; inspect system/CodeIntegrity evidence.
A timeout needs a located blocking operation, not an arbitrary increase in its limit.

Use artifacts from a trusted tag run in the official repository for releases, not an unreviewed PR/fork.
The Actions artifact-container digest differs from the portable `.zip.sha256`; do not substitute one
for the other. Follow [GITHUB_LAUNCH.md](GITHUB_LAUNCH.md) for exact-tag acceptance.
Configure required checks using actual job names. Keep experimental macOS non-required until its
support policy is decided; this document does not change branch protection.

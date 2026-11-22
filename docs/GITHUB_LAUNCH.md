# Publish the first alpha and open the community

Official repository: https://github.com/axon-software-foundation/axon-core.
The initial import is merged. Documentation/license changes must be reviewed in their own PR before tagging.
This checklist does not itself create a release or change repository settings.

## 1. Review and merge

From a clean public checkout, create a focused branch from updated main. Review all changes, including
removed `LICENSE.txt` references, executable documentation examples and packaged notices.
Run strict validation/package checks; push the branch and open a PR. Merge after review and successful
required checks. Inspect experimental macOS separately because it remains non-blocking.
Do not commit builds, model weights, secrets or private fixtures.

## 2. Confirm repository readiness

Review visibility/access and actual required check names. Enable and test the private security reporting
channel and establish moderation contacts. Review third-party provenance and preserve notices.
Do not claim these settings are enabled merely because policy documents exist.

## 3. Tag the exact reviewed code

From clean updated main:

```bash
git switch main
git pull --ff-only origin main
bash scripts/check-release.sh
git status --short
git log -1 --oneline
git tag -l v0.4.0-alpha.1
```

Confirm the official origin, clean state, approved commit and absence of this tag. If it already exists,
do not move it: advance VERSION/changelog to the next alpha and repeat validation.
Only when those checks are complete:

```bash
git tag -a v0.4.0-alpha.1 -m "Axon 0.4.0-alpha.1"
git push origin v0.4.0-alpha.1
```

The tag triggers CI and version/changelog consistency checks; it does not publish a release.

## 4. Accept the exact tag artifact

Open the trusted tag run in Actions and verify its source commit and all required job results.
Download `windows-x64-candidate-<SHA>` and extract the Actions artifact container; inside are the
portable ZIP and its checksum. The container digest is not the portable checksum.

```bash
sha256sum -c axon-v0.4.0-alpha.1-windows-x64.zip.sha256
```

Follow [PORTABLE.md](PORTABLE.md) with that exact ZIP, preferably on a second Windows x64 machine
without a development toolchain. `BUILD_INFO.txt` must identify the tag commit and clean source state.
Record OS/architecture/results and optional live-model results. Linux artifacts remain native validation
archives; no macOS package or registry image should be advertised unless separately built and accepted.

## 5. Prepare and publish a prerelease

Create a GitHub release for the existing tag, mark it as a prerelease and save a draft first.
Attach the accepted ZIP and `.sha256`, adapting [RELEASE_NOTES.md](RELEASE_NOTES.md) to the actual assets.
Do not describe an alpha as a stable Latest release.
Optional GitHub CLI commands (requires an authenticated CLI and downloaded assets in dist):

```bash
gh release create v0.4.0-alpha.1 \
  --repo axon-software-foundation/axon-core \
  --verify-tag --draft --prerelease \
  --title "Axon 0.4.0-alpha.1" \
  --notes-file docs/RELEASE_NOTES.md \
  dist/axon-v0.4.0-alpha.1-windows-x64.zip \
  dist/axon-v0.4.0-alpha.1-windows-x64.zip.sha256
```

Review tag/commit, assets, notices, checksums, clean-machine results, limitations and private contacts,
then publish the reviewed draft. Signing and reproducibility remain separate roadmap work.

## 6. Community and web

Open focused issues with acceptance criteria: Linux ABI/platform matrix, macOS packaging, third-party
provenance, Windows signing, HTTP contract, MCP design, deadlines/cancellation and actual concurrency.
Only label small, well-specified tasks as good first issues; security/transports/concurrency are not
automatically beginner work. Use issues for proposals rather than presenting roadmap items as delivered.

Update axon-pl.org to link the real prerelease/source, version and checksums. Advertise only available
platform assets and keep the playground unavailable until its isolated backend is ready.
Web code/hosting is a separate project and is not deployed by this Core change.

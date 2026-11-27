# Publish Axon 0.4.0-alpha.2

Official repository: https://github.com/axon-software-foundation/axon-core.
Alpha.1 is already published. This guide prepares a separate prerelease; it does not publish one
or change repository settings. Never move the alpha.1 tag or replace its existing assets.

## 1. Merge the version preparation

Use a clean branch release/0.4.0-alpha.2 from updated main, apply the version/docs patch, then run
RELEASING.md's complete build, CTest and packaging commands. Push a PR and wait for required checks
and reviews under the main ruleset. The earlier implementation CI does not validate a new version commit.
Do not commit builds, archives, Python environments, databases, secrets or delivery-aid files.

## 2. Tag the exact reviewed commit

After merging the preparation PR:

```bash
git switch main
git pull --ff-only origin main
git remote -v
git status --short
bash scripts/check-release.sh
git log -1 --oneline
git fetch origin --tags
git tag -l v0.4.0-alpha.2
```

Confirm official origin, no working-tree changes, VERSION 0.4.0-alpha.2, the approved commit and
that v0.4.0-alpha.2 does not already exist. If it exists, inspect it; do not recreate or force-move it.
Only after those checks:

```bash
git tag -a v0.4.0-alpha.2 -m "Axon 0.4.0-alpha.2"
git push origin v0.4.0-alpha.2
```

The push triggers CI, not a GitHub release. Verify the tag run's source SHA and all jobs; inspect
experimental macOS as well, since it remains non-blocking. Do not use an unrelated PR artifact.

## 3. Accept artifacts from the tag run

Download windows-x64-candidate-<TAG_SHA> and linux-validation-<TAG_SHA> from that trusted Actions run.
Extract the Actions container ZIPs first. Their outer digests are not the portable archive hashes.
Put the actual portable archives and their own .sha256 files into dist/:

```bash
cd dist
sha256sum -c axon-v0.4.0-alpha.2-windows-x64.zip.sha256
sha256sum -c axon-v0.4.0-alpha.2-linux-x86_64.tar.gz.sha256
cd ..
```

Run PORTABLE.md against each exact artifact on its target OS. BUILD_INFO.txt must identify the tag
commit with a clean source state. Confirm the CLI reports 0.4.0-alpha.2, run self-tests/examples,
and record OS/architecture/results. Use a second Windows machine where available; external Python
acceptance additionally requires a selected interpreter, while native/SQLite execution does not.
Linux archives require their system libraries. No macOS binary or registry image is promised.
If artifacts are rebuilt, rerun their acceptance/checksums; do not reuse evidence from an older file.

## 4. Draft the prerelease

Use the existing tag, title Axon 0.4.0-alpha.2, and mark the release as a prerelease. Copy the complete
body from docs/RELEASE_NOTES.md, adjusting platform/download details to the assets actually accepted.
The following optional GitHub CLI command assumes both OS archives were downloaded and accepted:

```bash
gh release create v0.4.0-alpha.2 --repo axon-software-foundation/axon-core --verify-tag --draft --prerelease --title "Axon 0.4.0-alpha.2" --notes-file docs/RELEASE_NOTES.md dist/axon-v0.4.0-alpha.2-windows-x64.zip dist/axon-v0.4.0-alpha.2-windows-x64.zip.sha256 dist/axon-v0.4.0-alpha.2-linux-x86_64.tar.gz dist/axon-v0.4.0-alpha.2-linux-x86_64.tar.gz.sha256 THIRD_PARTY_NOTICES.md
```

If only one platform has passed acceptance, attach only its archive/checksum and state that scope.
Do not upload the outer Actions ZIP as the runtime download. Review notices/provenance and the
working private reporting/contact channel; policy documents alone are not evidence of settings.
Publishing the reviewed draft is the final separate action. Keep prerelease status and do not
present it as a stable Latest release. This repository workflow does not sign binaries.

## 5. Community and the path to stable

Point release/download links at actual accepted assets after publication. Website hosting/design is
separate and not modified by this Core patch. Direct issues/discussions to the official repository.
Record feedback with concrete reproduction and acceptance criteria. Agree the first stable release's
supported scope after alpha.2; later database drivers, MCP and scraping need not all ship in it.
See STABLE_RELEASE.md for the beta/stable decision and remaining boundaries.

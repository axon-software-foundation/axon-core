#!/usr/bin/env bash
# Read-only consistency check. Does not create tags, commits or releases.
set -euo pipefail
AXON_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AXON_VERSION="$(tr -d '\r' < "$AXON_ROOT/VERSION")"
if [[ ! "$AXON_VERSION" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-(alpha|beta|rc)\.(0|[1-9][0-9]*))?$ ]]; then
    echo 'Invalid VERSION; expected X.Y.Z[-alpha.N|-beta.N|-rc.N].' >&2
    exit 1
fi
if [[ "${GITHUB_REF_TYPE:-}" == tag && "${GITHUB_REF_NAME:-}" != "v$AXON_VERSION" ]]; then
    echo "Tag ${GITHUB_REF_NAME:-} does not match v$AXON_VERSION." >&2
    exit 1
fi
if ! grep -Fq -- "## [$AXON_VERSION]" "$AXON_ROOT/CHANGELOG.md"; then
    echo "CHANGELOG.md has no entry for $AXON_VERSION." >&2
    exit 1
fi
echo "PASS: VERSION and changelog agree ($AXON_VERSION); tag checked when present."

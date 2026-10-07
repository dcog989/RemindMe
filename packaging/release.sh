#!/usr/bin/env bash
#
# Publish the release cog just created: build the KDE Store payload, push the branch
# and tag and open a GitHub release with it attached. Run by cog's post_bump_hooks;
# can also be run by hand to publish an already-tagged version.
#
# The KDE Store upload itself has no CLI and stays manual (upload the printed archive).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

tag="$(git describe --tags --abbrev=0)"
version="${tag#v}"
archive="dist/remindme-$version.tar.gz"

bash packaging/package.sh

git push origin "$(git rev-parse --abbrev-ref HEAD)"
git push origin "$tag"

if gh release view "$tag" >/dev/null 2>&1; then
    gh release upload "$tag" "$archive" --clobber
else
    gh release create "$tag" "$archive" --generate-notes
fi

echo "==> Released $tag (KDE Store: upload $archive)"

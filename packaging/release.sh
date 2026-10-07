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

# Use this version's section from the changelog cog wrote as the GitHub release body,
# rather than GitHub's auto-generated notes (which are not the curated changelog).
notes="$(awk -v v="$version" '
    $1 == "##" && $2 == "v" v { in_section = 1; next }
    in_section && ($1 == "##" || $0 == "- - -") { exit }
    in_section { print }
' CHANGELOG.md)"
if [ -z "$notes" ]; then
    echo "no changelog section for v$version in CHANGELOG.md" >&2
    exit 1
fi
notes_file="$(mktemp)"
trap 'rm -f "$notes_file"' EXIT
printf '%s\n' "$notes" >"$notes_file"

bash packaging/package.sh

git push origin "$(git rev-parse --abbrev-ref HEAD)"
git push origin "$tag"

if gh release view "$tag" >/dev/null 2>&1; then
    gh release upload "$tag" "$archive" --clobber
    gh release edit "$tag" --notes-file "$notes_file"
else
    gh release create "$tag" "$archive" --notes-file "$notes_file"
fi

echo "==> Released $tag (KDE Store: upload $archive)"

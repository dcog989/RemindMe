#!/usr/bin/env bash
#
# Build the KRunner "Get New Plugins" payload. Run from any directory; output lands in dist/.
#
# The archive is a git-archive snapshot of the tracked sources with the top-level
# krunner-plugininstallerrc that KRunner's krunner-plugininstaller reads to install the runner.
# It ships sources, so the recipient builds the plugin on first activation via
# packaging/kns/krunner-remindme-launcher.sh.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

for file in krunner-plugininstallerrc packaging/kns/krunner-remindme-launcher.sh data/plasma-runner-remindme.desktop; do
    if ! git ls-files --error-unmatch "$file" >/dev/null 2>&1; then
        echo "$file is not committed; commit it before packaging" >&2
        exit 1
    fi
done

# Single source of truth for the version: the project() declaration.
version="$(sed -n 's/^project(remindme VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
if [ -z "$version" ]; then
    echo "could not read the version from project() in CMakeLists.txt" >&2
    exit 1
fi
# Likewise for the D-Bus service name set in CMakeLists.txt.
service_name="$(sed -n 's/^set(REMINDME_DBUS_SERVICE_NAME "\([^"]*\)").*/\1/p' CMakeLists.txt)"
if [ -z "$service_name" ]; then
    echo "could not read REMINDME_DBUS_SERVICE_NAME from CMakeLists.txt" >&2
    exit 1
fi
name="remindme-$version"
out="dist/$name.tar.gz"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/$name" dist
git archive HEAD | tar -x -C "$work/$name"
# Render the metadata the installer copies; the archive ships the template otherwise.
sed -i -e "s/@PROJECT_VERSION@/$version/" -e "s/@REMINDME_DBUS_SERVICE_NAME@/$service_name/" \
    "$work/$name/data/plasma-runner-remindme.desktop"

tar -czf "$out" -C "$work" "$name"
echo "==> $out"

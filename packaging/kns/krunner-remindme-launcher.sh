#!/usr/bin/env bash
#
# D-Bus activation entry point for a "Get New Plugins" install. The store payload
# ships sources, so the first activation configures, builds and installs the plugin
# (into ~/.local, no sudo); later activations start the installed binary directly.
#
# Activation happens without a terminal, so build output goes to $PROJECTDIR/kns-build.log.
# A first build can outlast the bus activation timeout (~25s); that one query fails, the retry
# then finds the binary and succeeds.
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="$project_dir/build/kns"
manifest="$build_dir/install_manifest.txt"
log="$project_dir/kns-build.log"

# The installed helper, read from the manifest cmake --install writes; it is the only installed
# file whose path ends in this name. Run it, not the build-tree copy, so the autostart entry the
# helper writes points at the install prefix rather than at this download folder.
installed_binary() {
    grep -m1 -E '/krunner-remindme$' "$manifest" 2>/dev/null || true
}

build() {
    echo "==> Configuring remindme"
    cmake -S "$project_dir" -B "$build_dir" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF \
        -DCMAKE_INSTALL_PREFIX="$HOME/.local" || return 1
    echo "==> Building remindme"
    cmake --build "$build_dir" --parallel || return 1
    echo "==> Installing remindme"
    cmake --install "$build_dir" || return 1
}

binary="$(installed_binary)"
if [ -z "$binary" ] || [ ! -x "$binary" ]; then
    if ! command -v cmake >/dev/null 2>&1; then
        echo "remindme: cmake is required for the first run; see README.md for the build dependencies" >&2
        exit 1
    fi
    # A second activation can arrive while the first build is still running. The lock makes it
    # wait, so two cmake runs cannot corrupt the same build directory; the lock is taken by the
    # subshell and released when it exits, so the running helper never holds it.
    mkdir -p "$build_dir"
    if ! (
        flock 9
        candidate="$(installed_binary)"
        if [ -n "$candidate" ] && [ -x "$candidate" ]; then
            exit 0
        fi
        build >"$log" 2>&1
    ) 9>"$build_dir/lock"; then
        echo "remindme: build failed, see $log" >&2
        exit 1
    fi
    binary="$(installed_binary)"
fi

if [ -z "$binary" ] || [ ! -x "$binary" ]; then
    echo "remindme: the install did not produce the helper binary" >&2
    exit 1
fi

exec "$binary"

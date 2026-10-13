#!/usr/bin/env bash
#
# Remove the files a previous install recorded in the CMake install manifest.
#
#   packaging/uninstall.sh            # user install (~/.local)
#   packaging/uninstall.sh --system   # system install (/usr, uses sudo)
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

preset="user"
sudo=""
for arg in "$@"; do
    case "$arg" in
        --system)
            preset="system"
            sudo="sudo"
            ;;
        --user)
            preset="user"
            sudo=""
            ;;
        -h|--help)
            sed -n '3,8p' "$0"
            exit 0
            ;;
        *)
            echo "unknown option: $arg" >&2
            exit 2
            ;;
    esac
done

manifest="build/$preset/install_manifest.txt"
if [ ! -f "$manifest" ]; then
    echo "no install manifest at $manifest; run packaging/install.sh first" >&2
    exit 1
fi

# Remove deepest paths first so emptied directories can be pruned afterwards. Read line by line
# rather than word-splitting, so a path containing spaces is removed whole and never split.
while IFS= read -r file; do
    [ -n "$file" ] || continue
    # shellcheck disable=SC2086
    $sudo rm -f "$file"
done < <(sort -r "$manifest")

echo "==> Removed installed files ($preset)"
if command -v kquitapp6 >/dev/null 2>&1; then
    kquitapp6 krunner >/dev/null 2>&1 || true
fi
if command -v krunner >/dev/null 2>&1; then
    krunner >/dev/null 2>&1 &
fi

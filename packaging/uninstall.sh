#!/usr/bin/env bash
#
# Remove the files a previous install recorded in the CMake install manifest.
#
#   ./packaging/uninstall.sh            # user install (~/.local)
#   ./packaging/uninstall.sh --system   # system install (/usr, uses sudo)
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir/.."

# shellcheck source=preset-args.sh
source "$script_dir/preset-args.sh" "$@"

manifest="build/$preset/install_manifest.txt"
if [ ! -f "$manifest" ]; then
    echo "no install manifest at $manifest; install a preset first (just reload)" >&2
    exit 1
fi

# Remove each recorded path. Read line by line rather than word-splitting, so a path containing
# spaces is removed whole and never split. Emptied directories are left in place.
while IFS= read -r file; do
    [ -n "$file" ] || continue
    # shellcheck disable=SC2086
    $sudo rm -f "$file"
done <"$manifest"

# The engine's runtime autostart entry is not in the install manifest; drop it too, so a login
# does not try to run the removed binary.
rm -f "${XDG_CONFIG_HOME:-$HOME/.config}/autostart/krunner-remindme.desktop"

echo "==> Removed installed files ($preset)"
echo "Timers and their backups are kept in ${XDG_DATA_HOME:-$HOME/.local/share}/krunner-remindme; delete that directory to start empty."
bash "$script_dir/restart-krunner.sh"

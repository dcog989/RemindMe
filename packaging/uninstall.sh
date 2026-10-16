#!/usr/bin/env bash
#
# Remove the files a previous install recorded in the CMake install manifest.
#
#   packaging/uninstall.sh            # user install (~/.local)
#   packaging/uninstall.sh --system   # system install (/usr, uses sudo)
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir/.."

# Defaults; the shared parser (packaging/preset-args.sh) overrides them from the arguments.
preset="user"
sudo=""
# shellcheck source=preset-args.sh
source "$script_dir/preset-args.sh" "$@"

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

# The engine's runtime autostart entry is not in the install manifest; drop it too, so a login
# does not try to run the removed binary.
rm -f "${XDG_CONFIG_HOME:-$HOME/.config}/autostart/krunner-remindme.desktop"

echo "==> Removed installed files ($preset)"
bash "$script_dir/restart-krunner.sh"

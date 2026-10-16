#!/usr/bin/env bash
#
# Build and install remindme, then restart KRunner so it picks up the runner.
#
#   packaging/install.sh            # install into ~/.local (no sudo)
#   packaging/install.sh --system   # install into /usr (uses sudo)
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir/.."

# Defaults; the shared parser (packaging/preset-args.sh) overrides them from the arguments.
preset="user"
sudo=""
# shellcheck source=preset-args.sh
source "$script_dir/preset-args.sh" "$@"

cmake --preset "$preset"
cmake --build --preset "$preset"
# shellcheck disable=SC2086
$sudo cmake --install "build/$preset"

echo "==> Installed ($preset). Restarting KRunner"
bash "$script_dir/restart-krunner.sh"
echo "Try: rme 5  |  rme list  |  rme cancel 3"

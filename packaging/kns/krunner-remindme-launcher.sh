#!/usr/bin/env bash
#
# D-Bus activation entry point for a "Get New Plugins" install. The payload ships a binary for
# the machine's architecture plus the files a normal install places, so this copies them into
# the user prefix and starts the binary. No compiler or development packages are involved.
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
data_home="${XDG_DATA_HOME:-$HOME/.local/share}"
# Outside data_home on purpose: the D-Bus service file and the autostart entry the helper writes
# both carry this path, so it has to outlive the download folder of this and later versions.
binary="$(dirname "$data_home")/libexec/remindme/krunner-remindme"
payload="$project_dir/bin/krunner-remindme-$(uname -m)"

notify() {
    if command -v kdialog >/dev/null 2>&1; then
        kdialog --error "$1"
    elif command -v notify-send >/dev/null 2>&1; then
        notify-send -u critical "RemindMe" "$1"
    fi
}

if [ ! -x "$payload" ]; then
    notify "RemindMe: this download has no binary for $(uname -m). Install it again from KRunner's Get New Plugins."
    exit 1
fi

mkdir -p "$(dirname "$binary")"
install -m 755 "$payload" "$binary"

# The staged app entry names the build prefix; point it at this copy so the alarm window's
# window matching and task manager entry stay right.
escaped="${binary//\\/\\\\}"
escaped="${escaped//&/\\&}"
sed -i "s|^Exec=.*|Exec=\"$escaped\"|" "$project_dir/share/applications/krunner-remindme.desktop"

# share/krunner/dbusplugins is not copied: krunner-plugininstaller already placed the runner
# metadata and the D-Bus service file that runs this script.
for dir in applications icons knotifications6 locale; do
    if [ -d "$project_dir/share/$dir" ]; then
        mkdir -p "$data_home"
        cp -a "$project_dir/share/$dir" "$data_home/"
    fi
done

if command -v ldd >/dev/null 2>&1 && ldd "$binary" 2>/dev/null | grep -q 'not found'; then
    notify "RemindMe: libraries the plugin needs are missing:"$'\n'"$(ldd "$binary" | grep 'not found')"
    exit 1
fi

exec "$binary"

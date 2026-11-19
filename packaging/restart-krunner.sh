#!/usr/bin/env bash
#
# Restart KRunner so it reloads (or drops) the runner. Called by packaging/install.sh and
# packaging/uninstall.sh after they change the installation, and by `just reload`.
#
# Prefers the Plasma 6 user service. Hand-starting a second krunner instead of restarting the
# service makes the two race for org.kde.krunner; the loser segfaults on exit and, since the unit
# has Restart=no, the service then stays dead.
set -euo pipefail

# Drop a running helper from any install prefix (kf6 = cmake, remindme = KDE Store launcher) so
# the freshly installed (or removed) binary is the one used.
pkill -f '[k]runner-remindme' || true

if systemctl --user cat plasma-krunner.service >/dev/null 2>&1; then
    pkill -f '[k]runner --daemon' || true
    # No reset-failed first: killing the daemon can let systemd collect this static unit, and
    # reset-failed would then fail with a misleading "not loaded". restart starts it either way.
    systemctl --user restart plasma-krunner.service
    systemctl --user is-active --quiet plasma-krunner.service || {
        echo "krunner did not start; check: journalctl --user -u plasma-krunner.service" >&2
        exit 1
    }
else
    if command -v kquitapp6 >/dev/null 2>&1; then
        kquitapp6 krunner >/dev/null 2>&1 || true
    fi
    if command -v krunner >/dev/null 2>&1; then
        setsid -f krunner --daemon >/dev/null 2>&1
    fi
fi

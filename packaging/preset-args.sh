#!/usr/bin/env bash
# shellcheck disable=SC2034  # $preset and $sudo are read by the script that sources this file
#
# Shared option parsing for packaging/install.sh and packaging/uninstall.sh. Source it with the
# caller's arguments and it sets $preset and $sudo, or exits on -h/--help or an unknown option.
#
#     source "$script_dir/preset-args.sh" "$@"
#
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
            printf 'usage: %s [--user|--system]\n' "$(basename "$0")"
            exit 0
            ;;
        *)
            printf 'unknown option: %s\n' "$arg" >&2
            exit 2
            ;;
    esac
done

#
# Task runner for remindme. Run `just` to list recipes.
# Build configuration lives in CMakePresets.json; hooks live in lefthook.yml.

set shell := ["bash", "-euo", "pipefail", "-c"]

# List available recipes
default:
    @just --list

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

# Configure a preset (user, release, system, asan)
configure preset="user":
    cmake --preset {{preset}}

# Build a configured preset
build preset="user" *args:
    cmake --build --preset {{preset}} -- {{args}}

# Run the test suite for a preset
test preset="user" *args:
    ctest --preset {{preset}} {{args}}

# Configure, build and test in one command
ci preset="user":
    cmake --workflow --preset {{preset}}

# Install a built preset
install preset="user":
    cmake --install build/{{preset}}

# Extract translatable strings into po/krunner_remindme.pot
pot:
    #!/usr/bin/env bash
    set -euo pipefail
    XGETTEXT="${XGETTEXT:-xgettext}" podir=po bash po/Messages.sh

# Remove all build trees
clean:
    cmake -E rm -rf build

# ---------------------------------------------------------------------------
# Quality
# ---------------------------------------------------------------------------

# Format C++ sources and shell scripts in place
format:
    #!/usr/bin/env bash
    set -uo pipefail
    cpp=$(git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' '*.hxx')
    sh=$(git ls-files '*.sh' '*.bash')
    if command -v clang-format >/dev/null 2>&1 && [ -n "$cpp" ]; then
        clang-format -i $cpp
    fi
    if command -v shfmt >/dev/null 2>&1 && [ -n "$sh" ]; then
        shfmt -w $sh
    fi

# Fail if C++ sources are not formatted
format-check:
    #!/usr/bin/env bash
    set -uo pipefail
    cpp=$(git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' '*.hxx')
    if [ -z "$cpp" ]; then
        echo "no C++ sources to check"
        exit 0
    fi
    if ! command -v clang-format >/dev/null 2>&1; then
        echo "clang-format not installed; skipping" >&2
        exit 0
    fi
    clang-format --dry-run --Werror $cpp

# Run clang-tidy over project sources (configures and builds first: tidy needs a current
# compile DB and the generated .moc files)
tidy preset="user" *args:
    #!/usr/bin/env bash
    set -euo pipefail
    just configure {{preset}}
    just build {{preset}}
    # Qt passes -mno-direct-extern-access to GCC; clang-tidy's clang rejects it, so strip it.
    # Skip the *_autogen TUs: their generated moc code trips checks that do not apply to our source.
    run-clang-tidy -p build/{{preset}} \
        -removed-arg=-mno-direct-extern-access \
        '^(?!.*(_autogen|mocs_compilation)).*\.(cpp|cc|cxx)$' \
        {{args}}

# Run every available linter; missing tools and not-yet-added configs are skipped
lint:
    #!/usr/bin/env bash
    set -uo pipefail
    status=0
    have() { command -v "$1" >/dev/null 2>&1; }
    step() { printf '==> %s\n' "$*"; "$@" || status=1; }
    skip() { printf -- '--- skip: %s\n' "$*"; }

    if have typos; then step typos .; else skip "typos"; fi

    sh=$(git ls-files '*.sh' '*.bash')
    # -x follows sourced files; -P SCRIPTDIR resolves their `# shellcheck source=` directive
    # relative to the script, not the CWD (otherwise install/uninstall trip SC1091).
    if have shellcheck && [ -n "$sh" ]; then step shellcheck -x -P SCRIPTDIR $sh; else skip "shellcheck"; fi

    if have yamllint; then step yamllint .; else skip "yamllint"; fi

    if have editorconfig-checker; then step editorconfig-checker; else skip "editorconfig-checker"; fi

    if have clang-format && [ -n "$(git ls-files '*.cpp' '*.h' '*.hpp')" ]; then
        step clang-format --dry-run --Werror $(git ls-files '*.cpp' '*.h' '*.hpp')
    else
        skip "clang-format"
    fi

    exit "$status"

# Report which optional tools are missing
doctor:
    #!/usr/bin/env bash
    set -uo pipefail
    missing=0
    for tool in cmake ninja clang-format clang-tidy cog lefthook \
                just typos shellcheck shfmt yamllint editorconfig-checker gitleaks; do
        if command -v "$tool" >/dev/null 2>&1; then
            printf 'ok      %s\n' "$tool"
        else
            printf 'MISSING %s\n' "$tool"
            missing=1
        fi
    done
    exit "$missing"

# ---------------------------------------------------------------------------
# Packaging & local run
# ---------------------------------------------------------------------------

# Build the KDE Store payload (prebuilt binary per architecture, in a container)
package:
    #!/usr/bin/env bash
    set -euo pipefail
    if [ ! -f packaging/package.sh ]; then
        echo "packaging/package.sh is missing" >&2
        exit 1
    fi
    exec bash packaging/package.sh

# Rebuild, reinstall and relaunch KRunner with the local runner
reload preset="user":
    #!/usr/bin/env bash
    set -euo pipefail
    cmake --build --preset {{preset}}
    cmake --install build/{{preset}}
    # Drop the helper from an older install so the freshly installed binary is the one activated.
    pkill -f 'libexec/kf6/[k]runner-remindme' || true
    if systemctl --user cat plasma-krunner.service >/dev/null 2>&1; then
        # Plasma 6 runs KRunner as a user service. Restarting it yields the single instance that
        # owns org.kde.krunner; hand-starting another krunner instead makes the two race for the
        # name, and the loser segfaults on exit (plasma-krunner.service has Restart=no, so it
        # then stays dead).
        pkill -f '[k]runner --daemon' || true
        systemctl --user reset-failed plasma-krunner.service || true
        systemctl --user restart plasma-krunner.service
        systemctl --user is-active --quiet plasma-krunner.service || {
            echo "krunner did not start; check: journalctl --user -u plasma-krunner.service" >&2
            exit 1
        }
    else
        kquitapp6 krunner || true
        setsid -f krunner --daemon
    fi

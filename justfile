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

# Run clang-tidy over the configured sources (requires `just configure`)
tidy preset="user" *args:
    run-clang-tidy -p build/{{preset}} {{args}}

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
    if have shellcheck && [ -n "$sh" ]; then step shellcheck $sh; else skip "shellcheck"; fi

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

# Build the KDE Store source/binary package
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
    @kquitapp6 krunner || true
    @pkill -f 'libexec/kf6/krunner-remindme' || true
    cmake --build --preset {{preset}}
    cmake --install build/{{preset}}
    @krunner &

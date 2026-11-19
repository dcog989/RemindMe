#
# Task runner for remindme. Run `just` to list recipes.
# Build configuration lives in CMakePresets.json; hooks live in lefthook.yml.

# Verify the repo: lint, format check, then configure, build and test.
default: check

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

# Verify the repo: lint, format check and the full ci workflow
check: lint format-check ci

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

# Fail if C++ or shell sources are not formatted
format-check:
    #!/usr/bin/env bash
    set -uo pipefail
    status=0
    cpp=$(git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' '*.hxx')
    sh=$(git ls-files '*.sh' '*.bash')
    if [ -n "$cpp" ]; then
        if command -v clang-format >/dev/null 2>&1; then
            clang-format --dry-run --Werror $cpp || status=1
        else
            echo "clang-format not installed; skipping" >&2
        fi
    fi
    if [ -n "$sh" ]; then
        if command -v shfmt >/dev/null 2>&1; then
            shfmt -d $sh || status=1
        else
            echo "shfmt not installed; skipping" >&2
        fi
    fi
    exit "$status"

# Run clang-tidy over project sources
tidy preset="user" *args:
    #!/usr/bin/env bash
    set -euo pipefail
    just configure {{preset}}
    just build {{preset}}
    # Qt passes -mno-direct-extern-access to GCC; clang-tidy's clang rejects it, so strip it.
    # Skip the *_autogen TUs: their generated moc code trips checks that do not apply to our source.
    # Warnings are errors so the CI step is not a no-op when a diagnostic lands.
    run-clang-tidy -p build/{{preset}} \
        -removed-arg=-mno-direct-extern-access \
        -warnings-as-errors='*' \
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

    exit "$status"

# Report which optional tools are missing
doctor:
    #!/usr/bin/env bash
    set -uo pipefail
    missing=0
    for tool in cmake ninja clang-format clang-tidy cog lefthook \
                just typos shellcheck shfmt yamllint editorconfig-checker gitleaks podman gh; do
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

# Configure, build, reinstall and relaunch KRunner with the local runner
reload preset="user":
    #!/usr/bin/env bash
    set -euo pipefail
    cmake --preset {{preset}}
    cmake --build --preset {{preset}}
    cmake --install build/{{preset}}
    bash packaging/restart-krunner.sh

# Bump the version from conventional commits and tag it.
release:
    cog bump --auto

# Verify a commit message file, as the commit-msg hook passes it.
commit-check file *args:
    cog verify --file {{file}} {{args}}

# clang-format over a file list, as the pre-commit hook passes it.
format-clang-staged *files:
    clang-format -i {{files}}

# shfmt over a file list, as the pre-commit hook passes it.
format-shfmt-staged *files:
    shfmt -w {{files}}

# typos over a file list, as the pre-commit hook passes it. --force-exclude keeps the
# _typos.toml exclusions in effect for explicitly named files (staged binaries, CHANGELOG.md).
lint-typos-staged *files:
    typos --force-exclude {{files}}

# shellcheck over a file list, as the pre-commit hook passes it.
lint-shellcheck-staged *files:
    shellcheck -x -P SCRIPTDIR {{files}}

# yamllint over a file list, as the pre-commit hook passes it.
lint-yamllint-staged *files:
    yamllint {{files}}

# editorconfig-checker over a file list, as the pre-commit hook passes it.
lint-editorconfig-staged *files:
    editorconfig-checker {{files}}

# gitleaks over the staged index, as the pre-commit hook passes it.
lint-gitleaks-staged *files:
    gitleaks protect --staged --no-banner

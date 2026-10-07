# Agent Directives

## Project Specifics

- Name: RemindMe
- Description: A third-party (user) KRunner plugin for KDE Plasma 6. Adds a timer function: type a trigger word followed by a duration (and optional message) to start a countdown; when time is up, a dedicated alarm window pops up with Snooze (+1 min) and Dismiss. Active timers can be listed and cancelled from KRunner, and they survive KRunner restarts.
- Goal: a **standalone, third-party user plugin** distributed through the [KDE Store](https://store.kde.org/) and installable via KRunner's **Get New Plugins** flow; not part of KDE Plasma.
- Status: migration from the in-tree plasma-workspace plugin is complete; the extraction source has been removed. One `krunner-remindme` process is the engine, the KRunner DBus2 runner and the alarm window.
- Tech: C++20, Qt 6.9+, KDE Frameworks 6, CMake 3.29+, Extra CMake Modules (ECM)
- License: **GPL-3.0-or-later**, declared in a single `LICENSE` file at the repo root.

### File Access

- Repo root: `/home/bubba/Projects/remindme`
- Scratch: `/tmp/*`
- Read-only: `.env*`, `.git/`

### Target Layout

```text
remindme/
├── CMakeLists.txt                       # top-level project (ECM, KF6, options, subdirs)
├── krunner-plugininstallerrc            # KRunner "Get New Plugins" install descriptor
├── src/
│   ├── engine/                          # timer engine + alarm + entry point → krunner-remindme
│   │   ├── main.cpp                     # D-Bus service, engine and adaptor wiring
│   │   ├── remindmeengine.{h,cpp}       # timer engine (wall-clock deadlines, persistence)
│   │   ├── timerinfo.h                  # TimerInfo model type (persisted, signalled)
│   │   ├── deadlinescheduler.{h,cpp}    # deadline-wake abstraction + production factory
│   │   ├── timerfddeadlinescheduler.{h,cpp} # timerfd (realtime) scheduler
│   │   ├── qtimerdeadlinescheduler.{h,cpp}  # QTimer + wall-clock sweep (test-only)
│   │   ├── remindmetime.{h,cpp}         # duration parser
│   │   ├── remindmealarmdialog.{h,cpp}  # Snooze/Dismiss window
│   │   ├── remindmealarmnotification.{h,cpp} # shared alarm KNotification factory
│   │   ├── remindmealarmpresenter.{h,cpp} # dialog/sound presentation + lifetime
│   │   ├── remindmealarmsound.{h,cpp}   # looping expiry-alarm sound + stop timer
│   │   └── remindmeautostart.{h,cpp}    # session autostart toggling
│   └── runner/                          # org.kde.krunner1 D-Bus runner adaptor
│       ├── remindmeadaptor.{h,cpp}      # Match/Run/Actions/Config/Teardown
│       └── remotematch.h                # D-Bus wire types for the runner protocol
├── data/                                # installed data files
│   ├── plasma-runner-remindme.desktop   # DBus2 runner metadata
│   └── krunner-remindme.notifyrc
├── autotests/                           # ctest: remindmetimetest, remindmeenginetest, remindmeautostarttest, deadlineschedulertest, remindmeintegrationtest
├── po/                                  # translation extraction (Messages.sh)
├── packaging/                           # build.sh / install.sh / uninstall.sh / package.sh / release.sh
│   └── kns/                             # first-run build launcher for the store package
└── README.md
```

### Workflow

- Task runner: `just` (see `justfile`; `just` lists recipes). Never hand-roll configure/build commands when a recipe exists.
- Configure/build/test: `just ci` (runs `cmake --workflow --preset user`). Individual steps: `just configure`, `just build`, `just test`.
- Presets live in `CMakePresets.json`: `user` (`~/.local`, Debug), `release`, `system`, `asan`. `CMAKE_EXPORT_COMPILE_COMMANDS` is on.
- Install: `just install` (or `cmake --install build/<preset>`).
- Format: `just format` (clang-format + shfmt); verify with `just format-check`. Don't let a build regenerate the committed `.clang-format`.
- Lint: `just lint`; missing tools are skipped. `just doctor` reports gaps.
- Static analysis: `just tidy` (clang-tidy, config in `.clang-tidy`).
- Git hooks: `lefthook.yml`; install once with `lefthook install`. Pre-commit formats/lints, `commit-msg` runs `cog verify`, pre-push builds and tests.
- Releases: `cog bump --auto` (`cog.toml`) bumps the `project()` version in `CMakeLists.txt` (the single version source of truth), updates `CHANGELOG.md`, then `post_bump_hooks` push the branch and tag, build the store payload and open the GitHub release. The KDE Store upload stays manual.
- CI: `.github/workflows/ci.yml` (Arch container: build + test + lint).

### Rules

- Keep one executable: the timer engine, the `org.kde.krunner1` D-Bus runner adaptor and the alarm window live in the same process. Do not reintroduce a compiled KRunner plugin, a client/marshalling layer, a second target or a helper process.
- Tooling is committed at the repo root (`CMakePresets.json`, `justfile`, `lefthook.yml`, `cog.toml`, `.clang-format`, `.clang-tidy`, `.yamllint`, `.editorconfig`, `.editorconfig-checker.json`, `_typos.toml`). Keep it working when targets or file types change; don't add a second task runner or hook manager.
- Commit messages follow Conventional Commits (`cog verify`). Do not bypass hooks (`--no-verify`).
- Follow KRunner/KF6 DBus-runner conventions: `#pragma once`, `QDBusAbstractAdaptor`, `QStringLiteral`, `i18n()`. The runner is a D-Bus service (`io.github.dcog989.remindme`), not a compiled plugin.
- Use ECM install-dir variables (`KDE_INSTALL_*`) rather than hardcoded paths, so user- and system-prefix installs both work.
- Emulate existing test procedures: pure-logic unit tests, and D-Bus runner integration tests via a real subprocess (`krunner_configure_test` + `AbstractRunnerTest` DBus branch). Don't invent new patterns where an existing one fits.
- Run targeted tests, not the full `ctest` suite, on trivial changes.
- Require confirmation for: adding/removing dependencies, and any operation outside the project root.

---

## General Guidelines

### Code Changes

- For non-trivial work, propose an approach and confirm before implementing.
- Keep modifications minimal and scoped; prefer incremental improvements over rewrites. Ask before architectural changes.
- Use explicit types and named constants (no magic numbers).
- Return explicit error types; do not suppress exceptions.
- Follow standard repository linting and formatting configs.
- Decompose files over 400 lines if they mix concerns.
- Use clear naming over comments; reserve comments for complex workarounds or non-obvious issues — why, not what.
- Never run git mutations (commit, push, reset, rebase, amend) unless explicitly instructed.
- Do not create documentation files unless explicitly requested.

### Verification

- Do not run test, lint, format, or type-check commands; the user builds, tests, and lints manually.
- Run them only when the user explicitly asks.

### Author Environment

- CachyOS, KDE Plasma 6, Wayland, Btrfs.
- fish shell, Ghostty terminal, Fresh TUI editor, yay package manager, bun npm manager, Firefox, and Zed code editor.

### Testing

- Do not create test files for trivial changes, or for behavior that is not reliably unit-testable in the test environment (e.g. UI layout/click mapping). Prefer no new files; only add a test when the logic is genuinely testable and worth guarding.

### Definition of Done

- Logic fully implemented.
- Existing docs updated if public interfaces changed.
- When required by the `Verification` rules, run the corresponding `Workflow` command.
- On completion of an update or fix, print a concise conventional commit message in a fenced code block.

### Communication Style

- Provide concise, actionable responses.
- Ask clarifying questions when requirements are ambiguous.
- Flag potential risks or edge cases proactively.
- Do not pretend to understand how the user feels.
- Never editorialise your answer. No "to be honest", "honestly", hedging, disclaimers, or meta-commentary — just answer.

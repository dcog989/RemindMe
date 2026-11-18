# Agent Directives

## Project Specifics

- Name: RemindMe
- Description: A third-party (user) KRunner plugin for KDE Plasma 6. Adds a timer function: type a trigger word followed by a duration (and optional message) to start a countdown; when time is up, a dedicated alarm window pops up with Snooze (+1 min) and Dismiss. Active timers can be listed and cancelled from KRunner, and they survive KRunner restarts.
- Goal: a **standalone, third-party user plugin** distributed through the [KDE Store](https://store.kde.org/) and installable via KRunner's **Get New Plugins** flow; not part of KDE Plasma.
- Status: migration from the in-tree plasma-workspace plugin is complete; the extraction source has been removed. One `krunner-remindme` process is the engine, the KRunner DBus2 runner and the alarm window.
- Tech: C++20, Qt 6.9+, KDE Frameworks 6, CMake 3.29+, Extra CMake Modules (ECM)
- License: **GPL-3.0-or-later**, declared in a single `LICENSE` file at the repo root.

### File Access

- Repo root: `/home/bubba/Projects/RemindMe`
- Scratch: `/tmp/*`
- Read-only: `.env*`, `.git/`, `.docs/`

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
│   │   ├── qtimerdeadlinescheduler.{h,cpp}  # QTimer + wall-clock sweep (timerfd fallback)
│   │   ├── remindmetime.{h,cpp}         # duration parser
│   │   ├── remindmealarmdialog.{h,cpp}  # Snooze/Dismiss window
│   │   ├── remindmealarmnotification.{h,cpp} # shared alarm KNotification factory
│   │   ├── remindmealarmpresenter.{h,cpp} # dialog/sound presentation + lifetime
│   │   ├── remindmereminderlistdialog.{h,cpp} # overview window listing every waiting timer
│   │   ├── remindmetrayicon.{h,cpp}     # tray icon shown while timers wait
│   │   └── remindmeautostart.{h,cpp}    # session autostart toggling
│   └── runner/                          # org.kde.krunner1 D-Bus runner adaptor
│       ├── remindmeadaptor.{h,cpp}      # Match/Run/Actions/Config/Teardown
│       └── remotematch.h                # D-Bus wire types for the runner protocol
├── data/                                # installed data files
│   ├── io.github.dcog989.remindme.svg   # plugin icon (installed to hicolor)
│   ├── plasma-runner-remindme.desktop   # DBus2 runner metadata
│   ├── krunner-remindme.desktop         # application entry KWin matches the alarm window against
│   └── krunner-remindme.notifyrc
├── autotests/                           # ctest: remindmetimetest, remindmeenginetest, remindmeadaptertest, remindmeautostarttest, deadlineschedulertest, remindmealarmpresentertest, remindmetrayicontest, remindmeintegrationtest
├── po/                                  # translation extraction (Messages.sh)
├── packaging/                           # build.sh / install.sh / uninstall.sh / package.sh / release.sh / restart-krunner.sh / preset-args.sh
│   └── kns/                             # install launcher for the store package (prebuilt payload)
└── README.md
```

### Workflow

- Task runner: `just` (see `justfile`; `just` lists recipes). Never hand-roll configure/build commands when a recipe exists.
- Verify everything: `just check` (lint, format check, configure, build, test); it is the default recipe.
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

- For non-trivial work, propose an approach and confirm before implementing, including architectural changes.
- Keep modifications minimal and scoped; prefer incremental improvements over rewrites.
- Use explicit types and named constants (no magic numbers).
- Return explicit error types; do not suppress exceptions.
- Follow standard repository linting and formatting configs.
- Decompose files over 400 lines if they mix concerns.
- Name things clearly; reserve comments for the non-obvious why (workarounds, edge cases).
- Never run git mutations (commit, push, reset, rebase, amend) unless explicitly instructed.
- Do not create documentation files unless explicitly requested.
- Adhere to <https://develop.kde.org/hig/>.

### Author Environment

- Ryzen 5800X3D, Radeon RX 7900 XT, Display (AW3225QF); CachyOS, KDE, KWin / Wayland, Btrfs.
- fish shell, Ghostty terminal, Fresh TUI editor, yay package manager, bun npm manager, Firefox, Zed code editor.

### Testing

- Add tests only when the logic is genuinely testable and worth guarding. No test files for trivial changes or behavior the environment cannot test reliably (e.g. UI layout/click mapping).

### Definition of Done

- Logic fully implemented.
- Existing docs updated if public interfaces changed.
- Run the corresponding `Workflow` command when `Verification` requires it.

### Commit Messages

- On completion of an update or fix, print a conventional commit message in a fenced code block.
- Add a body only when the "why" isn't obvious.
- Never hard-wrap: one physical line per paragraph, however long.

### Communication Style

- Format: short declarative sentences, one idea per line.
- Instructions: numbered steps, one action each (exact command, path, or code); no prose recap.
- Explanations: essential context only. End question answers with `TL;DR:`, one sentence; omit for status updates and completed work.
- Tone: no preamble, restating, hedging, disclaimers, emotional framing, or closing offers. Keep every fact, command, flag, path, default, caveat.
- Edge Cases: state risks explicitly; ask when requirements are ambiguous.

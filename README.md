# remindme

A timer plugin for KDE Plasma 6 KRunner. Type a trigger word (`remindme` or `rme`) followed by a duration and an optional message to start a countdown. When the time is up a dedicated alarm window pops up with **Snooze (+1 min)** and **Dismiss**. Active timers can be listed and cancelled from KRunner, and they survive restarts.

`remindme` is distributed through the [KDE Store](https://store.kde.org/) and is installable from KRunner's **Get New Plugins** flow. The store payload ships sources, so the runner is built on first use (see [Install](#install)). It is **not** part of KDE Plasma and is **not** shipped by the Plasma release; the plugin lives entirely in this standalone repository.

## Usage

Trigger words: `remindme`, `rme` (case-insensitive).

| Input         | Meaning                                  |
| ------------- | ---------------------------------------- |
| `5`           | 5 minutes                                |
| `5.5`         | 5 minutes 30 seconds                     |
| `.5`          | 0.5 minutes = 30 seconds                 |
| `0:20`        | 20 seconds                               |
| `20s`         | 20 seconds                               |
| `4h`          | 4 hours                                  |
| `5:30`        | 5 minutes 30 seconds                     |
| `4:15:30`     | 4 hours 15 minutes 30 seconds            |
| `8 message`   | 8 minutes with the message `message`     |

- A bare number means minutes. Fractional values (`.5`, `.5m`, `.5h`, …) are accepted for bare numbers and unit forms, rounded to whole seconds.
- `0` (or any non-positive) duration is invalid; the maximum is `99:59:59`.
- The message is everything after the duration and may contain spaces.

| Action                  | Query                 |
| ----------------------- | --------------------- |
| Create timer            | `rme 5`, `rme 8 msg`  |
| List active timers      | `rme list`            |
| Cancel by id            | `rme cancel 3`        |
| Cancel by message       | `rme cancel meeting`  |
| List timers to cancel   | `rme cancel`          |
| Usage / help            | `rme`                 |

- Pressing Enter on a create match starts the timer. `rme list` shows each active timer with its `#id` and reminder time; activating a timer pops up its alarm, while its `✕` action cancels it. `rme cancel` with no target lists every timer so one can be picked and cancelled.
- "Cancel by message" matches the message *exactly* (case-sensitive), so a short typo cannot cancel a large set of timers. Use `rme cancel <id>` or `rme cancel #<id>` for a specific timer.
- On expiry the alarm window opens with the message plus Snooze and Dismiss, and plays a looping alarm sound until the window is closed.
- Timers survive KRunner restarts and session logout: the runner process owns the engine, persists timers to disk, and uses wall-clock deadlines (epoch), so suspend and restart are handled naturally.

## Install

### From the KDE Store

1. Open **System Settings → Search → Plasma Search**.
2. Click **Get New Plugins…**.
3. Search for **Remind me** and install it.
4. Restart KRunner: `kquitapp6 krunner && krunner &` (or log out and back in).

The store package ships sources: the first time KRunner activates the runner it is configured,
built and installed into `~/.local` (no sudo), which needs the [build dependencies](#from-source)
and can take a minute. The build runs without a terminal, so its output goes to
`~/.local/share/krunner-sources/remindme-*/kns-build.log`. Try `rme 5` once; if nothing happens,
check that log. Later activations start the installed binary directly.

### From source

Build dependencies: a C++20 compiler, CMake 3.29+, Qt 6.9+, KDE Frameworks 6 (`CoreAddons`, `I18n`, `Notifications`, `Config`) and [Extra CMake Modules](https://invent.kde.org/frameworks/extra-cmake-modules). Building the tests additionally needs `KRunner` (for `AbstractRunnerTest`).

The build is driven by [CMake presets](CMakePresets.json), with a thin [`justfile`](justfile) for convenience. If you have [`just`](https://github.com/casey/just) installed:

```sh
just configure          # cmake --preset user
just build              # cmake --build --preset user
just install            # install into ~/.local
```

Without `just`:

```sh
cmake --preset user
cmake --build --preset user
cmake --install build/user
```

Presets: `user` (`~/.local`, Debug), `release`, `system` (`/usr`), `asan` (ASan + UBSan). Configure, build and test in one go with `just ci <preset>` or `cmake --workflow --preset user`.

Because `remindme` is a D-Bus runner, a `~/.local` install needs no `QT_PLUGIN_PATH` and no sudo: KRunner scans
`~/.local/share/krunner/dbusplugins`, D-Bus scans
`~/.local/share/dbus-1/services`, and the service `Exec=` is an absolute path. Restart KRunner afterwards so it reloads the runner list:

```sh
kquitapp6 krunner && krunner &
```

The packaging helpers wrap the same steps and restart KRunner:

```sh
packaging/build.sh
packaging/install.sh            # or: packaging/install.sh --system
packaging/uninstall.sh          # driven by the CMake install manifest
```

## Tech Stack

- C++20, Qt 6.9+, KDE Frameworks 6, CMake 3.29+, Extra CMake Modules.
- One executable is the timer engine, the KRunner runner and the alarm window.
  It is exposed to KRunner through the `org.kde.krunner1` D-Bus runner protocol
  (`X-Plasma-API=DBus2`) and auto-activated from
  `io.github.dcog989.remindme.service`; no compiled KRunner plugin is involved.
- Matching reads the engine in-process, so a query never crosses D-Bus. The
  process persists timers to disk and exits when idle; D-Bus activation brings it
  back on demand.

## Project Layout

```text
remindme/
├── CMakeLists.txt              # top-level project (ECM, KF6, options, subdirs)
├── krunner-plugininstallerrc   # KRunner "Get New Plugins" install descriptor
├── src/
│   ├── engine/                 # timer engine, alarm window, autostart, entry point
│   │   ├── main.cpp                    # D-Bus service + engine + adaptor wiring
│   │   ├── remindmeengine.{h,cpp}      # timer engine (wall-clock deadlines, persistence)
│   │   ├── remindmetime.{h,cpp}        # duration parser
│   │   ├── remindmealarmdialog.{h,cpp} # Snooze/Dismiss window
│   │   └── remindmeautostart.{h,cpp}   # session autostart toggling
│   └── runner/
│       ├── remindmeadaptor.{h,cpp}     # org.kde.krunner1 Match/Run/Actions/Config
│       └── remotematch.h               # D-Bus wire types for the runner protocol
├── data/
│   ├── plasma-runner-remindme.desktop  # DBus2 runner metadata
│   └── krunner-remindme.notifyrc       # notification/sound config
├── autotests/                  # ctest suite
├── packaging/                  # build.sh / install.sh / uninstall.sh / package.sh
│   └── kns/                    # first-run build launcher for the store package
├── po/                         # translations (Messages.sh + catalogs)
└── README.md
```

## Development

- Configure, build and test: `just ci` (or `cmake --workflow --preset user`).
- Test only: `just test` (or `ctest --preset user`).
- Format C++ and shell sources: `just format`; verify with `just format-check`.
- Lint everything available: `just lint` (typos, shellcheck, yamllint,
  editorconfig-checker, clang-format). `just doctor` lists missing tools.
- Static analysis: `just tidy` (`clang-tidy` over the configured build).
- Rebuild, reinstall and relaunch KRunner: `just reload`.
- Package for the KDE Store: `just package`.

### Git hooks

Hooks are managed with [Lefthook](https://lefthook.dev/) and installed once per
clone:

```sh
lefthook install
```

Pre-commit formats C++ and shell files (re-staging the result) and runs the
available linters; `commit-msg` enforces
[Conventional Commits](https://www.conventionalcommits.org/) via
[Cocogitto](https://docs.cocogitto.io/) (`cog verify`); pre-push builds and tests.
Hooks for tools that are not installed are skipped, so a partial toolchain still
works.

Releases are one command: `cog bump --auto` bumps the `project()` version in
`CMakeLists.txt` (the single source of truth, rendered into the runner metadata), updates
`CHANGELOG.md`, builds the store payload, then pushes the branch and `v*` tag and opens a GitHub
release with the payload attached. The [KDE Store](https://store.kde.org/) upload itself stays
manual.

Handy Arch packages:

```sh
paru -S just lefthook cocogitto typos shellcheck shfmt yamllint \
        gitleaks editorconfig-checker clang
```

## License

GPL-3.0-or-later. See [`LICENSE`](LICENSE).

## Links

- Source: <https://github.com/dcog989/remindme>
- Inspiration: <https://store.kde.org/p/1081014/>
- KRunner plugin development: <https://develop.kde.org/docs/plasma/krunner/>
- Discussion: <https://discuss.kde.org/t/is-there-a-krunner-timer/49132>

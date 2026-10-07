# RemindMe

A timer plugin for KDE Plasma 6. Allows you to create, list, and cancel timers from KRunner with optional message and snooze.

## Install

1. Open *System Settings → Search → Plasma Search*.
2. Click *Get New Plugins…*.
3. Search for *RemindMe* and install it.
4. Restart KRunner: `kquitapp6 krunner && krunner &` (or log out and back in).

Optionally, go to *System Settings → Search → Plasma Search* and make RemindMe a favorite and move it above others as required.

## Usage

Trigger words: `remindme` or `rme`.

| Action                  | Query                  |
| ----------------------- | ---------------------- |
| Create timer            | `rme 5`, `rme 3h msg`  |
| List active timers      | `rme list`             |
| Cancel by id            | `rme cancel 3`         |
| Cancel by message       | `rme cancel meeting` (case-sensitive) |
| List timers to cancel   | `rme cancel`           |
| Usage / help            | `rme`                  |

| Input             | Meaning                             |
| ----------------- | ----------------------------------- |
| `rme 5`           | 5 minutes                           |
| `rme 5.5`         | 5 minutes 30 seconds                |
| `rme .5`          | 0.5 minutes = 30 seconds            |
| `rme 0:20`        | 20 seconds                          |
| `rme 20s`         | 20 seconds                          |
| `rme 4h`          | 4 hours                             |
| `rme 5:30`        | 5 minutes 30 seconds                |
| `rme 4:15:30`     | 4 hours 15 minutes 30 seconds       |
| `rme 8 pasta!`    | 8 minutes with the message `pasta!` |

---

## Technical

### Development

- `just ci` - configure, build and test
- `just test` - test only
- `just format` — clang-format + shfmt in place
- `just format-check` — fails if C++ isn't formatted
- `just lint` — typos, shellcheck, yamllint, editorconfig-checker, clang-format (skips missing tools)
- `just tidy` — clang-tidy over the configured preset (configures and builds first)
- `just reload` — rebuild, reinstall and relaunch KRunner
- `just package` — package for the KDE Store

### Install from source

Build dependencies: a C++20 compiler, CMake 3.29+, Qt 6.9+ (`Core`, `DBus`, `Widgets`), KDE Frameworks 6 (`CoreAddons`, `I18n`, `Notifications`, `Config`) and [Extra CMake Modules](https://invent.kde.org/frameworks/extra-cmake-modules). Building the tests additionally needs Qt `Test` and the KF6 `Runner` framework (for `AbstractRunnerTest`).

The build is driven by [CMake presets](CMakePresets.json), with a thin [`justfile`](justfile) for convenience. Requires [`just`](https://github.com/casey/just):

```sh
just configure          # cmake --preset user
just build              # cmake --build --preset user
just install            # install into ~/.local
just reload             # kills the running KRunner/helper, rebuilds the user preset, installs it, and relaunches KRunner.
```

Without `just`:

```sh
cmake --preset user
cmake --build --preset user
cmake --install build/user
```

Presets: `user` (`~/.local`, Debug), `release`, `system` (`/usr`), `asan` (ASan + UBSan). Configure, build and test in one go with `just ci` (or `cmake --workflow --preset user`); only `user` has a workflow preset, while `user` and `asan` also have test presets.

Because `remindme` is a D-Bus runner, a `~/.local` install needs no `QT_PLUGIN_PATH` and no sudo: KRunner scans `~/.local/share/krunner/dbusplugins`, D-Bus scans `~/.local/share/dbus-1/services`, and the service `Exec=` is an absolute path. Restart KRunner afterwards so it reloads the runner list:

```sh
kquitapp6 krunner && krunner &
```

The packaging helpers wrap the same steps and restart KRunner:

```sh
packaging/build.sh
packaging/install.sh            # or: packaging/install.sh --system
packaging/uninstall.sh          # driven by the CMake install manifest
```

---

## License

[GPL-3.0-or-later](LICENSE).

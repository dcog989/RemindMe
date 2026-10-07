# Changelog
All notable changes to this project will be documented in this file. See [conventional commits](https://www.conventionalcommits.org/) for commit guidelines.

- - -
## v0.2.8 - 2026-10-07
#### Bug Fixes
- (**engine**) guard the looping alarm notification with a QPointer - (f3be7f8) - dcog989
- (**engine**) write a spec-compliant Exec and refresh stale autostart paths - (151b993) - dcog989
- (**engine**) claim the D-Bus name before any startup side effects - (9f75cd5) - dcog989
- (**engine**) validate colon and fractional durations - (45ba415) - dcog989
- (**engine**) label the reminder action and show its due time - (267ffc5) - dcog989
- (**engine**) persist fired alarms so they survive a restart - (aa2e6cd) - dcog989
- (**engine**) report persistence failures and preserve corrupt state - (cec39ab) - dcog989
- (**engine**) keep process alive while a headless alarm sound is pending - (594f6ad) - dcog989
- (**engine**) guard autostart early-return with QFile::exists - (ff7051c) - dcog989
- (**engine**) include <utility> for std::as_const - (a18db68) - dcog989
- (**engine**) decide timerfd use before moving the clock argument - (fb71ef8) - dcog989
- (**justfile**) build before restarting KRunner and detach the relaunch - (5b18888) - dcog989
- (**justfile**) stop reload's pkill from matching its own recipe shell - (5ca78eb) - dcog989
- (**release**) build the store payload before pushing the tag - (363f267) - dcog989
- (**runner**) make cancel-by-message match, validate, and honor "#" - (13b094a) - dcog989
- (**runner**) parse cancel ids through the locale - (b50203f) - dcog989
- (**runner**) show usage hints under the trigger alias the user typed - (8348568) - dcog989
- (**runner**) use KRunner activation token to focus the reminder window - (3c85fb1) - dcog989
- (**time**) detect fractional durations with the locale decimal separator - (028c2dc) - dcog989
- clear the activation token and tighten two small leaks - (f4d3843) - dcog989
#### Performance
- (**autostart**) skip rewriting entry when state is unchanged - (52ae893) - dcog989
- (**engine**) run the fallback sweep only while timers exist - (c3bf81a) - dcog989
#### Documentation
- correct README and AGENTS details against the codebase - (1fafa2f) - dcog989
- refined - (6189944) - dcog989
- drop references to the removed plasma-workspace extraction source - (0809d3e) - dcog989
#### Build
- (**i18n**) wire up translation extraction and installation - (26503d1) - dcog989
- (**presets**) add workflow and test presets for every preset - (6f5f5ff) - dcog989
- (**release**) use the cog changelog section as the release notes - (c06f4cb) - dcog989
#### CI
- drop the unused ccache install - (2d157e3) - dcog989
- pin the Arch image and package snapshot - (ab8be93) - dcog989
#### Refactoring
- (**alarm**) replace the expired bool with an AlarmKind enum - (11fd22a) - dcog989
- (**alarm**) add shared makeAlarmNotification factory - (d82b683) - dcog989
- (**alarm-dialog**) derive the snooze label from snoozeDurationMs - (e1b9e48) - dcog989
- (**alarm-dialog**) extract overdueText() and AlarmSound - (bb9c478) - dcog989
- (**build**) define the D-Bus service name once - (e2674b6) - dcog989
- (**engine**) use toInteger() and a side-effect-free number parse - (a73360d) - dcog989
- (**engine**) make list() const - (6e87a26) - dcog989
- (**engine**) return std::optional from create instead of a sentinel id - (8d24880) - dcog989
- (**engine**) merge rearmExpiredTimer into snooze() - (478cff1) - dcog989
- (**engine**) use camelCase for the engine API - (01a5bae) - dcog989
- (**engine**) use a stack RemindmeAutostart and drop the default destructor - (fc8e6f5) - dcog989
- (**engine**) compile the QTimer scheduler only for tests - (0aed602) - dcog989
- (**engine**) drop non-Linux timerfd guards - (5c15561) - dcog989
- (**engine**) add shared parseLocaleInt helper - (9e3e72e) - dcog989
- (**engine**) move TimerInfo and dialog constants out of the engine - (589b5db) - dcog989
- (**engine**) extract AlarmPresenter from main() - (3921944) - dcog989
- (**engine**) extract DeadlineScheduler from RemindmeEngine - (1638c72) - dcog989
- (**engine**) add isEmpty()/contains() to skip List() copies - (c69a458) - dcog989
- (**engine**) key timers by id in a QMap for deterministic expiry - (47d9a9a) - dcog989
- (**engine**) drop unused Q_SLOTS on the domain API - (8d0c537) - dcog989
- (**engine**) reuse tryQuit in the idle-activation fallback - (142d296) - dcog989
- (**runner**) merge identical cancel prefix branches - (6a184f6) - dcog989
- (**runner**) name the "cancel " prefix constant - (711731e) - dcog989
- (**runner**) name match relevance literals - (cd33daf) - dcog989
- (**runner**) replace startsWith/mid pairs with stripPrefix helper - (f9fee5a) - dcog989
- (**runner**) name match-id prefixes and cross-reference trigger words - (79241a1) - dcog989
- (**runner**) centralize match construction in a makeMatch factory - (889c459) - dcog989

- - -

## v0.2.7 - 2026-09-22
#### CI
- (**release**) autostash before the pre-bump pull - (13da70a) - dcog989
- (**release**) sync main before bumping - (8ba42d5) - dcog989

- - -

## v0.2.5 - 2026-09-22
#### CI
- (**editorconfig**) disable indentation check and exclude generated files - (c5f5810) - dcog989

- - -

## v0.2.4 - 2026-09-22

- - -

## v0.2.3 - 2026-09-22
#### CI
- fix shellcheck findings in launcher and Messages.sh - (b7dd96d) - dcog989

- - -

## v0.2.2 - 2026-09-22
#### CI
- install git in build job and skip generated changelog in typos - (5a43de8) - dcog989

- - -

## v0.2.1 - 2026-09-22
#### CI
- mark workspace as a safe git directory in containers - (1306e92) - dcog989
- bump actions/checkout to v5 for Node 24 - (4e7758e) - dcog989

- - -

## v0.2.0 - 2026-09-22
#### Build
- (**release**) publish from cog post_bump_hooks + dependabot - (836056f) - dcog989

- - -

## v0.1.0 - 2026-09-22
#### Features
- (**packaging**) install via KRunner's Get New Plugins - (67c4300) - dcog989
- <span style="background-color: #d73a49; color: white; padding: 2px 6px; border-radius: 3px; font-weight: bold; font-size: 0.85em;">BREAKING</span>run remindme as a single DBus runner process - (592294e) - dcog989
#### Bug Fixes
- (**runner**) disambiguate cancel match-id prefixes - (29d9ba4) - dcog989
#### Documentation
- readme refined - (33f3ce9) - dcog989
- readme + agents refined - (d564b75) - dcog989
#### Build
- (**version**) derive runner metadata version from project() - (e07973d) - dcog989
#### Refactoring
- (**engine**) name idle-quit fallback timeout constant - (a63b1e6) - dcog989
- (**runner**) drop redundant quint64 cast in addCreateMatch - (b86f262) - dcog989

- - -

Changelog generated by [cocogitto](https://github.com/cocogitto/cocogitto).
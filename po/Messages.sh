#! /usr/bin/env bash
#
# Extracts translatable strings. KDE's build system defines $XGETTEXT and $podir; standalone runs
# fall back to plain xgettext, so the KDE keyword names are passed explicitly.
# shellcheck disable=SC2154
$XGETTEXT --from-code=UTF-8 --force-po --keyword=i18n --keyword=i18np:1,2 "$(dirname "$0")"/../src/engine/*.cpp "$(dirname "$0")"/../src/runner/*.cpp -o "$podir/krunner_remindme.pot"
# Desktop metadata and the notification event file are parsed with xgettext's Desktop rules and
# joined into the same catalogue the C++ pass created.
$XGETTEXT --language=Desktop --keyword=Name --keyword=Comment --join-existing "$(dirname "$0")"/../data/plasma-runner-remindme.desktop "$(dirname "$0")"/../data/krunner-remindme.desktop "$(dirname "$0")"/../data/krunner-remindme.notifyrc -o "$podir/krunner_remindme.pot"

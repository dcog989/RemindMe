#pragma once

#include <QString>

// Manages the XDG autostart entry that relaunches the reminder helper next login. The helper
// enables the entry while it holds at least one active timer and disables it when the last timer
// is gone, so a timer that is still running when the session ends is fired on the next login while
// an idle helper is not started at every login. Mirrors KDE's PlasmaAutostart::setAutostarts()
// (startkde/plasmaautostart) by toggling the entry's "Hidden" key in the user autostart directory.
class RemindmeAutostart
{
public:
    // The directory is overridable so tests can isolate the entry; the default is the user autostart dir.
    explicit RemindmeAutostart(QString autostartDir = {});

    void setActive(bool active);

    // Formats a path as a single Exec argument per the Desktop Entry spec: double-quoted, with
    // the characters special inside quotes backslash-escaped and '%' doubled (field-code escape;
    // a literal '%' is not allowed inside a quoted argument). Public so it can be unit-tested.
    static QString execArgument(const QString &path);

private:
    QString m_autostartDir;
    QString m_entryPath;
};

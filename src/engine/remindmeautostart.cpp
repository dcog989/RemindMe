#include "remindmeautostart.h"

#include <KConfigGroup>
#include <KDesktopFile>
#include <KLocalizedString>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <utility>

namespace
{
const QString s_entryName{QStringLiteral("krunner-remindme.desktop")};
}

QString RemindmeAutostart::execArgument(const QString &path)
{
    // Desktop Entry spec quoting: wrap the argument in double quotes, backslash-escape the
    // characters that are special inside quotes, and double '%' (the field-code introducer, which
    // must be escaped even inside quotes). Single quotes carry no meaning in the spec.
    QString escaped = path;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    escaped.replace(QLatin1Char('`'), QStringLiteral("\\`"));
    escaped.replace(QLatin1Char('$'), QStringLiteral("\\$"));
    escaped.replace(QLatin1Char('%'), QStringLiteral("%%"));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

RemindmeAutostart::RemindmeAutostart(QString autostartDir)
    : m_autostartDir(std::move(autostartDir))
{
    if (m_autostartDir.isEmpty()) {
        m_autostartDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/autostart");
    }
    m_entryPath = m_autostartDir + QLatin1Char('/') + s_entryName;
}

void RemindmeAutostart::setActive(bool active)
{
    const QString exec = execArgument(QCoreApplication::applicationFilePath());
    const bool hidden = !active;

    // Do not create an entry just to hide it: a user who never had a timer does not need the
    // file, and it would show up as an unused item in System Settings.
    if (!active && !QFile::exists(m_entryPath)) {
        return;
    }

    KDesktopFile file(m_entryPath);
    // Skip rewriting only when the entry already matches everything that depends on the active
    // state or the install prefix. Comparing Hidden alone would keep a stale Exec after the
    // helper is reinstalled to another prefix.
    if (QFile::exists(m_entryPath)) {
        const KConfigGroup existing = file.desktopGroup();
        if (existing.readEntry("Hidden", false) == hidden && existing.readEntry("Exec", QString()) == exec) {
            return;
        }
    }

    if (!QDir().mkpath(m_autostartDir)) {
        qWarning() << "Could not create autostart directory" << m_autostartDir;
        return;
    }

    KConfigGroup group = file.desktopGroup();
    group.writeEntry("Type", QStringLiteral("Application"));
    group.writeEntry("Name", i18n("RemindMe Timer"));
    group.writeEntry("Comment", i18n("Start, list and cancel timers"));
    group.writeEntry("Icon", QStringLiteral("io.github.dcog989.remindme"));
    group.writeEntry("Exec", exec);
    group.writeEntry("Terminal", false);
    group.writeEntry("Hidden", hidden);
    if (!file.sync()) {
        qWarning() << "Could not write autostart entry" << m_entryPath;
    }
}

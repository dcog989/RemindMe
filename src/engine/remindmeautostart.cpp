#include "remindmeautostart.h"

#include <KConfigGroup>
#include <KDesktopFile>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

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

    QDir().mkpath(m_autostartDir);

    KConfigGroup group = file.desktopGroup();
    group.writeEntry("Type", QStringLiteral("Application"));
    group.writeEntry("Name", QStringLiteral("RemindMe Timer"));
    group.writeEntry("Exec", exec);
    group.writeEntry("Terminal", false);
    group.writeEntry("Hidden", hidden);
    file.sync();
}

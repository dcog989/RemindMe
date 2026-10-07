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

RemindmeAutostart::RemindmeAutostart(QString autostartDir)
    : m_autostartDir(std::move(autostartDir))
{
    if (m_autostartDir.isEmpty()) {
        m_autostartDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/autostart");
    }
    m_entryPath = m_autostartDir + QLatin1Char('/') + s_entryName;
}

RemindmeAutostart::~RemindmeAutostart() = default;

void RemindmeAutostart::setActive(bool active)
{
    KDesktopFile file(m_entryPath);
    if (QFile::exists(m_entryPath) && file.desktopGroup().readEntry("Hidden", false) == !active) {
        return;
    }

    QDir().mkpath(m_autostartDir);

    KConfigGroup group = file.desktopGroup();
    group.writeEntry("Type", QStringLiteral("Application"));
    group.writeEntry("Name", QStringLiteral("RemindMe Timer"));
    group.writeEntry("Exec", QStringLiteral("'%1'").arg(QCoreApplication::applicationFilePath()));
    group.writeEntry("Terminal", false);
    group.writeEntry("Hidden", !active);
    file.sync();
}

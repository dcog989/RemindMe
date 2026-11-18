#include "remindmealarmnotification.h"

#include <KLocalizedString>

namespace Remindme
{
KNotification *makeAlarmNotification(const QString &message, KNotification::NotificationFlags flags, QObject *parent)
{
    auto *notification = new KNotification(QStringLiteral("remindme"), flags, parent);
    notification->setTitle(i18n("Timer finished"));
    notification->setText(message.isEmpty() ? i18n("Your timer has finished") : message);
    return notification;
}
}

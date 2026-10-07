#pragma once

#include <KNotification>
#include <QString>

namespace Remindme
{
// Title shown for an expiry alarm, shared by the alarm window and its notification(s).
QString alarmTitle();

// Builds the KNotification shared by the dialog's looping alarm and the headless one-shot: the
// "remindme" event (see data/krunner-remindme.notifyrc), the alarm title, and message, falling
// back to a generic text when the message is empty. The caller chooses the flags (LoopSound for
// the dialog alarm, CloseOnTimeout for the headless notification) and sends it.
KNotification *makeAlarmNotification(const QString &message, KNotification::NotificationFlags flags, QObject *parent);
}

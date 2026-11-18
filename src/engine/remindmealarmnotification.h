#pragma once

#include <KNotification>
#include <QString>

namespace Remindme
{
// The KNotification event id; data/krunner-remindme.notifyrc defines [Event/<id>] and the alarm
// test asserts the two stay in sync.
inline constexpr char alarmEventId[] = "remindme";

// Builds the KNotification shared by the dialog's looping alarm and the headless one-shot: the
// "remindme" event (see data/krunner-remindme.notifyrc), the alarm title, and message, falling
// back to a generic text when the message is empty. The caller chooses the flags (LoopSound for
// the dialog alarm, CloseOnTimeout for the headless notification) and sends it.
KNotification *makeAlarmNotification(const QString &message, KNotification::NotificationFlags flags, QObject *parent);
}

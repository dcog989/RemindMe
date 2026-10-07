#include "remindmealarmsound.h"

#include <QPointer>
#include <QTimer>

#include <KLocalizedString>
#include <KNotification>

AlarmSound::AlarmSound(const QString &message, QObject *parent)
    : QObject(parent)
{
    // LoopSound makes the audio plugin repeat the sound until the notification is closed; the
    // notification is a child of this object, so closing the dialog (our parent) stops the loop.
    QPointer<KNotification> sound = new KNotification(QStringLiteral("remindme"), KNotification::LoopSound, this);
    sound->setTitle(i18n("Timer finished"));
    sound->setText(message.isEmpty() ? i18n("Your timer has finished") : message);
    sound->sendEvent();
    // Stop the looping alarm after a while, but keep the window open until dismissed.
    // KNotification auto-deletes once closed (auto-delete is on by default for C++), so the guard
    // also covers the case where it closed on its own first.
    QTimer::singleShot(Remindme::maxAlarmSoundMs, this, [sound]() {
        if (sound) {
            sound->close();
        }
    });
}

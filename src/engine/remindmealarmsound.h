#pragma once

#include <QObject>

class QString;

namespace Remindme
{
// Maximum time the looping expiry alarm sound plays; the alarm window itself stays open until the
// user snoozes or dismisses it.
constexpr qint64 maxAlarmSoundMs = 10 * 60 * 1000;
}

// Owns the looping expiry-alarm sound: starts a KNotification LoopSound and closes it once
// Remindme::maxAlarmSoundMs has elapsed. Parent it to the alarm dialog so closing the dialog stops
// the sound along with it.
class AlarmSound : public QObject
{
    Q_OBJECT

public:
    explicit AlarmSound(const QString &message, QObject *parent = nullptr);
};

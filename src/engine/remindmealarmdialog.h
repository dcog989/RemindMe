#pragma once

#include <QDialog>

#include "timerinfo.h"

namespace Remindme
{
// A timer that surfaces this far past its deadline or less is shown as having fired on time; only
// later alarms get the "overdue" line in overdueText() (remindmealarmdialog.cpp).
constexpr qint64 overdueNoticeThresholdMs = 60 * 1000;

// Upper bound of the snooze spin box, in minutes (24 h).
constexpr int maxSnoozeMinutes = 60 * 24;

// Maximum time the looping expiry alarm sound plays; the alarm window itself stays open until the
// user snoozes or dismisses it.
constexpr qint64 maxAlarmSoundMs = 10 * 60 * 1000;
}

// The expiry alarm: title "RemindMe timer finished", the timer's message, and a looping sound
// until the user snoozes or dismisses it.
class RemindmeAlarmDialog : public QDialog
{
    Q_OBJECT

public:
    // nowMs is the engine's clock, so the overdue calculation uses the same time source as the
    // rest of the engine instead of the wall clock.
    explicit RemindmeAlarmDialog(const Remindme::TimerInfo &timer, qint64 nowMs, QWidget *parent = nullptr);

Q_SIGNALS:
    // minutes is the snooze length the user picked in the spin box.
    void snoozeRequested(int minutes);
    // Emitted when "Dismiss" acknowledges the alarm, or the window is closed (X or Esc).
    void dismissRequested();
};

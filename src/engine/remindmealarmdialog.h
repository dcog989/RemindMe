#pragma once

#include <QDialog>

#include "timerinfo.h"

namespace Remindme
{
// A timer that surfaces this far past its deadline or less is shown as having fired on time; only
// later alarms get the "overdue" line in overdueText() (remindmealarmdialog.cpp).
constexpr qint64 overdueNoticeThresholdMs = 60 * 1000;

// Which alarm a dialog or presenter is handling: an expiry alarm (looping sound, "Dismiss") or a
// manual reminder for a still-active timer (no sound, "Cancel timer").
enum class AlarmKind {
    Expiry,
    Reminder,
};
}

class RemindmeAlarmDialog : public QDialog
{
    Q_OBJECT

public:
    // kind distinguishes the expiry alarm (title "Timer finished", looping sound) from a manually
    // requested reminder of a still-active timer (shown without the alarm sound). nowMs is the
    // engine's clock, so the overdue calculation uses the same time source as the rest of the
    // engine instead of the wall clock.
    explicit RemindmeAlarmDialog(const Remindme::TimerInfo &timer, Remindme::AlarmKind kind, qint64 nowMs, QWidget *parent = nullptr);

Q_SIGNALS:
    void snoozeRequested();
    // Emitted when "Dismiss" acknowledges an expired alarm or "Cancel timer" cancels a
    // still-active one. Closing the window (X) emits nothing.
    void dismissRequested();
};

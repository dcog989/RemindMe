#pragma once

#include <QDialog>

#include "remindmeengine.h"

class RemindmeAlarmDialog : public QDialog
{
    Q_OBJECT

public:
    // expired distinguishes the expiry alarm (title "Timer finished", looping sound) from a
    // manually requested reminder of a still-active timer (shown without the alarm sound).
    // nowMs is the engine's clock, so the overdue calculation uses the same time source as the
    // rest of the engine instead of the wall clock.
    explicit RemindmeAlarmDialog(const Remindme::TimerInfo &timer, bool expired, qint64 nowMs, QWidget *parent = nullptr);

Q_SIGNALS:
    void snoozeRequested();
    // Emitted when "Dismiss" acknowledges an expired alarm or "Cancel timer" cancels a
    // still-active one. Closing the window (X) emits nothing.
    void dismissRequested();
};

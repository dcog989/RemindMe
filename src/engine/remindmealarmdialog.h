#pragma once

#include <QDialog>

#include "remindmeengine.h"

class RemindmeAlarmDialog : public QDialog
{
    Q_OBJECT

public:
    // expired distinguishes the expiry alarm (title "Timer finished", looping sound) from a
    // manually requested reminder of a still-active timer (shown without the alarm sound)
    explicit RemindmeAlarmDialog(const Remindme::TimerInfo &timer, bool expired, QWidget *parent = nullptr);

Q_SIGNALS:
    void snoozeRequested();
    // Emitted when "Dismiss" acknowledges an expired alarm or "Cancel timer" cancels a
    // still-active one. Closing the window (X) emits nothing.
    void dismissRequested();
};

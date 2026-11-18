#pragma once

#include <QDialog>
#include <QList>

#include "timerinfo.h"

// One window for every timer still waiting, opened from "rme list". Each row carries the same
// snooze/cancel pair the single-timer reminder window used to, so the whole set is visible and
// actionable at once.
class ReminderListDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ReminderListDialog(const QList<Remindme::TimerInfo> &timers, QWidget *parent = nullptr);

Q_SIGNALS:
    // minutes is the snooze length the user picked for that timer.
    void snoozeRequested(const Remindme::TimerInfo &timer, int minutes);
    // Emitted when "Cancel timer" drops that timer. Closing the window keeps every timer running.
    void cancelRequested(const Remindme::TimerInfo &timer);

private:
    // withSeparator draws the divider below the row, so only the gaps between reminders get one.
    QWidget *makeRow(const Remindme::TimerInfo &timer, bool withSeparator);

    int m_rowCount = 0;
};

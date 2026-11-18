#pragma once

#include <QDialog>
#include <QList>

#include "timerinfo.h"

// One window for every timer still waiting, opened from "rme list". Read-only except for
// "Cancel timer", so the whole set is visible at once.
class ReminderListDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ReminderListDialog(const QList<Remindme::TimerInfo> &timers, QWidget *parent = nullptr);

Q_SIGNALS:
    // Emitted when "Cancel timer" drops that timer. Closing the window keeps every timer running.
    void cancelRequested(const Remindme::TimerInfo &timer);

private:
    // withSeparator draws the divider below the row, so only the gaps between reminders get one.
    QWidget *makeRow(const Remindme::TimerInfo &timer, bool withSeparator);

    int m_rowCount = 0;
};

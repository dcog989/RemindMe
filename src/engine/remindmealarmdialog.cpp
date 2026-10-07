#include "remindmealarmdialog.h"

#include <QDate>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

#include <KFormat>
#include <KLocalizedString>

#include "remindmealarmnotification.h"
#include "remindmealarmsound.h"

namespace
{
// Mirrors the KRunner list wording so the reminder body reads like the row the user picked.
QString formatDue(const QDateTime &deadline)
{
    const QDate today = QDate::currentDate();
    const QString time = QLocale().toString(deadline.time(), QLocale::ShortFormat);
    if (deadline.date() == today) {
        return i18n("today at %1", time);
    }
    if (deadline.date() == today.addDays(1)) {
        return i18n("tomorrow at %1", time);
    }
    return i18n("%1 at %2", QLocale().toString(deadline.date(), QLocale::ShortFormat), time);
}

// Text for the "overdue" line shown when an expired timer surfaces later than its deadline (e.g.
// the system was off at the time). Missing the deadline by less than the notice threshold is
// normal scheduling jitter, so an empty string is returned for a timely alarm.
QString overdueText(qint64 deadlineMs, qint64 nowMs)
{
    const qint64 overdueMs = nowMs - deadlineMs;
    if (overdueMs < Remindme::overdueNoticeThresholdMs) {
        return {};
    }
    const QDateTime scheduled = QDateTime::fromMSecsSinceEpoch(deadlineMs);
    const QDate today = QDate::currentDate();
    const QString whenDate = scheduled.date() == today ? i18n("today")
        : scheduled.date() == today.addDays(-1)        ? i18n("yesterday")
                                                       : QLocale().toString(scheduled.date(), QLocale::ShortFormat);
    const QString whenTime = QLocale().toString(scheduled.time(), QLocale::ShortFormat);
    const QString overdue = KFormat().formatDuration(overdueMs, KFormat::AbbreviatedDuration | KFormat::HideSeconds);
    return i18n("The timer was due %1 at %2.\nIt is %3 overdue.", whenDate, whenTime, overdue);
}
}

RemindmeAlarmDialog::RemindmeAlarmDialog(const Remindme::TimerInfo &timer, Remindme::AlarmKind kind, qint64 nowMs, QWidget *parent)
    : QDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);

    const bool isExpiry = kind == Remindme::AlarmKind::Expiry;
    QVBoxLayout *layout = new QVBoxLayout(this);
    if (isExpiry) {
        setWindowTitle(Remindme::alarmTitle());
        if (!timer.message.isEmpty()) {
            QLabel *messageLabel = new QLabel(timer.message, this);
            messageLabel->setWordWrap(true);
            layout->addWidget(messageLabel);
        }
        // A timer that expired while the system was off (e.g. a shutdown) surfaces later than
        // its deadline. Show when it was due and how long ago, so a late alarm is not mistaken
        // for one that fired on time.
        const QString overdue = overdueText(timer.deadline, nowMs);
        if (!overdue.isEmpty()) {
            QLabel *overdueLabel = new QLabel(overdue, this);
            overdueLabel->setWordWrap(true);
            layout->addWidget(overdueLabel);
        }
    } else {
        // A reminder for a still-active timer. The message and due time used to live only in the
        // window title; show them as body text so the dialog is actually informative.
        setWindowTitle(i18n("Reminder"));
        if (!timer.message.isEmpty()) {
            QLabel *messageLabel = new QLabel(timer.message, this);
            messageLabel->setWordWrap(true);
            layout->addWidget(messageLabel);
        }
        QLabel *dueLabel = new QLabel(i18n("Due %1.", formatDue(QDateTime::fromMSecsSinceEpoch(timer.deadline))), this);
        dueLabel->setWordWrap(true);
        layout->addWidget(dueLabel);
    }

    QHBoxLayout *buttons = new QHBoxLayout;
    const int snoozeMinutes = Remindme::snoozeDurationMs / (60 * 1000);
    QPushButton *snoozeButton = new QPushButton(i18np("Snooze +%1 minute", "Snooze +%1 minutes", snoozeMinutes), this);
    // For a still-active timer the destructive action is cancelling it, so name it accordingly
    // rather than "Dismiss" (which, for the expired alarm, only acknowledges the alarm).
    QPushButton *dismissButton = new QPushButton(isExpiry ? i18n("Dismiss") : i18n("Cancel timer"), this);
    buttons->addWidget(snoozeButton);
    buttons->addWidget(dismissButton);
    layout->addLayout(buttons);

    connect(snoozeButton, &QPushButton::clicked, this, &RemindmeAlarmDialog::snoozeRequested);
    connect(snoozeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(dismissButton, &QPushButton::clicked, this, &QDialog::accept);
    // "Cancel timer" (still-active) cancels the timer; "Dismiss" (expired) acknowledges the
    // alarm. Both must clear engine state, otherwise a dismissed alarm would be replayed on the
    // next start. Closing with the window manager bypasses this and is deliberately a no-op.
    connect(dismissButton, &QPushButton::clicked, this, &RemindmeAlarmDialog::dismissRequested);

    // The expiry alarm loops the "remindme" sound until the dialog closes; AlarmSound is parented
    // to the dialog, so dismissing or snoozing (which destroys it) stops the sound.
    if (isExpiry) {
        new AlarmSound(timer.message, this);
    }
}

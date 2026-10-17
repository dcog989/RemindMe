#include "remindmealarmdialog.h"

#include <QDate>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <KFormat>
#include <KLocalizedString>

#include "remindmealarmnotification.h"
#include "remindmealarmsound.h"
#include "remindmeengine.h"
#include "remindmetime.h"

namespace
{
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
    auto *layout = new QVBoxLayout(this);
    if (!timer.message.isEmpty()) {
        auto *messageLabel = new QLabel(timer.message, this);
        messageLabel->setWordWrap(true);
        layout->addWidget(messageLabel);
    }
    if (isExpiry) {
        setWindowTitle(Remindme::alarmTitle());
        // A timer that expired while the system was off (e.g. a shutdown) surfaces later than
        // its deadline. Show when it was due and how long ago, so a late alarm is not mistaken
        // for one that fired on time.
        const QString overdue = overdueText(timer.deadline, nowMs);
        if (!overdue.isEmpty()) {
            auto *overdueLabel = new QLabel(overdue, this);
            overdueLabel->setWordWrap(true);
            layout->addWidget(overdueLabel);
        }
    } else {
        // A reminder for a still-active timer. The message and due time used to live only in the
        // window title; show them as body text so the dialog is actually informative.
        setWindowTitle(i18n("Reminder"));
        auto *dueLabel = new QLabel(i18n("Due %1.", Remindme::formatReminderTime(QDateTime::fromMSecsSinceEpoch(timer.deadline), false)), this);
        dueLabel->setWordWrap(true);
        layout->addWidget(dueLabel);
    }

    auto *buttons = new QHBoxLayout;
    auto *snoozeSpin = new QSpinBox(this);
    snoozeSpin->setRange(1, Remindme::maxSnoozeMinutes);
    snoozeSpin->setValue(Remindme::snoozeDurationMs / (60 * 1000));
    snoozeSpin->setSuffix(i18n(" min"));
    auto *snoozeButton = new QPushButton(i18n("Snooze"), this);
    // For a still-active timer the destructive action is cancelling it, so name it accordingly
    // rather than "Dismiss" (which, for the expired alarm, only acknowledges the alarm).
    auto *dismissButton = new QPushButton(isExpiry ? i18n("Dismiss") : i18n("Cancel timer"), this);
    buttons->addWidget(snoozeSpin);
    buttons->addWidget(snoozeButton);
    buttons->addWidget(dismissButton);
    layout->addLayout(buttons);

    connect(snoozeButton, &QPushButton::clicked, this, [this, snoozeSpin]() {
        Q_EMIT snoozeRequested(snoozeSpin->value());
    });
    connect(snoozeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(dismissButton, &QPushButton::clicked, this, &QDialog::accept);
    // "Cancel timer" (still-active) cancels the timer; "Dismiss" (expired) acknowledges the
    // alarm. Both must clear engine state, otherwise a dismissed alarm would be replayed on the
    // next start.
    connect(dismissButton, &QPushButton::clicked, this, &RemindmeAlarmDialog::dismissRequested);
    // Closing an expired alarm (window manager or Esc) acknowledges it too; otherwise the alarm
    // would be left pending while the process exits, and only shown again on the next activation.
    if (isExpiry) {
        connect(this, &QDialog::rejected, this, &RemindmeAlarmDialog::dismissRequested);
    }

    // The expiry alarm loops the "remindme" sound until the dialog closes; AlarmSound is parented
    // to the dialog, so dismissing or snoozing (which destroys it) stops the sound.
    if (isExpiry) {
        new AlarmSound(timer.message, this);
    }
}

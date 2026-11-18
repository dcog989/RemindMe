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

RemindmeAlarmDialog::RemindmeAlarmDialog(const Remindme::TimerInfo &timer, qint64 nowMs, QWidget *parent)
    : QDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);

    setMinimumHeight(Remindme::minWindowHeight);
    setWindowTitle(Remindme::alarmTitle());
    auto *layout = new QVBoxLayout(this);
    if (!timer.message.isEmpty()) {
        auto *messageLabel = new QLabel(timer.message, this);
        messageLabel->setWordWrap(true);
        layout->addWidget(messageLabel);
    }
    // A timer that expired while the system was off (e.g. a shutdown) surfaces later than its
    // deadline. Show when it was due and how long ago, so a late alarm is not mistaken for one
    // that fired on time.
    const QString overdue = overdueText(timer.deadline, nowMs);
    if (!overdue.isEmpty()) {
        auto *overdueLabel = new QLabel(overdue, this);
        overdueLabel->setWordWrap(true);
        layout->addWidget(overdueLabel);
    }

    auto *buttons = new QHBoxLayout;
    auto *snoozeSpin = new QSpinBox(this);
    snoozeSpin->setRange(1, Remindme::maxSnoozeMinutes);
    snoozeSpin->setValue(Remindme::snoozeDurationMs / (60 * 1000));
    snoozeSpin->setSuffix(i18n(" min"));
    auto *snoozeButton = new QPushButton(i18n("Snooze"), this);
    auto *dismissButton = new QPushButton(i18n("Dismiss"), this);
    buttons->addWidget(snoozeSpin);
    buttons->addWidget(snoozeButton);
    buttons->addWidget(dismissButton);
    layout->addLayout(buttons);

    connect(snoozeButton, &QPushButton::clicked, this, [this, snoozeSpin]() {
        Q_EMIT snoozeRequested(snoozeSpin->value());
    });
    connect(snoozeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(dismissButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(dismissButton, &QPushButton::clicked, this, &RemindmeAlarmDialog::dismissRequested);
    // Closing the alarm (window manager or Esc) acknowledges it too; otherwise the alarm would be
    // left pending while the process exits, and only shown again on the next activation.
    connect(this, &QDialog::rejected, this, &RemindmeAlarmDialog::dismissRequested);

    // The expiry alarm loops the "remindme" sound until the dialog closes; AlarmSound is parented
    // to the dialog, so dismissing or snoozing (which destroys it) stops the sound.
    new AlarmSound(timer.message, this);
}

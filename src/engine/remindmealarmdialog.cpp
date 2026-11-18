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
// Same width floor as the reminder overview, so the two windows feel like one family.
constexpr int minWidth = 480;

// Ceiling for the width: a long message wraps inside the window instead of widening it across
// the screen.
constexpr int maxWidth = 640;

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

    setWindowTitle(i18n("RemindMe timer finished"));
    auto *layout = new QHBoxLayout(this);

    // Message and overdue line on the left, the controls at the right: the same row shape as a
    // reminder overview row (remindmereminderlistdialog.cpp).
    const QString overdue = overdueText(timer.deadline, nowMs);
    // A timer without a message has no text column at all, so the controls are not pushed apart
    // by a stretch of empty window.
    const bool hasText = !timer.message.isEmpty() || !overdue.isEmpty();
    if (hasText) {
        auto *text = new QWidget(this);
        auto *textLayout = new QVBoxLayout(text);
        textLayout->setContentsMargins(0, 0, 0, 0);
        if (!timer.message.isEmpty()) {
            auto *messageLabel = new QLabel(timer.message, this);
            messageLabel->setTextFormat(Qt::PlainText);
            messageLabel->setWordWrap(true);
            textLayout->addWidget(messageLabel);
        }
        // A timer that expired while the system was off (e.g. a shutdown) surfaces later than its
        // deadline. Show when it was due and how long ago, so a late alarm is not mistaken for one
        // that fired on time.
        if (!overdue.isEmpty()) {
            auto *overdueLabel = new QLabel(overdue, this);
            overdueLabel->setWordWrap(true);
            textLayout->addWidget(overdueLabel);
        }
        textLayout->addStretch();
        layout->addWidget(text, 1);
    }

    auto *snoozeSpin = new QSpinBox(this);
    snoozeSpin->setRange(1, Remindme::maxSnoozeMinutes);
    snoozeSpin->setValue(Remindme::snoozeDurationMs / (60 * 1000));
    snoozeSpin->setSuffix(i18n(" min"));
    auto *snoozeButton = new QPushButton(i18n("Snooze"), this);
    auto *dismissButton = new QPushButton(i18n("Dismiss"), this);
    layout->addWidget(snoozeSpin);
    layout->addWidget(snoozeButton);
    layout->addWidget(dismissButton);

    // Fixed to its content, like the overview: a fixed-size window cannot be dragged into KWin's
    // 150 px floor and never opens with empty space. The height follows the wrapped text. The
    // width floor only applies when there is text to fill it; a controls-only alarm opens narrow.
    layout->activate();
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint);
    const int contentWidth = qMin(qMax(layout->sizeHint().width(), hasText ? minWidth : 0), maxWidth);
    setFixedSize(contentWidth, layout->hasHeightForWidth() ? layout->heightForWidth(contentWidth) : layout->sizeHint().height());

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

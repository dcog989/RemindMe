#include "remindmealarmdialog.h"

#include <QDate>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <KFormat>
#include <KLocalizedString>
#include <KNotification>

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
}

RemindmeAlarmDialog::RemindmeAlarmDialog(const Remindme::TimerInfo &timer, bool expired, QWidget *parent)
    : QDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);

    QVBoxLayout *layout = new QVBoxLayout(this);
    if (expired) {
        setWindowTitle(i18n("Timer finished"));
        if (!timer.message.isEmpty()) {
            QLabel *messageLabel = new QLabel(timer.message, this);
            messageLabel->setWordWrap(true);
            layout->addWidget(messageLabel);
        }
        // A timer that expired while the system was off (e.g. a shutdown) surfaces later than
        // its deadline. Show when it was due and how long ago, so a late alarm is not mistaken
        // for one that fired on time. Missing the deadline by less than a minute is normal
        // scheduling jitter and does not warrant the "overdue" line.
        const qint64 overdueMs = QDateTime::currentMSecsSinceEpoch() - timer.deadline;
        if (overdueMs >= Remindme::overdueNoticeThresholdMs) {
            const QDateTime scheduled = QDateTime::fromMSecsSinceEpoch(timer.deadline);
            const QDate today = QDate::currentDate();
            const QString whenDate = scheduled.date() == today ? i18n("today")
                : scheduled.date() == today.addDays(-1)        ? i18n("yesterday")
                                                               : QLocale().toString(scheduled.date(), QLocale::ShortFormat);
            const QString whenTime = QLocale().toString(scheduled.time(), QLocale::ShortFormat);
            const QString overdue = KFormat().formatDuration(overdueMs, KFormat::AbbreviatedDuration | KFormat::HideSeconds);
            QLabel *overdueLabel = new QLabel(i18n("The timer was due %1 at %2.\nIt is %3 overdue.", whenDate, whenTime, overdue), this);
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
    QPushButton *snoozeButton = new QPushButton(i18n("Snooze +1 minute"), this);
    // For a still-active timer the destructive action is cancelling it, so name it accordingly
    // rather than "Dismiss" (which, for the expired alarm, only acknowledges the alarm).
    QPushButton *dismissButton = new QPushButton(expired ? i18n("Dismiss") : i18n("Cancel timer"), this);
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

    // The "remindme" event is Sound-only. LoopSound makes the audio plugin repeat the sound
    // until the notification is closed; it is a child of the dialog, so dismissing or
    // snoozing (which closes the dialog) stops the looping.
    if (expired) {
        KNotification *sound = new KNotification(QStringLiteral("remindme"), KNotification::LoopSound, this);
        sound->setTitle(i18n("Timer finished"));
        sound->setText(timer.message.isEmpty() ? i18n("Your timer has finished") : timer.message);
        sound->sendEvent();
        // Stop the looping alarm after a while, but keep the window open until dismissed.
        // The dialog is the timer's context object, so if the user dismisses or snoozes
        // (destroying the dialog) the timer is cancelled and never touches the deleted
        // notification.
        QTimer::singleShot(Remindme::maxAlarmSoundMs, this, [sound]() {
            sound->close();
        });
    }
}

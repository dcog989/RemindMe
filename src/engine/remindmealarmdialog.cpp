#include "remindmealarmdialog.h"

#include <QDate>
#include <QDateTime>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <KFormat>
#include <KLocalizedString>

#include "remindmealarmnotification.h"
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

    setWindowTitle(i18n("RemindMe timer finished"));
    auto *layout = new QVBoxLayout(this);

    // Message and overdue line on top, the controls in a row below, so a long description never
    // squeezes the snooze/dismiss row.
    const QString overdue = overdueText(timer.deadline, nowMs);
    // A timer without a message has no text column at all, so the controls are not pushed apart
    // by a stretch of empty window.
    const bool hasText = !timer.message.isEmpty() || !overdue.isEmpty();
    // Needed by the sizing below: a scroll area reports a fixed default hint, not the text's size.
    QWidget *text = nullptr;
    QScrollArea *scroll = nullptr;
    if (hasText) {
        text = new QWidget(this);
        auto *textLayout = new QVBoxLayout(text);
        textLayout->setContentsMargins(0, 0, 0, 0);
        if (!timer.message.isEmpty()) {
            auto *messageLabel = new QLabel(timer.message, this);
            messageLabel->setTextFormat(Qt::PlainText);
            messageLabel->setWordWrap(true);
            // A single unbroken word cannot wrap, so the fixed width clips it; the tooltip is the
            // only place the whole message stays readable. Escaping alone does not make the
            // tooltip rich text, so a plain "&" would show as "&amp;"; the <p> wrapper does.
            messageLabel->setToolTip(QStringLiteral("<p>%1</p>").arg(timer.message.toHtmlEscaped()));
            textLayout->addWidget(messageLabel);
        }
        // A timer that expired while the system was off (e.g. a shutdown) surfaces later than its
        // deadline. Show when it was due and how long ago, so a late alarm is not mistaken for one
        // that fired on time.
        if (!overdue.isEmpty()) {
            auto *overdueLabel = new QLabel(overdue, this);
            overdueLabel->setTextFormat(Qt::PlainText);
            overdueLabel->setWordWrap(true);
            textLayout->addWidget(overdueLabel);
        }
        textLayout->addStretch();

        scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setWidget(text);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        if (const QScreen *screen = QGuiApplication::primaryScreen()) {
            scroll->setMaximumHeight(screen->availableGeometry().height() / Remindme::dialogMaxScreenHeightShare);
        }
        layout->addWidget(scroll);
    }

    auto *controls = new QHBoxLayout;
    auto *snoozeSpin = new QSpinBox(this);
    snoozeSpin->setRange(1, Remindme::maxSnoozeMinutes);
    snoozeSpin->setValue(Remindme::snoozeDurationMs / (60 * 1000));
    snoozeSpin->setSuffix(i18n(" min"));
    auto *snoozeButton = new QPushButton(i18n("Snooze"), this);
    auto *dismissButton = new QPushButton(i18n("Dismiss"), this);
    controls->addStretch(1);
    controls->addWidget(snoozeSpin);
    controls->addWidget(snoozeButton);
    controls->addWidget(dismissButton);
    layout->addLayout(controls);

    // Fixed to its content, like the overview: a fixed-size window cannot be dragged into KWin's
    // 150 px floor and never opens with empty space. The width is whatever the widest row needs
    // (the snooze/dismiss row is always present) and the height follows the wrapped text, capped by
    // the scroll area above.
    layout->activate();
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint);
    const int contentWidth = qMin(layout->sizeHint().width(), Remindme::dialogMaxWidth);
    if (scroll) {
        // The scroll area's own hint is a fixed default (several times taller than one line), so
        // the window would open with empty space under a short message. Size it to the wrapped
        // text instead, keeping the screen-height cap: a longer message then scrolls inside it.
        const int textWidth =
            contentWidth - layout->contentsMargins().left() - layout->contentsMargins().right() - scroll->verticalScrollBar()->sizeHint().width();
        const int wrappedHeight = text->heightForWidth(textWidth);
        if (wrappedHeight > 0) {
            scroll->setMaximumHeight(qMin(wrappedHeight, scroll->maximumHeight()));
        }
        layout->invalidate();
    }
    setFixedSize(contentWidth, layout->sizeHint().height());

    connect(snoozeButton, &QPushButton::clicked, this, [this, snoozeSpin]() {
        Q_EMIT snoozeRequested(snoozeSpin->value());
    });
    connect(snoozeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(dismissButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(dismissButton, &QPushButton::clicked, this, &RemindmeAlarmDialog::dismissRequested);
    // Closing the alarm (window manager or Esc) acknowledges it too; otherwise the alarm would be
    // left pending while the process exits, and only shown again on the next activation.
    connect(this, &QDialog::rejected, this, &RemindmeAlarmDialog::dismissRequested);

    // The expiry alarm loops the "remindme" sound until the dialog closes; the notification is
    // parented here, so snoozing or dismissing (which destroys the dialog) stops the sound, and the
    // guard stops a long loop while the window stays open.
    QPointer<KNotification> sound = Remindme::makeAlarmNotification(timer.message, KNotification::LoopSound, this);
    sound->sendEvent();
    QTimer::singleShot(Remindme::maxAlarmSoundMs, this, [sound]() {
        if (sound) {
            sound->close();
        }
    });
}

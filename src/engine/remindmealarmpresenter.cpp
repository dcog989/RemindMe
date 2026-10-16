#include "remindmealarmpresenter.h"

#include <QGuiApplication>

#include <KNotification>

#include "remindmealarmdialog.h"
#include "remindmealarmnotification.h"

AlarmPresenter::AlarmPresenter(RemindmeEngine &engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
{
}

void AlarmPresenter::present(const Remindme::TimerInfo &timer, Remindme::AlarmKind kind)
{
    // Headless contexts (e.g. CI, no desktop) cannot show a window. An expired timer was already
    // removed by the engine, so the alarm degrades to a single sound notification; a manually
    // requested reminder is simply a no-op.
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
        if (kind == Remindme::AlarmKind::Expiry) {
            presentSound(timer);
        }
        return;
    }
    presentDialog(timer, kind);
}

bool AlarmPresenter::isPresenting() const
{
    return m_openDialogCount > 0 || m_pendingSoundCount > 0;
}

void AlarmPresenter::presentDialog(const Remindme::TimerInfo &timer, Remindme::AlarmKind kind)
{
    // The dialog shows the message and, for the expiry alarm, plays a looping alarm sound
    // (see RemindmeAlarmDialog) until the user snoozes or dismisses it.
    auto *dialog = new RemindmeAlarmDialog(timer, kind, m_engine.nowMs());
    const QPointer<RemindmeAlarmDialog> previous = m_dialogs.value(timer.id);
    m_dialogs.insert(timer.id, dialog);
    // Count before closing the previous window: its later destruction decrements the count again,
    // so the presenter never reports "nothing on screen" while this timer still has a window.
    ++m_openDialogCount;
    connect(dialog, &RemindmeAlarmDialog::snoozeRequested, this, [this, timer]() {
        m_engine.snooze(timer);
    });
    // "Cancel timer" (still-active) or "Dismiss" (expired) clears the engine entry; cancel handles
    // both. The dialog also emits this when an expiry window is closed (X or Esc); a closed
    // reminder window emits nothing and leaves its timer running.
    connect(dialog, &RemindmeAlarmDialog::dismissRequested, this, [this, timer]() {
        m_engine.cancel(timer.id);
    });
    // The window is WA_DeleteOnClose, so closing it may be the last thing holding the process up
    // (e.g. a cancel-by-dismissal leaves no timers); let the host re-check idleness.
    connect(dialog, &QObject::destroyed, this, &AlarmPresenter::onDialogClosed);
    dialog->show();
    // This process runs in the background, so the dialog could otherwise map behind the active
    // window (especially on Wayland); an alarm you cannot see defeats the purpose.
    dialog->raise();
    dialog->activateWindow();

    // A reminder opened from "rme list" can be superseded by the expiry alarm for the same timer;
    // close the earlier window so one timer never has two windows at once. Detach first, so this
    // programmatic close is not taken for the user acknowledging the replacement.
    if (previous) {
        disconnect(previous.data(), &RemindmeAlarmDialog::dismissRequested, this, nullptr);
        previous->close();
    }
}

void AlarmPresenter::presentSound(const Remindme::TimerInfo &timer)
{
    KNotification *sound = Remindme::makeAlarmNotification(timer.message, KNotification::CloseOnTimeout, this);
    // Count the sound before sending: sendEvent() is asynchronous, and the engine's count signal
    // that reaches the quit check runs first.
    ++m_pendingSoundCount;
    // KNotification auto-deletes on close, so closed() is the last chance to react.
    connect(sound, &KNotification::closed, this, &AlarmPresenter::onSoundClosed);
    sound->sendEvent();
}

void AlarmPresenter::onDialogClosed()
{
    --m_openDialogCount;
    if (!isPresenting()) {
        Q_EMIT presentationsFinished();
    }
}

void AlarmPresenter::onSoundClosed()
{
    --m_pendingSoundCount;
    if (!isPresenting()) {
        Q_EMIT presentationsFinished();
    }
}

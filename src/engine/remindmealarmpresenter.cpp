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

void AlarmPresenter::present(const Remindme::TimerInfo &timer, bool expired)
{
    // Headless contexts (e.g. CI, no desktop) cannot show a window. An expired timer was already
    // removed by the engine, so the alarm degrades to a single sound notification; a manually
    // requested reminder is simply a no-op.
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
        if (expired) {
            presentSound(timer);
        }
        return;
    }
    presentDialog(timer, expired);
}

bool AlarmPresenter::isPresenting() const
{
    return m_openDialogCount > 0 || m_pendingSoundCount > 0;
}

void AlarmPresenter::presentDialog(const Remindme::TimerInfo &timer, bool expired)
{
    // The dialog shows the message and, for the expiry alarm, plays a looping alarm sound
    // (see RemindmeAlarmDialog) until the user snoozes or dismisses it.
    auto *dialog = new RemindmeAlarmDialog(timer, expired, m_engine.nowMs());
    ++m_openDialogCount;
    connect(dialog, &RemindmeAlarmDialog::snoozeRequested, this, [this, timer, expired]() {
        if (expired) {
            m_engine.rearmExpiredTimer(timer);
        } else {
            m_engine.Snooze(timer.id);
        }
    });
    // "Cancel timer" (still-active) or "Dismiss" (expired) clears the engine entry; Cancel handles
    // both. Closing the window without a button is a no-op.
    connect(dialog, &RemindmeAlarmDialog::dismissRequested, this, [this, timer]() {
        m_engine.Cancel(timer.id);
    });
    // The window is WA_DeleteOnClose, so closing it may be the last thing holding the process up
    // (e.g. a cancel-by-dismissal leaves no timers); let the host re-check idleness.
    connect(dialog, &QObject::destroyed, this, &AlarmPresenter::onDialogClosed);
    dialog->show();
    // This process runs in the background, so the dialog could otherwise map behind the active
    // window (especially on Wayland); an alarm you cannot see defeats the purpose.
    dialog->raise();
    dialog->activateWindow();
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

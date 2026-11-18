#include "remindmealarmpresenter.h"

#include <QGuiApplication>

#include <KNotification>

#include "remindmealarmdialog.h"
#include "remindmealarmnotification.h"
#include "remindmereminderlistdialog.h"

AlarmPresenter::AlarmPresenter(RemindmeEngine &engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
{
    // A cancellation from KRunner ("rme cancel ...") acknowledges the alarm, so its window must not
    // keep ringing for a timer the engine no longer has.
    connect(&m_engine, &RemindmeEngine::timerRemoved, this, [this](qint32 id) {
        if (RemindmeAlarmDialog *dialog = m_dialogs.value(id)) {
            dialog->close();
        }
    });
}

void AlarmPresenter::presentExpiry(const Remindme::TimerInfo &timer)
{
    // Headless contexts (e.g. CI, no desktop) cannot show a window. An expired timer was already
    // removed by the engine, so the alarm degrades to a single sound notification.
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
        presentSound(timer);
        return;
    }
    presentDialog(timer);
}

void AlarmPresenter::presentReminders(const QList<Remindme::TimerInfo> &timers)
{
    // A reminder for still-running timers has no headless form: there is nothing to ring.
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
        return;
    }

    // One window lists every waiting timer; a new request replaces it so it never shows a stale set.
    auto *dialog = new ReminderListDialog(timers);
    const QPointer<ReminderListDialog> previous = m_reminders;
    m_reminders = dialog;
    // Count before closing the previous window: its later destruction decrements the count again,
    // so the presenter never reports "nothing on screen" while this window is still open.
    ++m_openDialogCount;
    connect(dialog, &ReminderListDialog::cancelRequested, this, [this](const Remindme::TimerInfo &timer) {
        m_engine.cancel(timer.id);
    });
    connect(dialog, &QObject::destroyed, this, [this]() {
        --m_openDialogCount;
        emitFinishedIfIdle();
    });
    dialog->show();
    // This process runs in the background, so the window could otherwise map behind the active
    // window (especially on Wayland).
    dialog->raise();
    dialog->activateWindow();
    if (previous) {
        previous->close();
    }
}

bool AlarmPresenter::isPresenting() const
{
    return m_openDialogCount > 0 || m_pendingSoundCount > 0;
}

void AlarmPresenter::presentDialog(const Remindme::TimerInfo &timer)
{
    // The overview lists still-active timers, so an expiry makes its rows stale; close it and let
    // the user reopen the list rather than act on a timer that already fired.
    // shortcut: the overview is not refreshed for timer changes made elsewhere, close it instead;
    // refresh it live if that becomes annoying.
    if (m_reminders) {
        m_reminders->close();
    }

    // The dialog shows the message and loops the alarm sound until the user snoozes or dismisses
    // it (see RemindmeAlarmDialog).
    auto *dialog = new RemindmeAlarmDialog(timer, m_engine.nowMs());
    const QPointer<RemindmeAlarmDialog> previous = m_dialogs.value(timer.id);
    m_dialogs.insert(timer.id, dialog);
    // Count before closing the previous window: its later destruction decrements the count again,
    // so the presenter never reports "nothing on screen" while this timer still has a window.
    ++m_openDialogCount;
    connect(dialog, &RemindmeAlarmDialog::snoozeRequested, this, [this, timer](int minutes) {
        m_engine.snooze(timer, static_cast<qint64>(minutes) * 60 * 1000);
    });
    // Dismiss clears the engine entry, which is how a fired alarm is acknowledged; the dialog also
    // emits this when an alarm window is closed (X or Esc).
    connect(dialog, &RemindmeAlarmDialog::dismissRequested, this, [this, timer]() {
        m_engine.cancel(timer.id);
    });
    // The window is WA_DeleteOnClose, so closing it may be the last thing holding the process up
    // (e.g. a cancel-by-dismissal leaves no timers); let the host re-check idleness. Drop the
    // tracking entry here too, or the hash grows one stale key per alarm shown. A replacement
    // window is already stored under the same id by the time the old one dies, so only remove the
    // entry when the tracked window is gone.
    connect(dialog, &QObject::destroyed, this, [this, id = timer.id]() {
        if (m_dialogs.value(id).isNull()) {
            m_dialogs.remove(id);
        }
        --m_openDialogCount;
        emitFinishedIfIdle();
    });
    dialog->show();
    // This process runs in the background, so the dialog could otherwise map behind the active
    // window (especially on Wayland); an alarm you cannot see defeats the purpose.
    dialog->raise();
    dialog->activateWindow();

    // A re-presented alarm for the same timer replaces its window instead of stacking a duplicate.
    // Hide it before deleting: close() would emit rejected, which dismisses the timer this window
    // presents.
    if (previous) {
        previous->hide();
        previous->deleteLater();
    }
}

void AlarmPresenter::presentSound(const Remindme::TimerInfo &timer)
{
    KNotification *sound = Remindme::makeAlarmNotification(timer.message, KNotification::CloseOnTimeout, this);
    // Count the sound before sending: sendEvent() is asynchronous, and the engine's count signal
    // that reaches the quit check runs first.
    ++m_pendingSoundCount;
    // A one-shot notification is the headless form of the alarm window, so closing it acknowledges
    // the fired alarm; otherwise the entry stays pending and replays on every activation.
    connect(sound, &KNotification::closed, this, [this, id = timer.id]() {
        m_engine.cancel(id);
        --m_pendingSoundCount;
        emitFinishedIfIdle();
    });
    // The presenter outlives every notification, so a closed one would otherwise stay its child
    // (and keep its message) for the rest of the process.
    connect(sound, &KNotification::closed, sound, &QObject::deleteLater);
    sound->sendEvent();
}

void AlarmPresenter::emitFinishedIfIdle()
{
    if (!isPresenting()) {
        Q_EMIT presentationsFinished();
    }
}

#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>

#include "remindmealarmdialog.h"
#include "remindmeengine.h"

class ReminderListDialog;

// Presents expiry alarms and the reminder overview, owning the dialog/sound lifetime so the host
// only has to ask whether anything is still on screen. Headless contexts fall back to a one-shot
// sound notification for an alarm; the reminder overview has no headless form.
class AlarmPresenter : public QObject
{
    Q_OBJECT

public:
    explicit AlarmPresenter(RemindmeEngine &engine, QObject *parent = nullptr);

    // Presents timer's expiry alarm: a looping-sound alarm window, or a sound notification when
    // there is no display.
    void presentExpiry(const Remindme::TimerInfo &timer);
    // Presents every still-active timer in one reminder window.
    void presentReminders(const QList<Remindme::TimerInfo> &timers);

    // Whether a dialog or an in-flight sound notification is still active. The host must not exit
    // while this is true, or it would tear down the process over an alarm the user is acting on.
    bool isPresenting() const;

Q_SIGNALS:
    // Emitted when the last presentation goes away; the host re-checks whether it may exit.
    void presentationsFinished();

private:
    void presentDialog(const Remindme::TimerInfo &timer);
    void presentSound(const Remindme::TimerInfo &timer);
    void onDialogClosed();
    void onSoundClosed();

    RemindmeEngine &m_engine;
    // Open alarm windows keyed by timer id, so a second presentation for the same timer replaces
    // its window instead of stacking a duplicate. QPointer nulls itself when the WA_DeleteOnClose
    // dialog is destroyed.
    QHash<int, QPointer<RemindmeAlarmDialog>> m_dialogs;
    // The reminder overview, replaced rather than stacked when reminders are requested again.
    QPointer<ReminderListDialog> m_reminders;
    // An expired timer was already moved to the engine's fired set, so the open window alone keeps
    // the process alive until the user snoozes or dismisses it.
    int m_openDialogCount = 0;
    // Headless sound notifications still in flight: without holding the count the process could
    // quit and tear down the bus before the asynchronous notification is delivered.
    int m_pendingSoundCount = 0;
};

#pragma once

#include <QObject>

#include "remindmeengine.h"

// Presents expiry alarms and manual reminders, owning the dialog/sound lifetime so the host only
// has to ask whether anything is still on screen. Headless contexts fall back to a one-shot sound
// notification; a manually requested reminder has no meaningful headless form and is a no-op.
class AlarmPresenter : public QObject
{
    Q_OBJECT

public:
    explicit AlarmPresenter(RemindmeEngine &engine, QObject *parent = nullptr);

    // Presents timer: an expiry alarm (looping sound) when expired, otherwise a reminder for a
    // still-active timer (shown without the alarm sound). Tracked until dismissed or snoozed.
    void present(const Remindme::TimerInfo &timer, bool expired);

    // Whether a dialog or an in-flight sound notification is still active. The host must not exit
    // while this is true, or it would tear down the process over an alarm the user is acting on.
    bool isPresenting() const;

Q_SIGNALS:
    // Emitted when the last presentation goes away; the host re-checks whether it may exit.
    void presentationsFinished();

private:
    void presentDialog(const Remindme::TimerInfo &timer, bool expired);
    void presentSound(const Remindme::TimerInfo &timer);
    void onDialogClosed();
    void onSoundClosed();

    RemindmeEngine &m_engine;
    // An expired timer was already moved to the engine's fired set, so the open window alone keeps
    // the process alive until the user snoozes or dismisses it.
    int m_openDialogCount = 0;
    // Headless sound notifications still in flight: without holding the count the process could
    // quit and tear down the bus before the asynchronous notification is delivered.
    int m_pendingSoundCount = 0;
};

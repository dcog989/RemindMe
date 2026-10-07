#pragma once

#include <QHash>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>

namespace Remindme
{
struct TimerInfo {
    int id = -1;
    qint64 deadline = 0;
    QString message;
};

// How long a snooze extends a timer by. This is also the duration named in the
// "Snooze +1 minute" button/action labels in remindmealarmdialog.cpp; keep that label in sync
// if this value ever changes.
constexpr qint64 snoozeDurationMs = 60 * 1000;

// A timer that surfaces this far past its deadline or less is shown as having fired on time;
// only alarms later than this get the "due X ago" line in remindmealarmdialog.cpp.
constexpr qint64 overdueNoticeThresholdMs = 60 * 1000;

// Maximum time the looping expiry alarm sound plays; the alarm window itself stays open
// until the user snoozes or dismisses it.
constexpr qint64 maxAlarmSoundMs = 10 * 60 * 1000;
}

Q_DECLARE_METATYPE(Remindme::TimerInfo)

class QSocketNotifier;

class RemindmeEngine : public QObject
{
    Q_OBJECT

public:
    explicit RemindmeEngine(QObject *parent = nullptr, const QString &stateFilePath = {}, std::function<qint64()> clock = {});
    ~RemindmeEngine() override;

    // Re-arms a timer that has already expired (and so moved to m_firedAlarms). The
    // Snooze(id) method handles the still-active case; both funnel into snoozeTimer().
    void rearmExpiredTimer(const Remindme::TimerInfo &timer);

    // Snoozes a still-active timer. Not part of the D-Bus interface (no external consumer);
    // used by Show()'s notification action and the engine unit tests.
    bool Snooze(qint32 id);

    // Moves timers expired as of nowMs() to the fired set and emits timerExpired() for each.
    // Called by the expiry timer(s); public so unit tests can drive expiry deterministically
    // without real-time waits.
    void checkExpiredTimers();

    // Whether an expired alarm is still awaiting Snooze/Dismiss. Fired alarms are persisted
    // across restarts, so the host can tell it has work to resume (and keep autostart on).
    bool hasPendingAlarms() const;
    // Re-emits timerExpired() for every fired alarm loaded from disk. The host calls this once
    // after wiring the signal so alarms that fired before a restart are shown again.
    void replayPendingAlarms();

    Remindme::TimerInfo Create(qint32 durationSeconds, const QString &message);
    QList<Remindme::TimerInfo> List();
    bool Cancel(qint32 id);
    bool CancelByMessage(const QString &message);
    void Show(qint32 id);

Q_SIGNALS:
    void timerExpired(const Remindme::TimerInfo &timer);
    // Emitted when a reminder is requested for a still-active timer (e.g. from "rme list")
    void showReminderRequested(const Remindme::TimerInfo &timer);
    // Emitted whenever the set of timers changes (create, cancel, expiry, snooze), with the new
    // count of active plus fired alarms; lets the host process know when the engine is out of work.
    void timerCountChanged(int timerCount);

private:
    void loadFromDisk();
    void saveToDisk() const;
    void scheduleExpiryCheck();
    qint64 nowMs() const;
    // Persists m_timers and m_firedAlarms and re-arms the expiry timer. Call after any mutation.
    void commitTimers();
    // Extends deadline by Remindme::snoozeDurationMs, anchored to now if it has already passed
    // (so snoozing an expired timer doesn't leave it expired again immediately).
    qint64 extendedDeadline(qint64 deadline) const;
    // Extends timer's deadline and (re-)inserts it into m_timers. The same operation whether
    // snoozing a still-active timer (Snooze(id) keeps the time that was left) or re-arming one
    // that already expired and was moved to m_firedAlarms (rearmExpiredTimer()).
    void snoozeTimer(Remindme::TimerInfo timer);

    QHash<int, Remindme::TimerInfo> m_timers;
    // Expired but not yet acknowledged (Snooze/Dismiss). An id lives in exactly one of
    // m_timers/m_firedAlarms; this set is persisted so a session end does not lose the alarm.
    QHash<int, Remindme::TimerInfo> m_firedAlarms;
    QTimer m_expiryTimer;
    // Suspend fallback where the timerfd is unavailable (non-Linux, injected test clock): a
    // monotonic QTimer pauses during suspend, so sweep the wall clock to bound wake lag.
    QTimer m_sweepTimer;
    // Linux, real clock: a realtime-absolute timerfd fixes expiry to the wall clock, so a deadline
    // crossed during suspend fires on resume. When set, m_expiryTimer/m_sweepTimer are unused.
    bool m_useTimerfd = false;
    int m_deadlineFd = -1;
    QSocketNotifier *m_deadlineNotifier = nullptr;
    QString m_stateFilePath;
    // Whether the caller injected a clock. Captured before m_clock consumes the argument, so the
    // constructor never reads the moved-from function.
    bool m_hasInjectedClock = false;
    std::function<qint64()> m_clock;
    int m_nextId = 1;
};

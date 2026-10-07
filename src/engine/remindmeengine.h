#pragma once

#include <QMap>
#include <QObject>
#include <QString>

#include <optional>

#include "timerinfo.h"

namespace Remindme
{
// How long a snooze extends a timer by; the alarm dialog derives its "Snooze +N minute(s)" label
// from this value.
constexpr qint64 snoozeDurationMs = 60 * 1000;
}

class DeadlineScheduler;

class RemindmeEngine : public QObject
{
    Q_OBJECT

public:
    // scheduler is injected by tests; when null the engine creates the production one (realtime
    // timerfd) and parents it to itself.
    explicit RemindmeEngine(QObject *parent = nullptr, const QString &stateFilePath = {}, DeadlineScheduler *scheduler = nullptr);

    // Snoozes a timer, extending its deadline by Remindme::snoozeDurationMs. A still-active timer
    // keeps the time that was left (its live entry is preferred); an expired alarm is dropped from
    // the fired set and re-armed from now. Returns false if the id is neither active nor fired.
    bool snooze(const Remindme::TimerInfo &timer);

    // Moves timers expired as of nowMs() to the fired set and emits timerExpired() for each.
    // Called by the scheduler; public so unit tests can drive expiry deterministically without
    // real-time waits.
    void checkExpiredTimers();

    // Whether an expired alarm is still awaiting Snooze/Dismiss. Fired alarms are persisted
    // across restarts, so the host can tell it has work to resume (and keep autostart on).
    bool hasPendingAlarms() const;
    // Re-emits timerExpired() for every fired alarm loaded from disk. The host calls this once
    // after wiring the signal so alarms that fired before a restart are shown again.
    void replayPendingAlarms();
    // Current time from the engine's clock: the wall clock in production, the injected clock in
    // tests. Exposed so UI built around timers does not reach for the wall clock directly.
    qint64 nowMs() const;

    // Creates a timer lasting durationSeconds and returns it, or std::nullopt when the duration is
    // out of range (no timer is created).
    std::optional<Remindme::TimerInfo> create(qint32 durationSeconds, const QString &message);
    QList<Remindme::TimerInfo> list() const;
    // Whether any active timer exists / whether the active timer with id exists. Avoids the value
    // copy list() makes when the caller only needs a membership test.
    bool isEmpty() const;
    bool contains(qint32 id) const;
    bool cancel(qint32 id);
    bool cancelByMessage(const QString &message);
    // Active and fired timers whose message contains term (case-insensitive substring). Shared by
    // the "cancel by message" UI (to validate and describe the match) and cancelByMessage (to
    // remove exactly that set), so the offer and the action cannot drift apart.
    QList<Remindme::TimerInfo> findTimersByMessage(const QString &term) const;
    void show(qint32 id);

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
    // Persists m_timers and m_firedAlarms and re-arms the scheduler. Call after any mutation.
    void commitTimers();
    // Extends deadline by Remindme::snoozeDurationMs, anchored to now if it has already passed
    // (so snoozing an expired timer doesn't leave it expired again immediately).
    qint64 extendedDeadline(qint64 deadline) const;
    // Extends timer's deadline and (re-)inserts it into m_timers; shared by both snooze() paths.
    void snoozeTimer(Remindme::TimerInfo timer);

    // Keyed by id so iteration is id-ordered: expiry alarms fire (and their dialogs stack) in
    // the order the timers were created, deterministic across runs.
    QMap<int, Remindme::TimerInfo> m_timers;
    // Expired but not yet acknowledged (Snooze/Dismiss). An id lives in exactly one of
    // m_timers/m_firedAlarms; this set is persisted so a session end does not lose the alarm.
    QMap<int, Remindme::TimerInfo> m_firedAlarms;
    QString m_stateFilePath;
    // Owned by the engine only when it created the scheduler (scheduler argument null); an
    // injected scheduler stays owned by the caller.
    DeadlineScheduler *m_scheduler = nullptr;
    int m_nextId = 1;
};

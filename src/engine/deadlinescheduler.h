#pragma once

#include <QObject>

#include <functional>
#include <optional>

// Wakes the engine when the next timer deadline is reached, decoupling the timer model from the
// platform mechanism used to wait for it. Production uses a realtime timerfd (which fires across
// suspend); tests inject a manual implementation driven by a fake clock. The scheduler owns the
// clock so the whole engine reads a single time source.
class DeadlineScheduler : public QObject
{
    Q_OBJECT

public:
    explicit DeadlineScheduler(std::function<qint64()> clock = {}, QObject *parent = nullptr);

    // Current time from the scheduler's clock: the wall clock in production, the injected clock
    // in tests. Exposed so the engine needs no clock of its own.
    qint64 nowMs() const;

    // Arms to emit deadlineReached() when the earliest deadline is reached. std::nullopt disarms
    // (no active timers). Safe to call repeatedly; each call replaces the previous arming.
    virtual void setNextDeadline(std::optional<qint64> nextDeadlineMs) = 0;

Q_SIGNALS:
    void deadlineReached();

private:
    std::function<qint64()> m_clock;
};

// Builds the production scheduler: a realtime timerfd, which fires across suspend. If the timerfd
// cannot be created it falls back to the QTimer scheduler, so deadlines still fire. The returned
// scheduler is parented to parent.
DeadlineScheduler *createDeadlineScheduler(QObject *parent = nullptr);

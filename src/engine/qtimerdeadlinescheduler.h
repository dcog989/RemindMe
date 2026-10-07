#pragma once

#include "deadlinescheduler.h"

#include <QTimer>

#include <optional>

// Fallback scheduler for platforms without timerfd: a single-shot QTimer for the exact wait plus
// a periodic wall-clock sweep. The sweep matters because a monotonic QTimer pauses during suspend,
// so re-reading the wall clock bounds how late an alarm can sound after resume. Tests also drive
// this implementation through an injected clock.
class QTimerDeadlineScheduler final : public DeadlineScheduler
{
    Q_OBJECT

public:
    explicit QTimerDeadlineScheduler(std::function<qint64()> clock = {}, QObject *parent = nullptr);

    void setNextDeadline(std::optional<qint64> nextDeadlineMs) override;

private:
    void checkDeadline();

    std::optional<qint64> m_deadline;
    QTimer m_deadlineTimer;
    QTimer m_sweepTimer;
};

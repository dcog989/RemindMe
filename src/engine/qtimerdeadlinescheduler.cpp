#include "qtimerdeadlinescheduler.h"

#include <algorithm>
#include <utility>

namespace
{
// Bounds how late an alarm can sound after a wall-clock jump (e.g. resume) where the single-shot
// monotonic timer did not fire.
constexpr int sweepIntervalMs = 500;
}

QTimerDeadlineScheduler::QTimerDeadlineScheduler(std::function<qint64()> clock, QObject *parent)
    : DeadlineScheduler(std::move(clock), parent)
{
    m_deadlineTimer.setSingleShot(true);
    connect(&m_deadlineTimer, &QTimer::timeout, this, &QTimerDeadlineScheduler::checkDeadline);

    m_sweepTimer.setInterval(sweepIntervalMs);
    connect(&m_sweepTimer, &QTimer::timeout, this, &QTimerDeadlineScheduler::checkDeadline);
}

void QTimerDeadlineScheduler::setNextDeadline(std::optional<qint64> nextDeadlineMs)
{
    m_deadline = nextDeadlineMs;
    if (!m_deadline.has_value()) {
        m_deadlineTimer.stop();
        m_sweepTimer.stop();
        return;
    }
    if (!m_sweepTimer.isActive()) {
        m_sweepTimer.start();
    }
    m_deadlineTimer.start(std::max<qint64>(0, *m_deadline - nowMs()));
}

void QTimerDeadlineScheduler::checkDeadline()
{
    if (!m_deadline.has_value() || nowMs() < *m_deadline) {
        return;
    }
    // Spent: stop the timers before notifying so a caller that does not re-arm cannot be woken
    // again for the same deadline.
    m_deadline.reset();
    m_deadlineTimer.stop();
    m_sweepTimer.stop();
    Q_EMIT deadlineReached();
}

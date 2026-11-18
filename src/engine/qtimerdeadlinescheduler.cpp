#include "qtimerdeadlinescheduler.h"

#include <algorithm>
#include <limits>
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
    // QTimer::start() takes an int: a far-future deadline would wrap negative and never start.
    const qint64 waitMs = std::clamp<qint64>(*m_deadline - nowMs(), 0, std::numeric_limits<int>::max());
    m_deadlineTimer.start(static_cast<int>(waitMs));
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

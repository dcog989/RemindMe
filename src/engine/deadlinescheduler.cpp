#include "deadlinescheduler.h"

#include <QDateTime>

#include <utility>

#include "qtimerdeadlinescheduler.h"
#include "timerfddeadlinescheduler.h"

DeadlineScheduler::DeadlineScheduler(std::function<qint64()> clock, QObject *parent)
    : QObject(parent)
    , m_clock(clock ? std::move(clock) : [] {
        return QDateTime::currentMSecsSinceEpoch();
    })
{
}

qint64 DeadlineScheduler::nowMs() const
{
    return m_clock();
}

DeadlineScheduler *createDeadlineScheduler(QObject *parent)
{
    // The timerfd path is only correct for the real wall clock; where timerfd is unavailable the
    // monotonic QTimer plus wall-clock sweep fallback is used instead.
    auto *timerfd = new TimerfdDeadlineScheduler(parent);
    if (timerfd->isValid()) {
        return timerfd;
    }
    delete timerfd;
    return new QTimerDeadlineScheduler({}, parent);
}

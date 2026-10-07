#include "deadlinescheduler.h"

#include <QDateTime>

#include <utility>

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
    return new TimerfdDeadlineScheduler(parent);
}

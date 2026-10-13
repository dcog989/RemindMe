#include "deadlinescheduler.h"

#include <QDateTime>
#include <QDebug>

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
    auto *timerfd = new TimerfdDeadlineScheduler(parent);
    if (timerfd->isUsable()) {
        return timerfd;
    }
    // Without a timerfd no deadline would ever be armed and every timer would silently never fire.
    // Fall back to the QTimer scheduler, which still fires across suspend via its wall-clock sweep.
    qWarning() << "timerfd unavailable; using the QTimer deadline scheduler";
    delete timerfd;
    return new QTimerDeadlineScheduler({}, parent);
}

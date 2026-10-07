#include "timerfddeadlinescheduler.h"

#include <QDebug>
#include <QSocketNotifier>

#include <cerrno>
#include <sys/timerfd.h>
#include <unistd.h>

TimerfdDeadlineScheduler::TimerfdDeadlineScheduler(QObject *parent)
    : DeadlineScheduler({}, parent)
{
    m_fd = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC);
    if (m_fd < 0) {
        return;
    }
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this](QSocketDescriptor, QSocketNotifier::Type) {
        // Drain the timerfd. The fd is non-blocking, so a spurious wakeup can read nothing
        // (EAGAIN); only a genuine error is worth reporting.
        quint64 expirations = 0;
        const ssize_t bytes = ::read(m_fd, &expirations, sizeof(expirations));
        if (bytes < 0 && errno != EAGAIN) {
            qWarning() << "Could not read timerfd, errno" << errno;
        }
        Q_EMIT deadlineReached();
    });
}

TimerfdDeadlineScheduler::~TimerfdDeadlineScheduler()
{
    if (m_fd >= 0) {
        ::close(m_fd);
    }
}

void TimerfdDeadlineScheduler::setNextDeadline(std::optional<qint64> nextDeadlineMs)
{
    if (m_fd < 0) {
        return;
    }
    itimerspec ts{};
    if (nextDeadlineMs.has_value()) {
        // An absolute time is what makes a past deadline (e.g. crossed while suspended) fire
        // immediately on resume; a zero it_value disarms.
        ts.it_value.tv_sec = static_cast<time_t>(*nextDeadlineMs / 1000);
        ts.it_value.tv_nsec = static_cast<long>(*nextDeadlineMs % 1000) * 1000000L;
    }
    if (timerfd_settime(m_fd, TFD_TIMER_ABSTIME, &ts, nullptr) != 0) {
        qWarning() << "Could not arm timerfd, errno" << errno;
    }
}

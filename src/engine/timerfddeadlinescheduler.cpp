#include "timerfddeadlinescheduler.h"

#include <QDebug>
#include <QSocketNotifier>

#ifdef Q_OS_LINUX
#include <cerrno>
#include <sys/timerfd.h>
#include <unistd.h>
#endif

TimerfdDeadlineScheduler::TimerfdDeadlineScheduler(QObject *parent)
    : DeadlineScheduler({}, parent)
{
#ifdef Q_OS_LINUX
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
#endif
}

TimerfdDeadlineScheduler::~TimerfdDeadlineScheduler()
{
#ifdef Q_OS_LINUX
    if (m_fd >= 0) {
        ::close(m_fd);
    }
#endif
}

bool TimerfdDeadlineScheduler::isValid() const
{
    return m_fd >= 0;
}

void TimerfdDeadlineScheduler::setNextDeadline(std::optional<qint64> nextDeadlineMs)
{
#ifdef Q_OS_LINUX
    if (m_fd < 0) {
        return;
    }
    itimerspec ts{};
    if (nextDeadlineMs.has_value()) {
        // An absolute time is what makes a past deadline (e.g. crossed while suspended) fire
        // immediately on resume; a zero it_value disarms.
        ts.it_value.tv_sec = time_t(*nextDeadlineMs / 1000);
        ts.it_value.tv_nsec = long(*nextDeadlineMs % 1000) * 1000000L;
    }
    if (timerfd_settime(m_fd, TFD_TIMER_ABSTIME, &ts, nullptr) != 0) {
        qWarning() << "Could not arm timerfd, errno" << errno;
    }
#else
    Q_UNUSED(nextDeadlineMs);
#endif
}

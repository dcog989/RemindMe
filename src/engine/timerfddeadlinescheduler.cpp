#include "timerfddeadlinescheduler.h"

#include <QDateTime>
#include <QDebug>
#include <QSocketNotifier>

#include <cerrno>
#include <sys/timerfd.h>
#include <unistd.h>

namespace
{
// Absolute realtime deadline for timerfd_settime(); a default-constructed spec disarms.
itimerspec absoluteSpec(qint64 deadlineMs)
{
    itimerspec spec{};
    spec.it_value.tv_sec = static_cast<time_t>(deadlineMs / 1000);
    spec.it_value.tv_nsec = static_cast<long>(deadlineMs % 1000) * 1000000L;
    return spec;
}
}

TimerfdDeadlineScheduler::TimerfdDeadlineScheduler(QObject *parent)
    : DeadlineScheduler({}, parent)
{
    m_fd = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC);
    if (m_fd < 0) {
        qWarning() << "Could not create timerfd, errno" << errno;
        return;
    }
    // A timerfd that cannot be armed would silently never wake the engine, so probe arming once
    // here and let createDeadlineScheduler() fall back to the QTimer scheduler instead.
    const itimerspec probe = absoluteSpec(QDateTime::currentMSecsSinceEpoch() + 60 * 60 * 1000);
    if (timerfd_settime(m_fd, TFD_TIMER_ABSTIME, &probe, nullptr) != 0) {
        qWarning() << "Could not arm timerfd, errno" << errno;
        ::close(m_fd);
        m_fd = -1;
        return;
    }
    const itimerspec disarm{};
    timerfd_settime(m_fd, TFD_TIMER_ABSTIME, &disarm, nullptr);

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

bool TimerfdDeadlineScheduler::isUsable() const
{
    return m_fd >= 0;
}

void TimerfdDeadlineScheduler::setNextDeadline(std::optional<qint64> nextDeadlineMs)
{
    if (m_fd < 0) {
        return;
    }
    // An absolute time is what makes a past deadline (e.g. crossed while suspended) fire
    // immediately on resume; a zero it_value disarms.
    const itimerspec spec = nextDeadlineMs.has_value() ? absoluteSpec(*nextDeadlineMs) : itimerspec{};
    if (timerfd_settime(m_fd, TFD_TIMER_ABSTIME, &spec, nullptr) != 0) {
        qWarning() << "Could not arm timerfd, errno" << errno;
    }
}

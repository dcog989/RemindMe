#pragma once

#include "deadlinescheduler.h"

class QSocketNotifier;

// Real-clock scheduler backed by a CLOCK_REALTIME timerfd. Its deadline is an absolute wall-clock
// time, so a deadline crossed while the machine was suspended fires as soon as the fd becomes
// readable on resume. isValid() is false where timerfd is unavailable, in which case the factory
// falls back to QTimerDeadlineScheduler.
class TimerfdDeadlineScheduler final : public DeadlineScheduler
{
    Q_OBJECT

public:
    explicit TimerfdDeadlineScheduler(QObject *parent = nullptr);
    ~TimerfdDeadlineScheduler() override;

    bool isValid() const;

    void setNextDeadline(std::optional<qint64> nextDeadlineMs) override;

private:
    int m_fd = -1;
    QSocketNotifier *m_notifier = nullptr;
};

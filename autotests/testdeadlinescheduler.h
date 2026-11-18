#pragma once

#include "deadlinescheduler.h"

#include <functional>
#include <optional>
#include <utility>

// Deterministic scheduler shared by the engine and adaptor tests: it records the deadline the
// engine armed and only wakes the engine when the test calls fire(), so expiry never depends on
// wall time or an event loop. No Q_OBJECT: it adds no signals or slots of its own.
class TestDeadlineScheduler : public DeadlineScheduler
{
public:
    explicit TestDeadlineScheduler(std::function<qint64()> clock, QObject *parent = nullptr)
        : DeadlineScheduler(std::move(clock), parent)
    {
    }

    void setNextDeadline(std::optional<qint64> nextDeadlineMs) override
    {
        deadline = nextDeadlineMs;
    }

    void fire()
    {
        // Both real schedulers disarm before emitting (single-shot); mirror that so a missing
        // engine re-arm is visible to the tests.
        deadline.reset();
        Q_EMIT deadlineReached();
    }

    std::optional<qint64> deadline;
};

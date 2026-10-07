#include "deadlinescheduler.h"
#include "qtimerdeadlinescheduler.h"

#include <QScopedPointer>
#include <QSignalSpy>
#include <QTest>

#include <optional>

// The engine tests drive expiry through a manual scheduler; these tests cover the test-only QTimer
// scheduler, notably that its wall-clock sweep notices a clock jump and that disarming stops it
// from waking.
class DeadlineSchedulerTest : public QObject
{
    Q_OBJECT

private:
    qint64 fakeNow = 0;

private Q_SLOTS:
    void testFactoryProvidesScheduler()
    {
        QScopedPointer<DeadlineScheduler> scheduler(createDeadlineScheduler());
        QVERIFY(scheduler);
    }

    void testFiresWhenDeadlineReached()
    {
        fakeNow = static_cast<qint64>(1ULL << 40U);
        QTimerDeadlineScheduler scheduler([this]() {
            return fakeNow;
        });
        QSignalSpy spy(&scheduler, &DeadlineScheduler::deadlineReached);

        scheduler.setNextDeadline(fakeNow + 1000);
        fakeNow += 2000; // the deadline is crossed, as after a resume
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);

        // The deadline is spent: the scheduler must not wake again unless re-armed.
        QTest::qWait(700);
        QCOMPARE(spy.count(), 1);
    }

    void testDisarmStopsWaking()
    {
        fakeNow = static_cast<qint64>(1ULL << 40U);
        QTimerDeadlineScheduler scheduler([this]() {
            return fakeNow;
        });
        QSignalSpy spy(&scheduler, &DeadlineScheduler::deadlineReached);

        scheduler.setNextDeadline(fakeNow + 1000);
        scheduler.setNextDeadline(std::nullopt);
        fakeNow += 60 * 1000;
        QTest::qWait(700); // longer than one sweep interval
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_GUILESS_MAIN(DeadlineSchedulerTest)

#include "deadlineschedulertest.moc"

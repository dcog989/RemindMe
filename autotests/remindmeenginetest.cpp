#include "remindmeengine.h"
#include "deadlinescheduler.h"
#include "remindmetime.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <functional>
#include <optional>
#include <utility>

using namespace Remindme;

// Deterministic scheduler for the engine tests: it records the deadline the engine armed and only
// wakes the engine when the test calls fire(), so expiry never depends on wall time or an event
// loop. The QTimer-based scheduler's own wait/sweep behaviour is covered separately in
// deadlineschedulertest.cpp.
class TestDeadlineScheduler : public DeadlineScheduler
{
    Q_OBJECT

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
        Q_EMIT deadlineReached();
    }

    std::optional<qint64> deadline;
};

class RemindmeEngineTest : public QObject
{
    Q_OBJECT

private:
    QScopedPointer<RemindmeEngine> engine;
    QScopedPointer<TestDeadlineScheduler> scheduler;
    QScopedPointer<QTemporaryDir> tempDir;
    // Controllable clock: tests advance it instead of waiting on real time.
    qint64 fakeNow = 0;

    QString stateFilePath() const
    {
        return tempDir->filePath(QStringLiteral("timers.json"));
    }

    void resetEngine()
    {
        engine.reset();
        scheduler.reset(new TestDeadlineScheduler([this]() {
            return fakeNow;
        }));
        engine.reset(new RemindmeEngine(nullptr, stateFilePath(), scheduler.data()));
    }

    static bool hasId(const QList<TimerInfo> &timers, int id)
    {
        for (const TimerInfo &timer : timers) {
            if (timer.id == id) {
                return true;
            }
        }
        return false;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QCoreApplication::setApplicationName(QStringLiteral("krunner-remindme"));
    }

    void init()
    {
        tempDir.reset(new QTemporaryDir);
        QVERIFY(tempDir->isValid());
        fakeNow = 0;
        resetEngine();
    }

    void cleanup()
    {
        engine.reset();
        scheduler.reset();
        tempDir.reset();
    }

    void testCreateAndList()
    {
        const qint64 before = fakeNow;
        const TimerInfo first = engine->create(5 * 60, QStringLiteral("pasta"));
        QCOMPARE(first.id, 1);
        QCOMPARE(first.deadline, before + 5 * 60 * 1000);
        QCOMPARE(first.message, QStringLiteral("pasta"));

        const TimerInfo second = engine->create(60, QString());
        QCOMPARE(second.id, 2);

        const QList<TimerInfo> timers = engine->list();
        QCOMPARE(timers.size(), 2);
        QVERIFY(hasId(timers, 1));
        QVERIFY(hasId(timers, 2));
        QCOMPARE(timers.at(0).id, 1); // sorted by id, as the D-Bus interface documents
        QCOMPARE(timers.at(1).id, 2);

        // Membership helpers used by the host and runner avoid the list() copy
        QVERIFY(!engine->isEmpty());
        QVERIFY(engine->contains(1));
        QVERIFY(engine->contains(2));
        QVERIFY(!engine->contains(99));
    }

    void testCreateRejectsInvalidDurations()
    {
        QCOMPARE(engine->create(0, QString()).id, -1);
        QCOMPARE(engine->create(-5, QString()).id, -1);
        QCOMPARE(engine->create(maxDurationSeconds + 1, QString()).id, -1);
        QVERIFY(engine->list().isEmpty());

        // Boundary values are still accepted
        QVERIFY(engine->create(maxDurationSeconds, QString()).id > 0);
        QCOMPARE(engine->list().size(), 1);
    }

    void testCancelById()
    {
        engine->create(60, QStringLiteral("one"));
        engine->create(60, QStringLiteral("two"));

        QVERIFY(engine->cancel(1));
        QCOMPARE(engine->list().size(), 1);
        QVERIFY(hasId(engine->list(), 2));

        QVERIFY(!engine->cancel(1)); // already gone
        QVERIFY(!engine->cancel(99)); // never existed
    }

    void testCancelByMessage()
    {
        engine->create(60, QStringLiteral("meeting"));
        engine->create(60, QStringLiteral("meeting"));
        engine->create(60, QStringLiteral("lunch"));

        QVERIFY(engine->cancelByMessage(QStringLiteral("meeting")));
        const QList<TimerInfo> timers = engine->list();
        QCOMPARE(timers.size(), 1);
        QCOMPARE(timers.constFirst().message, QStringLiteral("lunch"));

        QVERIFY(!engine->cancelByMessage(QStringLiteral("meeting"))); // none left
        QVERIFY(!engine->cancelByMessage(QStringLiteral("none")));

        // Matching is a case-insensitive substring, not exact equality
        engine->create(60, QStringLiteral("Call Mom"));
        QVERIFY(engine->cancelByMessage(QStringLiteral("call")));
        QCOMPARE(engine->list().size(), 1); // only "lunch" remains
        QCOMPARE(engine->list().constFirst().message, QStringLiteral("lunch"));
    }

    void testPersistenceAcrossRestart()
    {
        const qint64 deadline = engine->create(600, QStringLiteral("hello")).deadline;
        engine.reset();
        resetEngine(); // reload from disk

        QCOMPARE(engine->list().size(), 1);
        const TimerInfo restored = engine->list().constFirst();
        QCOMPARE(restored.id, 1);
        QCOMPARE(restored.message, QStringLiteral("hello"));
        QCOMPARE(restored.deadline, deadline);

        // The next id must continue after the restored timer
        QCOMPARE(engine->create(60, QString()).id, 2);
    }

    void testExpiredOnRestart()
    {
        // Simulate a timer that expired while the helper process was stopped. The injected
        // clock is anchored to a large base so the "past" deadline stays a positive value.
        fakeNow = qint64(1) << 40;
        const qint64 past = fakeNow - 60 * 1000;
        QFile file(stateFilePath());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonArray{QJsonObject{{QStringLiteral("id"), 7},
                                                        {QStringLiteral("deadline"), double(past)},
                                                        {QStringLiteral("message"), QStringLiteral("missed")}}})
                       .toJson());
        file.close();

        resetEngine(); // arms the scheduler for the already-past deadline

        QVERIFY(scheduler->deadline.has_value());
        scheduler->fire(); // the scheduler wakes the engine immediately on start
        QCOMPARE(engine->list().size(), 0);
        QFile remaining(stateFilePath());
        QVERIFY(remaining.open(QIODevice::ReadOnly));
        QVERIFY(QJsonDocument::fromJson(remaining.readAll()).isArray());
    }

    void testInvalidStateOnDiskIsIgnored()
    {
        QFile file(stateFilePath());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("this is not json");
        file.close();

        resetEngine();
        QCOMPARE(engine->list().size(), 0);

        // The unreadable file is preserved as .bak and not overwritten by the next save.
        QVERIFY(!QFile::exists(stateFilePath()));
        QFile backup(stateFilePath() + QStringLiteral(".bak"));
        QVERIFY(backup.open(QIODevice::ReadOnly));
        QCOMPARE(backup.readAll(), QByteArray("this is not json"));

        QCOMPARE(engine->create(60, QString()).id, 1); // next mutation writes a fresh file
        QVERIFY(QFile::exists(stateFilePath()));
    }

    void testSnoozeRearmsExpiredTimer()
    {
        const TimerInfo timer = engine->create(1, QStringLiteral("tea"));
        fakeNow += 2 * 1000; // the deadline has now passed
        engine->checkExpiredTimers();

        QCOMPARE(engine->list().size(), 0); // expired and no longer active
        QVERIFY(engine->hasPendingAlarms()); // but awaiting acknowledgment

        QVERIFY(engine->snooze(timer));

        const QList<TimerInfo> timers = engine->list();
        QCOMPARE(timers.size(), 1);
        QCOMPARE(timers.constFirst().id, timer.id);
        QCOMPARE(timers.constFirst().message, QStringLiteral("tea"));
        QCOMPARE(timers.constFirst().deadline, fakeNow + Remindme::snoozeDurationMs);
        QVERIFY(!engine->hasPendingAlarms()); // snoozing acknowledged the alarm
    }

    void testFiredAlarmSurvivesRestart()
    {
        const TimerInfo timer = engine->create(1, QStringLiteral("bread"));
        fakeNow += 2 * 1000; // the deadline has now passed
        engine->checkExpiredTimers();
        QVERIFY(engine->hasPendingAlarms());

        engine.reset();
        resetEngine(); // reload from disk

        // The fired alarm is not active, but is still pending and replayed to the host.
        QCOMPARE(engine->list().size(), 0);
        QVERIFY(engine->hasPendingAlarms());
        QSignalSpy replaySpy(engine.data(), &RemindmeEngine::timerExpired);
        engine->replayPendingAlarms();
        QCOMPARE(replaySpy.count(), 1);
        const TimerInfo replayed = replaySpy.constFirst().constFirst().value<TimerInfo>();
        QCOMPARE(replayed.id, timer.id);
        QCOMPARE(replayed.message, QStringLiteral("bread"));
        QCOMPARE(replayed.deadline, timer.deadline);
    }

    void testAcknowledgingFiredAlarmClearsIt()
    {
        const TimerInfo timer = engine->create(1, QStringLiteral("bread"));
        fakeNow += 2 * 1000;
        engine->checkExpiredTimers();
        QVERIFY(engine->hasPendingAlarms());

        QVERIFY(engine->cancel(timer.id)); // "Dismiss"
        QVERIFY(!engine->hasPendingAlarms());

        engine.reset();
        resetEngine(); // the acknowledgment must have persisted
        QVERIFY(!engine->hasPendingAlarms());
        QCOMPARE(engine->list().size(), 0);
    }

    void testSnoozeActiveTimerKeepsRemainingTime()
    {
        // A still-active timer snoozed via snooze() must keep the time that was left,
        // adding one minute to the existing deadline instead of collapsing to now + 1 minute
        const qint64 before = fakeNow;
        const TimerInfo timer = engine->create(10 * 60, QStringLiteral("meeting"));
        const qint64 originalDeadline = timer.deadline;

        QVERIFY(engine->snooze(timer));

        const QList<TimerInfo> timers = engine->list();
        QCOMPARE(timers.size(), 1);
        QCOMPARE(timers.constFirst().id, timer.id);
        QCOMPARE(timers.constFirst().deadline, before + 10 * 60 * 1000 + Remindme::snoozeDurationMs); // 10 min left + 1 min snooze
        QCOMPARE(timers.constFirst().deadline, originalDeadline + Remindme::snoozeDurationMs); // strictly extended, not collapsed
    }

    void testExpiryEmitsSignal()
    {
        QSignalSpy spy(engine.data(), &RemindmeEngine::timerExpired);
        engine->create(1, QStringLiteral("blip"));
        fakeNow += 2 * 1000; // the deadline has now passed
        engine->checkExpiredTimers();

        QCOMPARE(spy.count(), 1);
        const TimerInfo timer = spy.takeFirst().constFirst().value<TimerInfo>();
        QCOMPARE(timer.message, QStringLiteral("blip"));
        QCOMPARE(engine->list().size(), 0); // the expired timer is no longer active
    }

    void testExpiryAfterClockJump()
    {
        // Simulates a timer coming due while the machine was asleep: the wall clock (fakeNow)
        // jumps forward past the deadline. The QTimer scheduler's periodic wall-clock sweep that
        // notices this on its own is covered in deadlineschedulertest.cpp; here the manual
        // scheduler stands in for that wake.
        QSignalSpy spy(engine.data(), &RemindmeEngine::timerExpired);
        engine->create(60, QStringLiteral("nap"));
        fakeNow += 5 * 60 * 1000; // slept past the deadline
        scheduler->fire();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(engine->list().size(), 0);
    }

    void testExpiryOrderIsById()
    {
        // QMap iteration is id-ordered, so equal-deadline alarms fire (and their dialogs stack)
        // deterministically in creation order rather than in QHash bucket order.
        QSignalSpy spy(engine.data(), &RemindmeEngine::timerExpired);
        engine->create(1, QStringLiteral("first"));
        engine->create(1, QStringLiteral("second"));
        engine->create(1, QStringLiteral("third"));
        fakeNow += 2 * 1000; // every deadline has passed at once
        engine->checkExpiredTimers();

        QCOMPARE(spy.count(), 3);
        QCOMPARE(spy.at(0).constFirst().value<TimerInfo>().id, 1);
        QCOMPARE(spy.at(1).constFirst().value<TimerInfo>().id, 2);
        QCOMPARE(spy.at(2).constFirst().value<TimerInfo>().id, 3);
    }
};

QTEST_GUILESS_MAIN(RemindmeEngineTest)

#include "remindmeenginetest.moc"

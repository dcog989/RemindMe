#include "remindmeengine.h"
#include "remindmetime.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Remindme;

class RemindmeEngineTest : public QObject
{
    Q_OBJECT

private:
    QScopedPointer<RemindmeEngine> engine;
    QScopedPointer<QTemporaryDir> tempDir;
    // Controllable clock: tests advance it instead of waiting on real time.
    qint64 fakeNow = 0;

    QString stateFilePath() const
    {
        return tempDir->filePath(QStringLiteral("timers.json"));
    }

    void resetEngine()
    {
        engine.reset(new RemindmeEngine(nullptr, stateFilePath(), [this]() {
            return fakeNow;
        }));
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
        tempDir.reset();
    }

    void testCreateAndList()
    {
        const qint64 before = fakeNow;
        const TimerInfo first = engine->Create(5 * 60, QStringLiteral("pasta"));
        QCOMPARE(first.id, 1);
        QCOMPARE(first.deadline, before + 5 * 60 * 1000);
        QCOMPARE(first.message, QStringLiteral("pasta"));

        const TimerInfo second = engine->Create(60, QString());
        QCOMPARE(second.id, 2);

        const QList<TimerInfo> timers = engine->List();
        QCOMPARE(timers.size(), 2);
        QVERIFY(hasId(timers, 1));
        QVERIFY(hasId(timers, 2));
        QCOMPARE(timers.at(0).id, 1); // sorted by id, as the D-Bus interface documents
        QCOMPARE(timers.at(1).id, 2);
    }

    void testCreateRejectsInvalidDurations()
    {
        QCOMPARE(engine->Create(0, QString()).id, -1);
        QCOMPARE(engine->Create(-5, QString()).id, -1);
        QCOMPARE(engine->Create(maxDurationSeconds + 1, QString()).id, -1);
        QVERIFY(engine->List().isEmpty());

        // Boundary values are still accepted
        QVERIFY(engine->Create(maxDurationSeconds, QString()).id > 0);
        QCOMPARE(engine->List().size(), 1);
    }

    void testCancelById()
    {
        engine->Create(60, QStringLiteral("one"));
        engine->Create(60, QStringLiteral("two"));

        QVERIFY(engine->Cancel(1));
        QCOMPARE(engine->List().size(), 1);
        QVERIFY(hasId(engine->List(), 2));

        QVERIFY(!engine->Cancel(1)); // already gone
        QVERIFY(!engine->Cancel(99)); // never existed
    }

    void testCancelByMessage()
    {
        engine->Create(60, QStringLiteral("meeting"));
        engine->Create(60, QStringLiteral("meeting"));
        engine->Create(60, QStringLiteral("lunch"));

        QVERIFY(engine->CancelByMessage(QStringLiteral("meeting")));
        const QList<TimerInfo> timers = engine->List();
        QCOMPARE(timers.size(), 1);
        QCOMPARE(timers.constFirst().message, QStringLiteral("lunch"));

        QVERIFY(!engine->CancelByMessage(QStringLiteral("meeting"))); // none left
        QVERIFY(!engine->CancelByMessage(QStringLiteral("none")));
    }

    void testPersistenceAcrossRestart()
    {
        const qint64 deadline = engine->Create(600, QStringLiteral("hello")).deadline;
        engine.reset();
        resetEngine(); // reload from disk

        QCOMPARE(engine->List().size(), 1);
        const TimerInfo restored = engine->List().constFirst();
        QCOMPARE(restored.id, 1);
        QCOMPARE(restored.message, QStringLiteral("hello"));
        QCOMPARE(restored.deadline, deadline);

        // The next id must continue after the restored timer
        QCOMPARE(engine->Create(60, QString()).id, 2);
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

        resetEngine(); // fires the expired timer immediately on start

        QTRY_COMPARE_WITH_TIMEOUT(engine->List().size(), 0, 1000);
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
        QCOMPARE(engine->List().size(), 0);

        // The unreadable file is preserved as .bak and not overwritten by the next save.
        QVERIFY(!QFile::exists(stateFilePath()));
        QFile backup(stateFilePath() + QStringLiteral(".bak"));
        QVERIFY(backup.open(QIODevice::ReadOnly));
        QCOMPARE(backup.readAll(), QByteArray("this is not json"));

        QCOMPARE(engine->Create(60, QString()).id, 1); // next mutation writes a fresh file
        QVERIFY(QFile::exists(stateFilePath()));
    }

    void testSnoozeRearmsExpiredTimer()
    {
        const TimerInfo timer = engine->Create(1, QStringLiteral("tea"));
        fakeNow += 2 * 1000; // the deadline has now passed
        engine->checkExpiredTimers();

        QCOMPARE(engine->List().size(), 0); // expired and no longer active
        QVERIFY(engine->hasPendingAlarms()); // but awaiting acknowledgment

        engine->rearmExpiredTimer(timer);

        const QList<TimerInfo> timers = engine->List();
        QCOMPARE(timers.size(), 1);
        QCOMPARE(timers.constFirst().id, timer.id);
        QCOMPARE(timers.constFirst().message, QStringLiteral("tea"));
        QCOMPARE(timers.constFirst().deadline, fakeNow + Remindme::snoozeDurationMs);
        QVERIFY(!engine->hasPendingAlarms()); // snoozing acknowledged the alarm
    }

    void testFiredAlarmSurvivesRestart()
    {
        const TimerInfo timer = engine->Create(1, QStringLiteral("bread"));
        fakeNow += 2 * 1000; // the deadline has now passed
        engine->checkExpiredTimers();
        QVERIFY(engine->hasPendingAlarms());

        engine.reset();
        resetEngine(); // reload from disk

        // The fired alarm is not active, but is still pending and replayed to the host.
        QCOMPARE(engine->List().size(), 0);
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
        const TimerInfo timer = engine->Create(1, QStringLiteral("bread"));
        fakeNow += 2 * 1000;
        engine->checkExpiredTimers();
        QVERIFY(engine->hasPendingAlarms());

        QVERIFY(engine->Cancel(timer.id)); // "Dismiss"
        QVERIFY(!engine->hasPendingAlarms());

        engine.reset();
        resetEngine(); // the acknowledgment must have persisted
        QVERIFY(!engine->hasPendingAlarms());
        QCOMPARE(engine->List().size(), 0);
    }

    void testSnoozeActiveTimerKeepsRemainingTime()
    {
        // A still-active timer snoozed via Snooze() must keep the time that was left,
        // adding one minute to the existing deadline instead of collapsing to now + 1 minute
        const qint64 before = fakeNow;
        const TimerInfo timer = engine->Create(10 * 60, QStringLiteral("meeting"));
        const qint64 originalDeadline = timer.deadline;

        QVERIFY(engine->Snooze(timer.id));

        const QList<TimerInfo> timers = engine->List();
        QCOMPARE(timers.size(), 1);
        QCOMPARE(timers.constFirst().id, timer.id);
        QCOMPARE(timers.constFirst().deadline, before + 10 * 60 * 1000 + Remindme::snoozeDurationMs); // 10 min left + 1 min snooze
        QCOMPARE(timers.constFirst().deadline, originalDeadline + Remindme::snoozeDurationMs); // strictly extended, not collapsed
    }

    void testExpiryEmitsSignal()
    {
        QSignalSpy spy(engine.data(), &RemindmeEngine::timerExpired);
        engine->Create(1, QStringLiteral("blip"));
        fakeNow += 2 * 1000; // the deadline has now passed
        engine->checkExpiredTimers();

        QCOMPARE(spy.count(), 1);
        const TimerInfo timer = spy.takeFirst().constFirst().value<TimerInfo>();
        QCOMPARE(timer.message, QStringLiteral("blip"));
        QCOMPARE(engine->List().size(), 0); // the expired timer is no longer active
    }

    void testExpiryAfterClockJump()
    {
        // Simulates a timer coming due while the machine was asleep: the wall clock (fakeNow)
        // jumps forward past the deadline. Nothing calls checkExpiredTimers() directly -- the
        // periodic wall-clock sweep (remindmeengine.cpp) must notice on its own within one
        // sweep interval, without relying on any suspend notification.
        QSignalSpy spy(engine.data(), &RemindmeEngine::timerExpired);
        engine->Create(60, QStringLiteral("nap"));
        fakeNow += 5 * 60 * 1000; // slept past the deadline
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
        QCOMPARE(engine->List().size(), 0);
    }
};

QTEST_GUILESS_MAIN(RemindmeEngineTest)

#include "remindmeenginetest.moc"

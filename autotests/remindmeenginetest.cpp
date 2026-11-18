#include "remindmeengine.h"
#include "remindmetime.h"
#include "testdeadlinescheduler.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Remindme;

// The QTimer-based scheduler's own wait/sweep behaviour is covered separately in
// deadlineschedulertest.cpp.
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
        const TimerInfo first = engine->create(5 * 60, QStringLiteral("pasta")).value();
        QCOMPARE(first.id, 1);
        QCOMPARE(first.deadline, before + 5 * 60 * 1000);
        QCOMPARE(first.message, QStringLiteral("pasta"));

        const TimerInfo second = engine->create(60, QString()).value();
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
        QVERIFY(!engine->create(0, QString()).has_value());
        QVERIFY(!engine->create(-5, QString()).has_value());
        QVERIFY(!engine->create(maxDurationSeconds + 1, QString()).has_value());
        QVERIFY(engine->list().isEmpty());

        // Boundary values are still accepted
        QVERIFY(engine->create(maxDurationSeconds, QString()).has_value());
        QCOMPARE(engine->list().size(), 1);
    }

    void testCreateStopsAtTheTimerCap()
    {
        // A hard cap bounds the per-create file rewrite and the state file size.
        for (int i = 0; i < Remindme::maxTimers; ++i) {
            QVERIFY(engine->create(60, QString()).has_value());
        }
        QVERIFY(engine->isFull());
        QVERIFY(!engine->create(60, QString()).has_value());
        QCOMPARE(engine->list().size(), Remindme::maxTimers);
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

    void testCancelSignalsRemovedIdForActiveAndFired()
    {
        QSignalSpy spy(engine.data(), &RemindmeEngine::timerRemoved);
        engine->create(1, QStringLiteral("tea"));

        // A fired alarm stays addressable, so "rme cancel <id>" can acknowledge its window.
        fakeNow += 2 * 1000;
        engine->checkExpiredTimers();
        QVERIFY(engine->contains(1));

        QVERIFY(engine->cancel(1));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.constFirst().constFirst().toInt(), 1);
        QVERIFY(!engine->contains(1));
        QVERIFY(!engine->hasPendingAlarms());
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
        const qint64 deadline = engine->create(600, QStringLiteral("hello")).value().deadline;
        engine.reset();
        resetEngine(); // reload from disk

        QCOMPARE(engine->list().size(), 1);
        const TimerInfo restored = engine->list().constFirst();
        QCOMPARE(restored.id, 1);
        QCOMPARE(restored.message, QStringLiteral("hello"));
        QCOMPARE(restored.deadline, deadline);

        // The next id must continue after the restored timer
        QCOMPARE(engine->create(60, QString()).value().id, 2);
    }

    void testExpiredOnRestart()
    {
        // Simulate a timer that expired while the helper process was stopped. The injected
        // clock is anchored to a large base so the "past" deadline stays a positive value.
        fakeNow = static_cast<qint64>(1ULL << 40U);
        const qint64 past = fakeNow - 60 * 1000;
        QFile file(stateFilePath());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonArray{QJsonObject{{QStringLiteral("id"), 7},
                                                        {QStringLiteral("deadline"), static_cast<double>(past)},
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

    void testDuplicateIdInStateFileKeepsOneEntry()
    {
        // A hand-edited file can repeat an id. The later entry must replace the earlier one, or the
        // same id ends up in the active and the fired set at once and fires a second time.
        QFile file(stateFilePath());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(
            QJsonDocument(
                QJsonArray{QJsonObject{{QStringLiteral("id"), 3}, {QStringLiteral("deadline"), 60000.0}, {QStringLiteral("message"), QStringLiteral("active")}},
                           QJsonObject{{QStringLiteral("id"), 3},
                                       {QStringLiteral("deadline"), 30000.0},
                                       {QStringLiteral("message"), QStringLiteral("fired")},
                                       {QStringLiteral("fired"), true}}})
                .toJson());
        file.close();

        resetEngine();

        QVERIFY(engine->list().isEmpty());
        QCOMPARE(engine->pendingAlarms().size(), 1);
        QCOMPARE(engine->pendingAlarms().constFirst().message, QStringLiteral("fired"));
        QVERIFY(!scheduler->deadline.has_value()); // the fired entry does not arm the timer
    }

    void testArmsEarliestDeadline()
    {
        const TimerInfo first = engine->create(10 * 60, QStringLiteral("first")).value();
        const TimerInfo second = engine->create(2 * 60, QStringLiteral("second")).value();

        // The engine arms the earliest deadline, not the one created last.
        QVERIFY(scheduler->deadline == second.deadline);
        QVERIFY(engine->cancel(second.id));
        QVERIFY(scheduler->deadline == first.deadline);
        QVERIFY(engine->cancel(first.id));
        // Nothing left to wake for.
        QVERIFY(!scheduler->deadline.has_value());
    }

    void testRearmsForTheNextTimerAfterExpiry()
    {
        // A missed re-arm after a deadline fires would leave every later timer silent.
        const TimerInfo first = engine->create(60, QStringLiteral("first")).value();
        const TimerInfo second = engine->create(120, QStringLiteral("second")).value();

        fakeNow = first.deadline;
        scheduler->fire();
        QCOMPARE(engine->list().size(), 1);
        QVERIFY(scheduler->deadline == second.deadline);

        fakeNow = second.deadline;
        scheduler->fire();
        QVERIFY(engine->list().isEmpty());
        QVERIFY(!scheduler->deadline.has_value());
    }

    void testSpuriousWakeRearms()
    {
        // A wake before the deadline expires nothing; the engine must still re-arm, otherwise the
        // timer would never fire (the fallback's 500 ms sweep can wake early).
        const TimerInfo timer = engine->create(60, QStringLiteral("tea")).value();

        scheduler->fire();
        QCOMPARE(engine->list().size(), 1);
        QVERIFY(scheduler->deadline == timer.deadline);
    }

    void testInvalidStateOnDiskIsIgnored()
    {
        QFile file(stateFilePath());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("this is not json");
        file.close();

        resetEngine();
        QCOMPARE(engine->list().size(), 0);

        // The unreadable file is preserved under a timestamped .bak name and is not overwritten by
        // the next save.
        QVERIFY(!QFile::exists(stateFilePath()));
        const auto backups = [this]() {
            return QDir(tempDir->path()).entryList(QStringList{QStringLiteral("timers.json*.bak")}, QDir::Files, QDir::Name);
        };
        QCOMPARE(backups().size(), 1);
        QFile first(tempDir->filePath(backups().constFirst()));
        QVERIFY(first.open(QIODevice::ReadOnly));
        QCOMPARE(first.readAll(), QByteArray("this is not json"));
        first.close();

        // A second corruption backs up the new file without destroying the first backup.
        QFile again(stateFilePath());
        QVERIFY(again.open(QIODevice::WriteOnly));
        again.write("still not json");
        again.close();
        resetEngine();
        QCOMPARE(backups().size(), 2);

        QCOMPARE(engine->create(60, QString()).value().id, 1); // next mutation writes a fresh file
        QVERIFY(QFile::exists(stateFilePath()));
    }

    void testUnreadableStateFileIsPreserved()
    {
        QFile file(stateFilePath());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"([{"id":1,"deadline":1000,"message":"keep"}])");
        file.close();
        QVERIFY(file.setPermissions(QFileDevice::WriteOwner));
        QFile probe(stateFilePath());
        if (probe.open(QIODevice::ReadOnly)) {
            probe.close();
            QSKIP("permission bits are not enforced here; cannot simulate a read failure");
        }

        resetEngine();
        QCOMPARE(engine->list().size(), 0);

        // An unreadable file is moved aside, not replaced by the next save.
        QVERIFY(!QFile::exists(stateFilePath()));
        const QStringList backups = QDir(tempDir->path()).entryList(QStringList{QStringLiteral("timers.json*.bak")}, QDir::Files);
        QCOMPARE(backups.size(), 1);
        QCOMPARE(engine->create(60, QString()).value().id, 1);
        QVERIFY(QFile::exists(stateFilePath()));
    }

    void testSnoozeRearmsExpiredTimer()
    {
        const TimerInfo timer = engine->create(1, QStringLiteral("tea")).value();
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
        const TimerInfo timer = engine->create(1, QStringLiteral("bread")).value();
        fakeNow += 2 * 1000; // the deadline has now passed
        engine->checkExpiredTimers();
        QVERIFY(engine->hasPendingAlarms());

        engine.reset();
        resetEngine(); // reload from disk

        // The fired alarm is not active, but is still pending and replayed to the host.
        QCOMPARE(engine->list().size(), 0);
        QVERIFY(engine->hasPendingAlarms());
        QCOMPARE(engine->pendingAlarms().size(), 1);
        QCOMPARE(engine->pendingAlarms().constFirst().id, timer.id);
        QSignalSpy replaySpy(engine.data(), &RemindmeEngine::timerExpired);
        engine->replayPendingAlarms();
        QCOMPARE(replaySpy.count(), 1);
        const auto replayed = replaySpy.constFirst().constFirst().value<TimerInfo>();
        QCOMPARE(replayed.id, timer.id);
        QCOMPARE(replayed.message, QStringLiteral("bread"));
        QCOMPARE(replayed.deadline, timer.deadline);
    }

    void testAcknowledgingFiredAlarmClearsIt()
    {
        const TimerInfo timer = engine->create(1, QStringLiteral("bread")).value();
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
        const TimerInfo timer = engine->create(10 * 60, QStringLiteral("meeting")).value();
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
        const auto timer = spy.takeFirst().constFirst().value<TimerInfo>();
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

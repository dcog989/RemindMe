#include "remindmeadaptor.h"
#include "remindmeengine.h"
#include "testdeadlinescheduler.h"

#include <QScopedPointer>
#include <QTemporaryDir>
#include <QTest>

#include <KLocalizedString>

using namespace Remindme;

// Exercises the runner protocol without the bus: Match()/Run()/Config() are plain calls that only
// need an engine, so the match grammar and the id round trip are checked here instead of through
// the slower end-to-end subprocess test.
class RemindmeAdapterTest : public QObject
{
    Q_OBJECT

    QScopedPointer<QTemporaryDir> tempDir;
    QScopedPointer<RemindmeEngine> engine;
    // The engine only borrows an injected scheduler, so the test must keep (and free) it.
    QScopedPointer<TestDeadlineScheduler> scheduler;
    RemindmeAdaptor *adaptor = nullptr;
    qint64 fakeNow = 0;

private Q_SLOTS:
    void initTestCase()
    {
        // A domain with no installed catalog, so i18n returns the source strings the assertions
        // spell out (the real domain would translate them on a non-English desktop).
        KLocalizedString::setApplicationDomain(QByteArrayLiteral("remindme-autotest"));
    }

    void init()
    {
        tempDir.reset(new QTemporaryDir);
        QVERIFY(tempDir->isValid());
        fakeNow = 0;
        scheduler.reset(new TestDeadlineScheduler([this]() {
            return fakeNow;
        }));
        engine.reset(new RemindmeEngine(nullptr, tempDir->filePath(QStringLiteral("timers.json")), scheduler.data()));
        // The engine owns the adaptor, so it dies with the engine.
        adaptor = new RemindmeAdaptor(engine.data());
    }

    void cleanup()
    {
        engine.reset();
        scheduler.reset();
    }

    void testTriggerWords()
    {
        QCOMPARE(adaptor->Config().value(QStringLiteral("TriggerWords")).toStringList(), (QStringList{QStringLiteral("remindme"), QStringLiteral("rme")}));

        // Only a trigger word (as a whole word) reaches the runner.
        QVERIFY(adaptor->Match(QStringLiteral("5 min")).isEmpty());
        QVERIFY(adaptor->Match(QStringLiteral("remind")).isEmpty());

        const RemoteMatches usage = adaptor->Match(QStringLiteral("remindme"));
        QVERIFY(!usage.isEmpty());
        // The first row is the list row, so a bare trigger opens the timers window; the hints
        // follow it, spelled with the alias the user typed.
        QCOMPARE(usage.constFirst().id, QStringLiteral("list"));
        for (const RemoteMatch &match : usage) {
            QVERIFY(match.id == QStringLiteral("list") || match.text.startsWith(QStringLiteral("remindme ")));
        }
    }

    void testListHintEchoesTheTypedAlias()
    {
        // The empty-state hint names the trigger the user typed, not the other alias.
        const RemoteMatches shortAlias = adaptor->Match(QStringLiteral("rme list"));
        QCOMPARE(shortAlias.size(), 1);
        QVERIFY(shortAlias.constFirst().properties.value(QStringLiteral("subtext")).toString().contains(QStringLiteral("rme")));

        const RemoteMatches longAlias = adaptor->Match(QStringLiteral("remindme list"));
        QCOMPARE(longAlias.size(), 1);
        const QString longHint = longAlias.constFirst().properties.value(QStringLiteral("subtext")).toString();
        QVERIFY(longHint.contains(QStringLiteral("remindme")));
        QVERIFY(!longHint.contains(QStringLiteral("rme")));
    }

    void testCreateRowCarriesNoAction()
    {
        const RemoteMatches matches = adaptor->Match(QStringLiteral("rme 5 pasta"));
        QCOMPARE(matches.size(), 1);
        QCOMPARE(matches.constFirst().id, QStringLiteral("create:5 pasta"));
        // No runner action: Run() must not create the timer under a stale "Cancel timer" action.
        QVERIFY(matches.constFirst().properties.value(QStringLiteral("actions")).toStringList().isEmpty());
    }

    void testRunIgnoresActionOnCreateRow()
    {
        adaptor->Run(QStringLiteral("create:5"), QStringLiteral("cancel"));
        QVERIFY(engine->list().isEmpty());

        adaptor->Run(QStringLiteral("create:5"), QString());
        QCOMPARE(engine->list().size(), 1);
    }

    void testTimerIdMustBeNumeric()
    {
        QVERIFY(engine->create(60, QStringLiteral("tea")).has_value());

        // A non-numeric remainder is ignored instead of quietly cancelling id 0.
        adaptor->Run(QStringLiteral("timer:abc"), QStringLiteral("cancel"));
        QCOMPARE(engine->list().size(), 1);

        adaptor->Run(QStringLiteral("timer:1"), QStringLiteral("cancel"));
        QVERIFY(engine->list().isEmpty());
    }

    void testRingingAlarmIsListedAndDismissable()
    {
        QVERIFY(engine->create(1, QStringLiteral("blip")).has_value());
        fakeNow += 2 * 1000;
        engine->checkExpiredTimers();
        QVERIFY(engine->hasPendingAlarms());

        // The alarm is not active, but both list views offer it as a ringing row that dismisses it.
        const RemoteMatches list = adaptor->Match(QStringLiteral("rme list"));
        QCOMPARE(list.size(), 1);
        QVERIFY(list.constFirst().text.contains(QStringLiteral("Ringing")));
        QCOMPARE(list.constFirst().id, QStringLiteral("cancel:1"));

        const RemoteMatches cancels = adaptor->Match(QStringLiteral("rme cancel"));
        QCOMPARE(cancels.size(), 1);
        QCOMPARE(cancels.constFirst().id, QStringLiteral("cancel:1"));

        adaptor->Run(cancels.constFirst().id, QString());
        QVERIFY(!engine->hasPendingAlarms());
        QCOMPARE(engine->pendingAlarms().size(), 0);

        // With nothing left, both views report the empty state again.
        const RemoteMatches empty = adaptor->Match(QStringLiteral("rme cancel"));
        QCOMPARE(empty.size(), 1);
        QVERIFY(empty.constFirst().text.contains(QStringLiteral("No timers")));
    }
};

QTEST_GUILESS_MAIN(RemindmeAdapterTest)

#include "remindmeadaptertest.moc"

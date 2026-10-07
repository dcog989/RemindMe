#include <KRunner/AbstractRunnerTest>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantMap>

using namespace KRunner;

// End-to-end test of the single-process runner: a real krunner-remindme subprocess on a private
// session bus, loaded through the DBus-runner metadata and driven by RunnerManager.
class RemindmeIntegrationTest : public KRunner::AbstractRunnerTest
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> stateDir;
    QProcess *runnerProcess = nullptr;
    QProcess *dbusDaemon = nullptr;

    static QString serviceName()
    {
        // Must match data/plasma-runner-remindme.desktop and src/engine/main.cpp
        return QStringLiteral("io.github.dcog989.remindme");
    }

    static QRegularExpression timerIdRegex()
    {
        return QRegularExpression(QStringLiteral("^#(\\d+) "));
    }

    // Reset before the query so an identical term is re-run (RunnerManager::launchQuery returns
    // early when the term did not change).
    QList<QueryMatch> query(const QString &term)
    {
        manager->reset();
        return launchQuery(term);
    }

    QList<QueryMatch> listTimers()
    {
        QList<QueryMatch> timers;
        const QList<QueryMatch> matches = query(QStringLiteral("rme list"));
        for (const QueryMatch &match : matches) {
            if (timerIdRegex().match(match.text()).hasMatch()) {
                timers.append(match);
            }
        }
        return timers;
    }

    int activeTimerCount()
    {
        return listTimers().size();
    }

    // Starts a timer through the runner and returns its engine id.
    int createTimer(const QString &term, const QString &message = QString())
    {
        const int before = activeTimerCount();
        QString q = QStringLiteral("rme ") + term;
        if (!message.isEmpty()) {
            q += QLatin1Char(' ') + message;
        }
        const QList<QueryMatch> matches = query(q);
        if (matches.isEmpty()) {
            return -1;
        }
        manager->run(matches.constFirst());

        // Run() is dispatched without blocking; wait for the engine to report the new timer.
        // No QTest macros here: they expand to a bare `return`, which is invalid in an int function.
        QElapsedTimer timer;
        timer.start();
        while (activeTimerCount() != before + 1 && timer.elapsed() < 5000) {
            QTest::qWait(50);
        }
        if (activeTimerCount() != before + 1) {
            return -1;
        }

        int maxId = -1;
        for (const QueryMatch &match : listTimers()) {
            const QRegularExpressionMatch idMatch = timerIdRegex().match(match.text());
            if (idMatch.hasMatch()) {
                maxId = qMax(maxId, idMatch.captured(1).toInt());
            }
        }
        return maxId;
    }

    void killRunner()
    {
        const QDBusReply<uint> pidReply = QDBusConnection::sessionBus().interface()->servicePid(serviceName());
        if (pidReply.isValid() && pidReply.value() != 0) {
            QProcess::execute(QStringLiteral("kill"), {QString::number(pidReply.value())});
        }
        if (runnerProcess && runnerProcess->state() != QProcess::NotRunning) {
            runnerProcess->kill();
            runnerProcess->waitForFinished();
        }
    }

private Q_SLOTS:
    void initTestCase()
    {
        stateDir.reset(new QTemporaryDir);
        QVERIFY(stateDir->isValid());

        // Run on a private session bus so the test neither depends on nor interferes with the
        // user's real one. Install a service file pointing at the build-tree binary so the bus
        // can re-activate the runner after it idles out between tests.
        QDir().mkpath(stateDir->path() + QStringLiteral("/dbus-1/services"));
        QFile serviceFile(stateDir->path() + QStringLiteral("/dbus-1/services/") + serviceName() + QStringLiteral(".service"));
        QVERIFY(serviceFile.open(QIODevice::WriteOnly));
        const QString content = QStringLiteral("[D-BUS Service]\nName=") + serviceName() + QStringLiteral("\nExec=")
            + QStringLiteral(KRUNNER_TEST_DBUS_EXECUTABLE) + QLatin1Char('\n');
        serviceFile.write(content.toUtf8());
        serviceFile.close();

        dbusDaemon = new QProcess(this);
        QProcessEnvironment daemonEnv = QProcessEnvironment::systemEnvironment();
        daemonEnv.insert(QStringLiteral("XDG_DATA_HOME"), stateDir->path());
        daemonEnv.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        dbusDaemon->setProcessEnvironment(daemonEnv);
        dbusDaemon->start(QStringLiteral("dbus-daemon"), {QStringLiteral("--session"), QStringLiteral("--print-address")});
        QVERIFY(dbusDaemon->waitForStarted());

        // dbus-daemon prints the bus address on stdout once it is listening
        QEventLoop loop;
        QString busAddress;
        connect(dbusDaemon, &QProcess::readyReadStandardOutput, &loop, [&loop, &busAddress, this]() {
            busAddress = QString::fromUtf8(dbusDaemon->readAllStandardOutput()).trimmed();
            if (!busAddress.isEmpty()) {
                loop.quit();
            }
        });
        connect(dbusDaemon, &QProcess::errorOccurred, &loop, &QEventLoop::quit);
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        QVERIFY2(!busAddress.isEmpty(), "dbus-daemon did not provide a bus address within 5 seconds");

        qputenv("DBUS_SESSION_BUS_ADDRESS", busAddress.toUtf8());
        qputenv("XDG_DATA_HOME", stateDir->path().toUtf8()); // isolate the timer state
        qputenv("QT_QPA_PLATFORM", "offscreen"); // don't pop up alarm windows during tests

        runnerProcess = new QProcess(this);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"), busAddress);
        env.insert(QStringLiteral("XDG_DATA_HOME"), stateDir->path());
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        runnerProcess->setProcessEnvironment(env);
        runnerProcess->setProcessChannelMode(QProcess::ForwardedChannels);

        QEventLoop waitLoop;
        QTimer pollTimer;
        pollTimer.setInterval(100);
        connect(&pollTimer, &QTimer::timeout, &waitLoop, [&waitLoop]() {
            if (QDBusConnection::sessionBus().interface()->isServiceRegistered(serviceName()).value()) {
                waitLoop.quit();
            }
        });
        pollTimer.start();
        QTimer::singleShot(10000, &waitLoop, &QEventLoop::quit);
        runnerProcess->start(QStringLiteral(KRUNNER_TEST_DBUS_EXECUTABLE));
        waitLoop.exec();

        QVERIFY2(QDBusConnection::sessionBus().interface()->isServiceRegistered(serviceName()).value(),
                 "remindme service was not registered within 10 seconds");
        QVERIFY(runnerProcess->state() == QProcess::Running);

        // Load the runner from the DBus metadata; only it participates in this test
        initProperties();
        QCOMPARE(runner->id(), QStringLiteral("remindme"));
    }

    void cleanupTestCase()
    {
        killRunner();
        if (dbusDaemon) {
            dbusDaemon->kill();
            dbusDaemon->waitForFinished();
        }
    }

    void cleanup()
    {
        if (!manager) {
            return;
        }
        const QList<QueryMatch> cancelMatches = query(QStringLiteral("rme cancel"));
        for (const QueryMatch &match : cancelMatches) {
            if (match.id().contains(QLatin1String("cancel"))) {
                manager->run(match);
            }
        }
        QTRY_VERIFY(activeTimerCount() == 0);
    }

    void testUsageMatch()
    {
        // A bare trigger word lists the registered syntaxes as usage hints
        const QList<QueryMatch> matches = query(QStringLiteral("rme"));
        QVERIFY(matches.size() >= 2);
        bool sawCreate = false;
        bool sawList = false;
        for (const QueryMatch &match : matches) {
            sawCreate = sawCreate || match.text().contains(QLatin1String("rme <duration>"));
            sawList = sawList || match.text().contains(QLatin1String("rme list"));
            QVERIFY(match.text() != QLatin1String("rme")); // the trigger itself is not a result
        }
        QVERIFY(sawCreate); // the create syntax is shown, preferring the "rme" example
        QVERIFY(sawList);

        // The long alias must get the same hints in its own spelling, not fall back to "rme"
        const QList<QueryMatch> longMatches = query(QStringLiteral("remindme"));
        QVERIFY(longMatches.size() >= 2);
        for (const QueryMatch &match : longMatches) {
            QVERIFY(match.text().startsWith(QLatin1String("remindme ")));
        }
    }

    void testInvalidDurationMatch()
    {
        const QList<QueryMatch> matches = query(QStringLiteral("rme 5x"));
        QCOMPARE(matches.size(), 1);
        QVERIFY(matches.constFirst().text().contains(QLatin1String("Invalid")));

        // The match text is rendered as rich text, so markup in the term must stay escaped
        const QList<QueryMatch> markupMatches = query(QStringLiteral("rme 5x <b>"));
        QCOMPARE(markupMatches.size(), 1);
        QVERIFY(markupMatches.constFirst().text().contains(QLatin1String("&lt;b&gt;")));
        QVERIFY(!markupMatches.constFirst().text().contains(QLatin1String("<b>")));
    }

    void testCreateMatch()
    {
        const QList<QueryMatch> matches = query(QStringLiteral("rme 5 pasta is ready!"));
        QCOMPARE(matches.size(), 1);
        QVERIFY(matches.constFirst().text().contains(QLatin1String("pasta is ready!")));
        QVERIFY(matches.constFirst().subtext().isEmpty());
    }

    void testRunCreateCreatesTimer()
    {
        const int id = createTimer(QStringLiteral("0:45"), QStringLiteral("coffee"));
        QVERIFY(id >= 1);
        const QList<QueryMatch> timers = listTimers();
        QCOMPARE(timers.size(), 1);
        QVERIFY(timers.constFirst().text().contains(QLatin1String("coffee")));
    }

    void testListMatch()
    {
        QVERIFY(createTimer(QStringLiteral("10:00"), QStringLiteral("hello")) >= 1);

        const QList<QueryMatch> timers = listTimers();
        QCOMPARE(timers.size(), 1);
        QVERIFY(timers.constFirst().text().contains(QLatin1String("hello"))); // all details in the text line
        QVERIFY(timers.constFirst().subtext().isEmpty());
        QCOMPARE(timers.constFirst().actions().size(), 1);
        QCOMPARE(timers.constFirst().actions().constFirst().id(), QStringLiteral("cancel"));
    }

    void testCancelMatch()
    {
        const int id = createTimer(QStringLiteral("10:00"));
        QVERIFY(id >= 1);

        const QList<QueryMatch> matches = query(QStringLiteral("rme cancel %1").arg(id));
        QCOMPARE(matches.size(), 1);
        QVERIFY(matches.constFirst().text().contains(QStringLiteral("#%1").arg(id)));

        manager->run(matches.constFirst());
        QTRY_COMPARE(activeTimerCount(), 0);
    }

    void testCancelMatchWithHash()
    {
        const int id = createTimer(QStringLiteral("10:00"));
        QVERIFY(id >= 1);

        const QList<QueryMatch> matches = query(QStringLiteral("rme cancel #%1").arg(id));
        QCOMPARE(matches.size(), 1);
        QVERIFY(matches.constFirst().text().contains(QStringLiteral("#%1").arg(id)));

        manager->run(matches.constFirst());
        QTRY_COMPARE(activeTimerCount(), 0);
    }

    void testBareCancelListsTimers()
    {
        const int first = createTimer(QStringLiteral("10:00"), QStringLiteral("one"));
        const int second = createTimer(QStringLiteral("10:00"), QStringLiteral("two"));
        QVERIFY(first >= 1);
        QVERIFY(second >= 1);

        const QList<QueryMatch> matches = query(QStringLiteral("rme cancel"));
        QCOMPARE(matches.size(), 2);
        for (const QueryMatch &match : matches) {
            QVERIFY(match.subtext().contains(QLatin1String("Cancel this timer")));
        }

        manager->run(matches.constFirst());
        QTRY_COMPARE(activeTimerCount(), 1);
    }

    void testCancelByMessageMatch()
    {
        QVERIFY(createTimer(QStringLiteral("10:00"), QStringLiteral("meeting")) >= 1);
        QVERIFY(createTimer(QStringLiteral("10:00"), QStringLiteral("meeting")) >= 1);
        QVERIFY(createTimer(QStringLiteral("10:00"), QStringLiteral("lunch")) >= 1);

        const QList<QueryMatch> matches = query(QStringLiteral("rme cancel meeting"));
        QCOMPARE(matches.size(), 1);
        QCOMPARE(matches.constFirst().text(), QStringLiteral("Cancel all timers matching \"meeting\""));

        // Matching is a case-insensitive substring, so a partial term still matches
        const QList<QueryMatch> partial = query(QStringLiteral("rme cancel MEET"));
        QCOMPARE(partial.size(), 1);
        QCOMPARE(partial.constFirst().text(), QStringLiteral("Cancel all timers matching \"MEET\""));

        manager->run(matches.constFirst());
        QTRY_COMPARE(activeTimerCount(), 1);
        QVERIFY(listTimers().constFirst().text().contains(QLatin1String("lunch")));

        // A term that matches no timer is reported instead of offering a no-op cancellation
        const QList<QueryMatch> noMatch = query(QStringLiteral("rme cancel nomatch"));
        QCOMPARE(noMatch.size(), 1);
        QVERIFY(noMatch.constFirst().text().contains(QLatin1String("No timer matching")));

        // Markup in the message term is escaped for the rich-text rendered match text
        const QList<QueryMatch> markupMatches = query(QStringLiteral("rme cancel <b>meeting</b>"));
        QCOMPARE(markupMatches.size(), 1);
        QVERIFY(markupMatches.constFirst().text().contains(QLatin1String("&lt;b&gt;meeting&lt;/b&gt;")));
    }

    void testNumericCancelValidatesId()
    {
        // A numeric term is read as an id; a nonexistent id warns instead of offering to cancel
        const QList<QueryMatch> missing = query(QStringLiteral("rme cancel 5"));
        QCOMPARE(missing.size(), 1);
        QVERIFY(missing.constFirst().text().contains(QLatin1String("No timer with id 5")));

        // "#" marks an id, so a non-numeric "#foo" is a missing id rather than a message search
        const QList<QueryMatch> hashText = query(QStringLiteral("rme cancel #foo"));
        QCOMPARE(hashText.size(), 1);
        QVERIFY(hashText.constFirst().text().contains(QLatin1String("No timer with id foo")));

        // An existing id still offers cancellation, marked with "#"
        const int id = createTimer(QStringLiteral("10:00"), QStringLiteral("5"));
        QVERIFY(id >= 1);
        const QList<QueryMatch> matches = query(QStringLiteral("rme cancel %1").arg(id));
        QCOMPARE(matches.size(), 1);
        QCOMPARE(matches.constFirst().text(), QStringLiteral("Cancel timer #%1").arg(id));
        QVERIFY(matches.constFirst().subtext().isEmpty());

        // A non-numeric term is read as a message and must match a timer to be offered
        const QList<QueryMatch> noMatch = query(QStringLiteral("rme cancel 5min"));
        QCOMPARE(noMatch.size(), 1);
        QVERIFY(noMatch.constFirst().text().contains(QLatin1String("No timer matching")));

        QVERIFY(createTimer(QStringLiteral("10:00"), QStringLiteral("5min left")) >= 1);
        const QList<QueryMatch> messageMatch = query(QStringLiteral("rme cancel 5min"));
        QCOMPARE(messageMatch.size(), 1);
        QCOMPARE(messageMatch.constFirst().text(), QStringLiteral("Cancel all timers matching \"5min\""));
    }

    void testListShowsReminder()
    {
        QVERIFY(createTimer(QStringLiteral("10:00"), QStringLiteral("first")) >= 1);

        const QList<QueryMatch> timers = listTimers();
        QCOMPARE(timers.size(), 1);
        manager->run(timers.constFirst()); // default action shows the reminder, it does not cancel
        QTest::qWait(100);
        QCOMPARE(activeTimerCount(), 1);
    }

    void testListCancelAction()
    {
        QVERIFY(createTimer(QStringLiteral("10:00"), QStringLiteral("second")) >= 1);

        const QList<QueryMatch> timers = listTimers();
        QCOMPARE(timers.size(), 1);
        QCOMPARE(timers.constFirst().actions().size(), 1);
        manager->run(timers.constFirst(), timers.constFirst().actions().constFirst());
        QTRY_COMPARE(activeTimerCount(), 0);
    }

    void testExpiryEndToEnd()
    {
        QVERIFY(createTimer(QStringLiteral("0:01"), QStringLiteral("blip")) >= 1);

        // The expired timer is removed by the engine and the alarm is shown/played headlessly
        QTRY_COMPARE_WITH_TIMEOUT(activeTimerCount(), 0, 5000);
    }
};

QTEST_GUILESS_MAIN(RemindmeIntegrationTest)

#include "remindmeintegrationtest.moc"

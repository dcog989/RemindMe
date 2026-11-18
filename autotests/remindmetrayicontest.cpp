#include "remindmetrayicon.h"
#include "remindmeengine.h"
#include "remindmetime.h"

#include <QDateTime>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QTest>

// The tray icon is the second way into the reminder overview: it must appear exactly while timers
// wait and name the next deadline. This runs on the "minimal" platform (see autotests/CMakeLists.txt),
// where QSystemTrayIcon keeps its own visible flag without a tray host.
class RemindmeTrayIconTest : public QObject
{
    Q_OBJECT

    static QString formattedDeadline(const Remindme::TimerInfo &timer)
    {
        return Remindme::formatReminderTime(QDateTime::fromMSecsSinceEpoch(timer.deadline), false);
    }

private Q_SLOTS:
    void testIconTracksTheTimerSet()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        RemindmeEngine engine(nullptr, tempDir.filePath(QStringLiteral("timers.json")));
        RemindmeTrayIcon tray(engine);
        auto *icon = tray.findChild<QSystemTrayIcon *>();
        QVERIFY(icon);

        // Nothing waiting: no icon.
        QVERIFY(!icon->isVisible());

        const auto created = engine.create(60, QStringLiteral("tea"));
        QVERIFY(created.has_value());
        QVERIFY(icon->isVisible());
        QVERIFY(icon->toolTip().contains(QStringLiteral("Next reminder")));

        // The last timer going away hides it again; the icon holds no state of its own.
        QVERIFY(engine.cancel(created->id));
        QVERIFY(!icon->isVisible());
    }

    void testTooltipNamesTheNextDeadline()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        RemindmeEngine engine(nullptr, tempDir.filePath(QStringLiteral("timers.json")));
        RemindmeTrayIcon tray(engine);
        auto *icon = tray.findChild<QSystemTrayIcon *>();
        QVERIFY(icon);

        const auto first = engine.create(2 * 60, QStringLiteral("first"));
        const auto second = engine.create(30 * 60, QStringLiteral("second"));
        QVERIFY(first.has_value());
        QVERIFY(second.has_value());
        QVERIFY(icon->toolTip().contains(formattedDeadline(*first)));

        // Removing the soonest timer must move the tooltip to the one still waiting.
        QVERIFY(engine.cancel(first->id));
        QVERIFY(icon->toolTip().contains(formattedDeadline(*second)));
    }
};

QTEST_MAIN(RemindmeTrayIconTest)

#include "remindmetrayicontest.moc"

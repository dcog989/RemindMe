#include "remindmetrayicon.h"
#include "remindmeengine.h"

#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QTest>

// The tray icon is the second way into the reminder overview: it must appear exactly while timers
// wait and name the next deadline. This runs on the "minimal" platform (see autotests/CMakeLists.txt),
// where QSystemTrayIcon keeps its own visible flag without a tray host.
class RemindmeTrayIconTest : public QObject
{
    Q_OBJECT

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
};

QTEST_MAIN(RemindmeTrayIconTest)

#include "remindmetrayicontest.moc"

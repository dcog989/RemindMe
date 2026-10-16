#include "remindmealarmpresenter.h"
#include "remindmealarmdialog.h"
#include "remindmeengine.h"

#include <QApplication>
#include <QList>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWidget>

using namespace Remindme;

// The presenter decides the process's right to exit from its open-window and in-flight-sound
// counts: a wrong count either drops a live alarm or keeps the process alive forever. This drives
// the windowed path (the sound path needs a notification service), so the test runs on a platform
// that can create widgets without a display (see QT_QPA_PLATFORM in autotests/CMakeLists.txt).
class RemindmeAlarmPresenterTest : public QObject
{
    Q_OBJECT

    static QList<RemindmeAlarmDialog *> visibleDialogs()
    {
        QList<RemindmeAlarmDialog *> dialogs;
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            auto *dialog = qobject_cast<RemindmeAlarmDialog *>(widget);
            if (dialog && dialog->isVisible()) {
                dialogs.append(dialog);
            }
        }
        return dialogs;
    }

    static TimerInfo timer(int id, const QString &message)
    {
        return TimerInfo{.id = id, .deadline = 0, .message = message};
    }

private Q_SLOTS:
    void testWindowLifetimeDrivesIdleState()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        RemindmeEngine engine(nullptr, tempDir.filePath(QStringLiteral("timers.json")));
        AlarmPresenter presenter(engine);
        QSignalSpy finishedSpy(&presenter, &AlarmPresenter::presentationsFinished);

        QVERIFY(!presenter.isPresenting());

        presenter.present(timer(1, QStringLiteral("tea")), AlarmKind::Reminder);
        QVERIFY(presenter.isPresenting());
        QCOMPARE(finishedSpy.count(), 0); // not idle while the window is open

        delete visibleDialogs().constFirst();

        // The host may exit again once the window is gone, and is told exactly once.
        QVERIFY(!presenter.isPresenting());
        QCOMPARE(finishedSpy.count(), 1);
    }

    void testFinishOnlyAfterTheLastWindowCloses()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        RemindmeEngine engine(nullptr, tempDir.filePath(QStringLiteral("timers.json")));
        AlarmPresenter presenter(engine);
        QSignalSpy finishedSpy(&presenter, &AlarmPresenter::presentationsFinished);

        presenter.present(timer(1, QStringLiteral("one")), AlarmKind::Reminder);
        presenter.present(timer(2, QStringLiteral("two")), AlarmKind::Reminder);
        const QList<RemindmeAlarmDialog *> dialogs = visibleDialogs();
        QCOMPARE(dialogs.size(), 2);

        delete dialogs.at(0);
        QVERIFY(presenter.isPresenting()); // one window still open
        QCOMPARE(finishedSpy.count(), 0);

        delete dialogs.at(1);
        QVERIFY(!presenter.isPresenting());
        QCOMPARE(finishedSpy.count(), 1);
    }
};

QTEST_MAIN(RemindmeAlarmPresenterTest)

#include "remindmealarmpresentertest.moc"

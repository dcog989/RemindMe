#include "remindmealarmpresenter.h"
#include "remindmealarmdialog.h"
#include "remindmeengine.h"
#include "remindmereminderlistdialog.h"

#include <QApplication>
#include <QLabel>
#include <QList>
#include <QPushButton>
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

    template<typename T>
    static QList<T *> visibleDialogs()
    {
        QList<T *> dialogs;
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            auto *dialog = qobject_cast<T *>(widget);
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

        presenter.presentExpiry(timer(1, QStringLiteral("tea")));
        QVERIFY(presenter.isPresenting());
        QCOMPARE(finishedSpy.count(), 0); // not idle while the window is open

        delete visibleDialogs<RemindmeAlarmDialog>().constFirst();

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

        presenter.presentExpiry(timer(1, QStringLiteral("one")));
        presenter.presentExpiry(timer(2, QStringLiteral("two")));
        const QList<RemindmeAlarmDialog *> dialogs = visibleDialogs<RemindmeAlarmDialog>();
        QCOMPARE(dialogs.size(), 2);

        delete dialogs.at(0);
        QVERIFY(presenter.isPresenting()); // one window still open
        QCOMPARE(finishedSpy.count(), 0);

        delete dialogs.at(1);
        QVERIFY(!presenter.isPresenting());
        QCOMPARE(finishedSpy.count(), 1);
    }

    void testReminderOverviewShowsEveryTimer()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        RemindmeEngine engine(nullptr, tempDir.filePath(QStringLiteral("timers.json")));
        AlarmPresenter presenter(engine);

        presenter.presentReminders({timer(1, QStringLiteral("one")), timer(2, QStringLiteral("two")), timer(3, QStringLiteral("three"))});

        // One window for the whole set, with one cancel button per timer.
        const QList<ReminderListDialog *> dialogs = visibleDialogs<ReminderListDialog>();
        QCOMPARE(dialogs.size(), 1);
        ReminderListDialog *dialog = dialogs.constFirst();
        QCOMPARE(dialog->findChildren<QPushButton *>().size(), 3);
        // The height is fixed to the rows, so it can never be dragged into KWin's 150 px floor.
        QCOMPARE(dialog->minimumHeight(), dialog->maximumHeight());
        QCOMPARE(dialog->height(), dialog->minimumHeight());

        // Acting on a timer drops its row; the overview stays open for the rest.
        for (QPushButton *button : dialog->findChildren<QPushButton *>()) {
            if (button->text() == QStringLiteral("Cancel timer")) {
                button->click();
                break;
            }
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(dialog->findChildren<QPushButton *>().size(), 2);
        QVERIFY(presenter.isPresenting());
    }

    void testReminderOverviewReportsNoTimers()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        RemindmeEngine engine(nullptr, tempDir.filePath(QStringLiteral("timers.json")));
        AlarmPresenter presenter(engine);

        // "rme list" with nothing active still opens the overview, which says so.
        presenter.presentReminders({});
        const QList<ReminderListDialog *> dialogs = visibleDialogs<ReminderListDialog>();
        QCOMPARE(dialogs.size(), 1);
        bool sawEmptyText = false;
        for (const QLabel *label : dialogs.constFirst()->findChildren<QLabel *>()) {
            sawEmptyText = sawEmptyText || label->text() == QStringLiteral("No active timers.");
        }
        QVERIFY(sawEmptyText);
        QVERIFY(presenter.isPresenting());
    }

    // A failed assertion would otherwise leave a window behind for the next test to find.
    void cleanup()
    {
        for (ReminderListDialog *dialog : visibleDialogs<ReminderListDialog>()) {
            delete dialog;
        }
        for (RemindmeAlarmDialog *dialog : visibleDialogs<RemindmeAlarmDialog>()) {
            delete dialog;
        }
    }
};

QTEST_MAIN(RemindmeAlarmPresenterTest)

#include "remindmealarmpresentertest.moc"

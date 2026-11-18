#include "remindmealarmpresenter.h"
#include "remindmealarmdialog.h"
#include "remindmealarmnotification.h"
#include "remindmeengine.h"
#include "remindmereminderlistdialog.h"

#include <KConfig>

#include <QApplication>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWidget>

#include <memory>

using namespace Remindme;

// The presenter decides the process's right to exit from its open-window and in-flight-sound
// counts: a wrong count either drops a live alarm or keeps the process alive forever. This drives
// the windowed path (the sound path needs a notification service), so the test runs on a platform
// that can create widgets without a display (see QT_QPA_PLATFORM in autotests/CMakeLists.txt).
class RemindmeAlarmPresenterTest : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> tempDir;
    std::unique_ptr<RemindmeEngine> engine;
    std::unique_ptr<AlarmPresenter> presenter;

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

    static void clickButton(QWidget *window, const QString &text)
    {
        for (QPushButton *button : window->findChildren<QPushButton *>()) {
            if (button->text() == text) {
                button->click();
                return;
            }
        }
    }

private Q_SLOTS:
    void init()
    {
        tempDir = std::make_unique<QTemporaryDir>();
        QVERIFY(tempDir->isValid());
        engine = std::make_unique<RemindmeEngine>(nullptr, tempDir->filePath(QStringLiteral("timers.json")));
        presenter = std::make_unique<AlarmPresenter>(*engine);
    }

    void testWindowLifetimeDrivesIdleState()
    {
        QSignalSpy finishedSpy(presenter.get(), &AlarmPresenter::presentationsFinished);

        QVERIFY(!presenter->isPresenting());

        presenter->presentExpiry(timer(1, QStringLiteral("tea")));
        QVERIFY(presenter->isPresenting());
        QCOMPARE(finishedSpy.count(), 0); // not idle while the window is open

        delete visibleDialogs<RemindmeAlarmDialog>().constFirst();

        // The host may exit again once the window is gone, and is told exactly once.
        QVERIFY(!presenter->isPresenting());
        QCOMPARE(finishedSpy.count(), 1);
    }

    void testFinishOnlyAfterTheLastWindowCloses()
    {
        QSignalSpy finishedSpy(presenter.get(), &AlarmPresenter::presentationsFinished);

        presenter->presentExpiry(timer(1, QStringLiteral("one")));
        presenter->presentExpiry(timer(2, QStringLiteral("two")));
        const QList<RemindmeAlarmDialog *> dialogs = visibleDialogs<RemindmeAlarmDialog>();
        QCOMPARE(dialogs.size(), 2);

        delete dialogs.at(0);
        QVERIFY(presenter->isPresenting()); // one window still open
        QCOMPARE(finishedSpy.count(), 0);

        delete dialogs.at(1);
        QVERIFY(!presenter->isPresenting());
        QCOMPARE(finishedSpy.count(), 1);
    }

    void testReminderOverviewShowsEveryTimer()
    {
        presenter->presentReminders({timer(1, QStringLiteral("one")), timer(2, QStringLiteral("two")), timer(3, QStringLiteral("three"))});

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
        QVERIFY(presenter->isPresenting());
    }

    void testCancelFromTheEngineClosesTheAlarmWindow()
    {
        QVERIFY(engine->create(60, QStringLiteral("tea")).has_value());
        presenter->presentExpiry(timer(1, QStringLiteral("tea")));
        QCOMPARE(visibleDialogs<RemindmeAlarmDialog>().size(), 1);

        // "rme cancel 1": the engine drops the entry, so the window must go with it.
        QVERIFY(engine->cancel(1));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(visibleDialogs<RemindmeAlarmDialog>().size(), 0);
        QVERIFY(!presenter->isPresenting());
        QVERIFY(!engine->contains(1));
    }

    void testReplacingAnAlarmKeepsItsTimer()
    {
        QVERIFY(engine->create(60, QStringLiteral("tea")).has_value());
        presenter->presentExpiry(timer(1, QStringLiteral("tea")));
        presenter->presentExpiry(timer(1, QStringLiteral("tea")));

        // Replacing the window must not dismiss the timer it presents.
        QVERIFY(engine->contains(1));
        QCOMPARE(visibleDialogs<RemindmeAlarmDialog>().size(), 1);
    }

    void testReminderOverviewWidthIsCapped()
    {
        // The window is fixed-size, so a long message must not widen it past the screen.
        presenter->presentReminders({timer(1, QString(500, QLatin1Char('x')))});

        const QList<ReminderListDialog *> dialogs = visibleDialogs<ReminderListDialog>();
        QCOMPARE(dialogs.size(), 1);
        QVERIFY(dialogs.constFirst()->width() <= Remindme::dialogMaxWidth);
    }

    void testReminderOverviewReportsNoTimers()
    {
        // "rme list" with nothing active still opens the overview, which says so.
        presenter->presentReminders({});
        const QList<ReminderListDialog *> dialogs = visibleDialogs<ReminderListDialog>();
        QCOMPARE(dialogs.size(), 1);
        bool sawEmptyText = false;
        for (const QLabel *label : dialogs.constFirst()->findChildren<QLabel *>()) {
            sawEmptyText = sawEmptyText || label->text() == QStringLiteral("No active timers.");
        }
        QVERIFY(sawEmptyText);
        QVERIFY(presenter->isPresenting());
    }

    void testLongMessageStaysOnScreen()
    {
        presenter->presentExpiry(timer(1, QStringLiteral("word ").repeated(2000)));

        // The text column scrolls once it would exceed a third of the screen, so a long message
        // can never make the fixed-size window taller than the display.
        const QList<RemindmeAlarmDialog *> dialogs = visibleDialogs<RemindmeAlarmDialog>();
        QCOMPARE(dialogs.size(), 1);
        QVERIFY(dialogs.constFirst()->height() <= QGuiApplication::primaryScreen()->availableGeometry().height());
    }

    void testLongUnbrokenMessageIsCapped()
    {
        // A single token cannot wrap: the fixed width must still cap it (the tooltip carries the
        // full text).
        presenter->presentExpiry(timer(1, QString(2000, QLatin1Char('x'))));

        const QList<RemindmeAlarmDialog *> dialogs = visibleDialogs<RemindmeAlarmDialog>();
        QCOMPARE(dialogs.size(), 1);
        QVERIFY(dialogs.constFirst()->width() <= Remindme::dialogMaxWidth);
        QVERIFY(dialogs.constFirst()->height() <= QGuiApplication::primaryScreen()->availableGeometry().height());
    }

    void testDismissButtonAcknowledgesTheAlarm()
    {
        const auto created = engine->create(60, QStringLiteral("tea"));
        QVERIFY(created.has_value());
        presenter->presentExpiry(*created);

        clickButton(visibleDialogs<RemindmeAlarmDialog>().constFirst(), QStringLiteral("Dismiss"));

        // Dismiss is an acknowledgement: the entry leaves the engine, so it is not replayed.
        QVERIFY(!engine->contains(created->id));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!presenter->isPresenting());
    }

    void testClosingTheAlarmAcknowledgesIt()
    {
        const auto created = engine->create(60, QStringLiteral("tea"));
        QVERIFY(created.has_value());
        presenter->presentExpiry(*created);

        // X or Esc closes the window; the alarm must not stay pending behind it.
        visibleDialogs<RemindmeAlarmDialog>().constFirst()->reject();

        QVERIFY(!engine->contains(created->id));
    }

    void testSnoozeKeepsTheTimer()
    {
        const auto created = engine->create(60, QStringLiteral("tea"));
        QVERIFY(created.has_value());
        presenter->presentExpiry(*created);

        clickButton(visibleDialogs<RemindmeAlarmDialog>().constFirst(), QStringLiteral("Snooze"));

        const QList<TimerInfo> timers = engine->list();
        QCOMPARE(timers.size(), 1);
        QVERIFY(timers.constFirst().deadline > created->deadline);
    }

    void testNotifyrcDefinesTheAlarmEvent()
    {
        // The event id lives in the code and in the installed notifyrc. A rename in one of them
        // makes KNotification fall back to an event with no configured sound.
        KConfig notifyrc(QStringLiteral(REMINDME_NOTIFYRC_PATH), KConfig::SimpleConfig);
        QVERIFY(notifyrc.hasGroup(QStringLiteral("Event/%1").arg(QString::fromLatin1(Remindme::alarmEventId))));
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
        presenter.reset();
        engine.reset();
        tempDir.reset();
    }
};

QTEST_MAIN(RemindmeAlarmPresenterTest)

#include "remindmealarmpresentertest.moc"

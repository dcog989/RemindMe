#include <QApplication>
#include <QDBusConnection>
#include <QGuiApplication>
#include <QTimer>

#include <KLocalizedString>
#include <KNotification>

#include <memory>

#include "remindmeadaptor.h"
#include "remindmealarmdialog.h"
#include "remindmeautostart.h"
#include "remindmeengine.h"

namespace
{
// The D-Bus service KRunner auto-activates and talks to; must match the service file and the
// .desktop X-Plasma-DBusRunner-Service.
const QString s_serviceName = QStringLiteral("io.github.dcog989.remindme");

// Grace period for the idle-activation fallback below: long enough for any pending D-Bus call
// to arrive before the process gives up and exits.
constexpr int idleQuitFallbackMs = 30 * 1000;
}

int main(int argc, char **argv)
{
    // Allow the runner to start in headless environments, where it degrades to the
    // sound-only alarm instead of failing to create a window.
    if (qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    QApplication app(argc, argv);
    // Deliberately no organizationName: QStandardPaths::AppDataLocation must stay
    // ~/.local/share/krunner-remindme so existing timers.json keeps being found.
    app.setApplicationName(QStringLiteral("krunner-remindme"));
    app.setOrganizationDomain(QStringLiteral("io.github.dcog989"));
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("krunner_remindme"));
    // Windowed, but closing the last alarm dialog must not quit the process and drop the
    // remaining timers; the process only exits once the engine reports nothing left to do.
    app.setQuitOnLastWindowClosed(false);

    // Single-instance guard, before any side effects: constructing the engine may load state and
    // back up a corrupt file, and autostart writes a .desktop entry. If another helper already
    // owns the name, exit without touching either.
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qWarning() << "No D-Bus session bus available; cannot start" << s_serviceName;
        return 1;
    }
    if (!bus.registerService(s_serviceName)) {
        qWarning() << "Another instance already owns" << s_serviceName << "; exiting";
        return 1;
    }

    RemindmeEngine engine;

    // Enable the autostart entry while a timer or an unacknowledged alarm exists so it is
    // relaunched next login, and disable it once none are left so an idle helper is not started
    // at every login. The initial call reconciles a leftover enabled entry after a hard shutdown
    // that left no timer behind.
    auto autostart = std::make_unique<RemindmeAutostart>();
    autostart->setActive(!engine.List().isEmpty() || engine.hasPendingAlarms());
    QObject::connect(&engine, &RemindmeEngine::timerCountChanged, &engine, [&autostart](int timerCount) {
        autostart->setActive(timerCount > 0);
    });

    // Alarm windows currently open. The helper may only exit while this is zero: an expired
    // timer was already moved to the engine's fired set, so on an expiry alarm the window alone
    // keeps the process alive until the user snoozes or dismisses it.
    int openDialogCount = 0;

    // Headless sound notifications still in flight. Like a dialog, the alarm was already moved to
    // the engine's fired set, so without this the process would quit and tear down the bus before
    // the asynchronous notification is delivered. Held until KNotification reports it closed.
    int pendingSoundCount = 0;

    // Exit when there is nothing left to do; D-Bus activation restarts the helper on demand.
    // Drop the service name first so a call racing the exit is routed to a freshly activated
    // helper instead of this process's torn-down object tree.
    const auto tryQuit = [&]() {
        if (engine.List().isEmpty() && openDialogCount == 0 && pendingSoundCount == 0) {
            QDBusConnection::sessionBus().unregisterService(s_serviceName);
            app.quit();
        }
    };

    // Show an alarm dialog (or, where no window is possible, a one-shot sound) and keep it
    // tracked so tryQuit() never ends the process over an alarm the user is still acting on.
    const auto showAlarmDialog = [&](const Remindme::TimerInfo &timer, bool expired) {
        // Headless contexts (e.g. CI, no desktop) cannot show a window. An expired timer was
        // already removed by the engine, so the alarm degrades to a single sound notification;
        // a manually requested reminder is simply a no-op.
        if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
            if (expired) {
                KNotification *sound = new KNotification(QStringLiteral("remindme"), KNotification::CloseOnTimeout, &engine);
                sound->setTitle(i18n("Timer finished"));
                sound->setText(timer.message.isEmpty() ? i18n("Your timer has finished") : timer.message);
                // Count the sound before sending: sendEvent() is asynchronous, and the
                // commitTimers() count signal that reaches tryQuit() runs first.
                ++pendingSoundCount;
                // KNotification auto-deletes on close, so closed() is the last chance to react.
                QObject::connect(sound, &KNotification::closed, &app, [&pendingSoundCount, &tryQuit]() {
                    if (--pendingSoundCount == 0) {
                        tryQuit();
                    }
                });
                sound->sendEvent();
            }
            return;
        }

        // The dialog shows the message and, for the expiry alarm, plays a looping alarm sound
        // (see RemindmeAlarmDialog) until the user snoozes or dismisses it.
        auto *dialog = new RemindmeAlarmDialog(timer, expired);
        ++openDialogCount;
        QObject::connect(dialog, &RemindmeAlarmDialog::snoozeRequested, &engine, [&engine, timer, expired]() {
            if (expired) {
                engine.rearmExpiredTimer(timer);
            } else {
                engine.Snooze(timer.id);
            }
        });
        // "Cancel timer" (still-active) or "Dismiss" (expired) clears the engine entry; Cancel
        // handles both. Closing the window without a button is a no-op.
        QObject::connect(dialog, &RemindmeAlarmDialog::dismissRequested, &engine, [&engine, timer]() {
            engine.Cancel(timer.id);
        });
        // The window is WA_DeleteOnClose, so closing it may be the last thing holding the
        // process up (e.g. a cancel-by-dismissal leaves no timers); re-check idleness.
        QObject::connect(dialog, &QObject::destroyed, &app, [&app, &openDialogCount, &tryQuit]() {
            if (--openDialogCount == 0) {
                tryQuit();
            }
        });
        dialog->show();
        // This process runs in the background, so the dialog could otherwise map behind the
        // active window (especially on Wayland); an alarm you cannot see defeats the purpose.
        dialog->raise();
        dialog->activateWindow();
    };

    QObject::connect(&engine, &RemindmeEngine::timerExpired, &engine, [&showAlarmDialog](const Remindme::TimerInfo &timer) {
        showAlarmDialog(timer, /*expired=*/true);
    });
    QObject::connect(&engine, &RemindmeEngine::showReminderRequested, &engine, [&showAlarmDialog](const Remindme::TimerInfo &timer) {
        showAlarmDialog(timer, /*expired=*/false);
    });
    // Whenever the engine's timer set changes (create, cancel, expiry, snooze), exit when
    // nothing remains; the engine persists its full state on every change first.
    QObject::connect(&engine, &RemindmeEngine::timerCountChanged, &app, tryQuit);

    // Re-present alarms that fired before this process started (e.g. the session ended while a
    // dialog was open). The engine loaded them from the persisted "fired" entries; showing them
    // now also bumps openDialogCount/pendingSoundCount so tryQuit() cannot drop them.
    engine.replayPendingAlarms();

    new RemindmeAdaptor(&engine);

    if (!bus.registerObject(QStringLiteral("/"), &engine)) {
        qWarning() << "Could not register D-Bus object for" << s_serviceName;
        bus.unregisterService(s_serviceName);
        return 1;
    }

    // One-shot fallback for the single case that emits no count change: an activation that
    // only queried an already-empty state. Give any pending call time to arrive, then exit
    // if the service is still idle instead of lingering as a permanent background process.
    QTimer::singleShot(idleQuitFallbackMs, &app, [&tryQuit]() {
        tryQuit();
    });

    return app.exec();
}

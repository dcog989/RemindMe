#include <QApplication>
#include <QDBusConnection>
#include <QTimer>

#include <KLocalizedString>

#include <memory>

#include "remindmeadaptor.h"
#include "remindmealarmpresenter.h"
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
    autostart->setActive(!engine.isEmpty() || engine.hasPendingAlarms());
    QObject::connect(&engine, &RemindmeEngine::timerCountChanged, &engine, [&autostart](int timerCount) {
        autostart->setActive(timerCount > 0);
    });

    // Owns alarm/reminder presentation and tracks whether anything is still on screen.
    AlarmPresenter presenter(engine);

    // Exit when there is nothing left to do; D-Bus activation restarts the helper on demand.
    // Drop the service name first so a call racing the exit is routed to a freshly activated
    // helper instead of this process's torn-down object tree.
    const auto tryQuit = [&]() {
        if (engine.isEmpty() && !presenter.isPresenting()) {
            QDBusConnection::sessionBus().unregisterService(s_serviceName);
            app.quit();
        }
    };

    QObject::connect(&engine, &RemindmeEngine::timerExpired, &presenter, [&presenter](const Remindme::TimerInfo &timer) {
        presenter.present(timer, /*expired=*/true);
    });
    QObject::connect(&engine, &RemindmeEngine::showReminderRequested, &presenter, [&presenter](const Remindme::TimerInfo &timer) {
        presenter.present(timer, /*expired=*/false);
    });
    // The last alarm window or in-flight sound going away may have removed the last reason to stay
    // alive (e.g. a cancel-by-dismissal, whose count change fired while the window was still
    // open), so re-check idleness.
    QObject::connect(&presenter, &AlarmPresenter::presentationsFinished, &app, tryQuit);
    // Whenever the engine's timer set changes (create, cancel, expiry, snooze), exit when
    // nothing remains; the engine persists its full state on every change first.
    QObject::connect(&engine, &RemindmeEngine::timerCountChanged, &app, tryQuit);

    // Re-present alarms that fired before this process started (e.g. the session ended while a
    // dialog was open). The engine loaded them from the persisted "fired" entries; showing them
    // now also keeps the presenter busy so tryQuit() cannot drop them.
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

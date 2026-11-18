#include "remindmetrayicon.h"

#include <QApplication>
#include <QDateTime>
#include <QGuiApplication>

#include <KLocalizedString>

#include <algorithm>

#include "remindmeengine.h"
#include "remindmetime.h"

RemindmeTrayIcon::RemindmeTrayIcon(RemindmeEngine &engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
{
    // Headless contexts (CI, no desktop) have no tray to add to; the engine still works.
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
        return;
    }

    m_icon.setIcon(QApplication::windowIcon());
    connect(&m_icon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            m_engine.showReminders();
        }
    });
    // Create, cancel, expiry and snooze all land here; the deadline itself never changes without
    // one of them.
    // shortcut: a timer running past midnight keeps yesterday's "today at ..." wording until the
    // next change; refresh on date rollover if that ever matters.
    connect(&m_engine, &RemindmeEngine::timerCountChanged, this, [this]() {
        refresh();
    });

    refresh();
}

void RemindmeTrayIcon::refresh()
{
    const QList<Remindme::TimerInfo> timers = m_engine.list();
    if (timers.isEmpty()) {
        m_icon.hide();
        return;
    }

    const auto next = std::ranges::min_element(timers, {}, &Remindme::TimerInfo::deadline);
    m_icon.setToolTip(i18n("Next reminder: %1", Remindme::formatReminderTime(QDateTime::fromMSecsSinceEpoch(next->deadline), false)));
    if (!m_icon.isVisible()) {
        m_icon.show();
    }
}

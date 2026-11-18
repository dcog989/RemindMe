#pragma once

#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QString>

#include "remotematch.h"

class RemindmeEngine;

namespace Remindme
{
struct TimerInfo;
}

// Exposes the timer engine to KRunner over the org.kde.krunner1 D-Bus runner interface. KRunner
// auto-activates this process through io.github.dcog989.remindme.service and drives Match()/Run();
// the engine lives in the same process, so matching never crosses D-Bus.
class RemindmeAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.krunner1")

public:
    explicit RemindmeAdaptor(RemindmeEngine *engine);

public Q_SLOTS:
    RemoteMatches Match(const QString &query);
    RemoteActions Actions();
    void Run(const QString &matchId, const QString &actionId);
    void Teardown();
    void SetActivationToken(const QString &token);
    QVariantMap Config();

private:
    void addUsageMatches(RemoteMatches &matches, const QString &trigger) const;
    void addCreateMatch(RemoteMatches &matches, const QString &term) const;
    void addListMatches(RemoteMatches &matches, const QString &trigger) const;
    void addCancelMatches(RemoteMatches &matches, const QString &target, const QString &trigger) const;
    void addCancelTimerMatch(RemoteMatches &matches, const Remindme::TimerInfo &timer) const;
    // Fired alarms awaiting acknowledgement: listed with the active ones so they can be dismissed
    // from KRunner instead of only through their window.
    void addPendingAlarmMatches(RemoteMatches &matches) const;
    void addSimpleMatch(RemoteMatches &matches, const QString &text, const QString &subtext) const;
    QString timerText(const Remindme::TimerInfo &timer) const;
    QString ringingText(const Remindme::TimerInfo &timer) const;

    RemindmeEngine *m_engine;
    QString m_activationToken;
};

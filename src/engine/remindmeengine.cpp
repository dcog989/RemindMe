#include "remindmeengine.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

#include "deadlinescheduler.h"
#include "remindmetime.h"

RemindmeEngine::RemindmeEngine(QObject *parent, const QString &stateFilePath, DeadlineScheduler *scheduler)
    : QObject(parent)
    , m_stateFilePath(stateFilePath.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/timers.json")
                                              : stateFilePath)
    , m_scheduler(scheduler ? scheduler : createDeadlineScheduler(this))
{
    qRegisterMetaType<Remindme::TimerInfo>();

    connect(m_scheduler, &DeadlineScheduler::deadlineReached, this, &RemindmeEngine::checkExpiredTimers);

    loadFromDisk();
    scheduleExpiryCheck();
}

qint64 RemindmeEngine::nowMs() const
{
    return m_scheduler->nowMs();
}

std::optional<Remindme::TimerInfo> RemindmeEngine::create(qint32 durationSeconds, const QString &message)
{
    if (durationSeconds <= 0 || durationSeconds > Remindme::maxDurationSeconds) {
        return std::nullopt;
    }
    Remindme::TimerInfo timer;
    timer.id = m_nextId++;
    timer.deadline = nowMs() + static_cast<qint64>(durationSeconds) * 1000;
    timer.message = message;
    m_timers.insert(timer.id, timer);
    commitTimers();
    return timer;
}

QList<Remindme::TimerInfo> RemindmeEngine::list() const
{
    // m_timers is a QMap, so values() is already sorted by id.
    return m_timers.values();
}

bool RemindmeEngine::isEmpty() const
{
    return m_timers.isEmpty();
}

bool RemindmeEngine::contains(qint32 id) const
{
    return m_timers.contains(id);
}

bool RemindmeEngine::cancel(qint32 id)
{
    const bool removedActive = m_timers.remove(id) > 0;
    // Clearing a fired alarm is how "Dismiss" acknowledges it after a restart.
    const bool removedFired = m_firedAlarms.remove(id) > 0;
    if (removedActive || removedFired) {
        commitTimers();
        return true;
    }
    return false;
}

bool RemindmeEngine::cancelByMessage(const QString &message)
{
    const QList<Remindme::TimerInfo> matches = findTimersByMessage(message);
    if (matches.isEmpty()) {
        return false;
    }
    for (const Remindme::TimerInfo &timer : matches) {
        // An id lives in exactly one set, so removing from both is safe and also acknowledges a
        // fired alarm whose message matches.
        m_timers.remove(timer.id);
        m_firedAlarms.remove(timer.id);
    }
    commitTimers();
    return true;
}

QList<Remindme::TimerInfo> RemindmeEngine::findTimersByMessage(const QString &term) const
{
    QList<Remindme::TimerInfo> matches;
    if (term.isEmpty()) {
        return matches;
    }
    const auto messageMatches = [&term](const Remindme::TimerInfo &timer) {
        return timer.message.contains(term, Qt::CaseInsensitive);
    };
    for (const Remindme::TimerInfo &timer : std::as_const(m_timers)) {
        if (messageMatches(timer)) {
            matches.append(timer);
        }
    }
    for (const Remindme::TimerInfo &timer : std::as_const(m_firedAlarms)) {
        if (messageMatches(timer)) {
            matches.append(timer);
        }
    }
    std::ranges::sort(matches, [](const Remindme::TimerInfo &a, const Remindme::TimerInfo &b) {
        return a.id < b.id;
    });
    return matches;
}

void RemindmeEngine::loadFromDisk()
{
    QFile file(m_stateFilePath);
    if (!file.exists()) {
        return; // first run: nothing to load
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Could not read timer state file" << m_stateFilePath << file.errorString();
        return;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();

    if (error.error != QJsonParseError::NoError || !doc.isArray()) {
        const QString reason = error.error != QJsonParseError::NoError ? error.errorString() : QStringLiteral("root is not a JSON array");
        qWarning() << "Discarding corrupt timer state file" << m_stateFilePath << reason;
        // Preserve every unreadable file so a later save cannot overwrite it and a second
        // corruption cannot destroy the first backup. Timestamped, with a numeric suffix to
        // resolve collisions within the same second; these accumulate and are never pruned.
        const QString base = m_stateFilePath + QStringLiteral(".") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        QString backupPath = base + QStringLiteral(".bak");
        for (int suffix = 1; QFile::exists(backupPath); ++suffix) {
            backupPath = base + QStringLiteral("-") + QString::number(suffix) + QStringLiteral(".bak");
        }
        if (!QFile::rename(m_stateFilePath, backupPath)) {
            qWarning() << "Could not back up corrupt timer state file to" << backupPath;
        }
        return;
    }

    const QJsonArray array = doc.array();
    for (const auto &value : array) {
        const QJsonObject object = value.toObject();
        Remindme::TimerInfo timer;
        timer.id = object.value(QStringLiteral("id")).toInt();
        timer.deadline = object.value(QStringLiteral("deadline")).toInteger();
        timer.message = object.value(QStringLiteral("message")).toString();
        if (timer.id >= 0 && timer.deadline > 0) {
            // A fired entry was already shown and is awaiting Snooze/Dismiss; keep it out of the
            // active set so list() and scheduling ignore it while it stays persisted for replay.
            if (object.value(QStringLiteral("fired")).toBool()) {
                m_firedAlarms.insert(timer.id, timer);
            } else {
                m_timers.insert(timer.id, timer);
            }
            m_nextId = std::max(m_nextId, timer.id + 1);
        }
    }
    Q_EMIT timerCountChanged(m_timers.size() + m_firedAlarms.size());
}

void RemindmeEngine::saveToDisk() const
{
    const QString path = m_stateFilePath;
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonArray array;
    const auto appendTimers = [&array](const QMap<int, Remindme::TimerInfo> &timers, bool fired) {
        for (const Remindme::TimerInfo &timer : timers) {
            QJsonObject object{{QStringLiteral("id"), timer.id},
                               {QStringLiteral("deadline"), QJsonValue(static_cast<qint64>(timer.deadline))},
                               {QStringLiteral("message"), timer.message}};
            if (fired) {
                object.insert(QStringLiteral("fired"), true);
            }
            array.append(object);
        }
    };
    appendTimers(m_timers, false);
    appendTimers(m_firedAlarms, true);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Could not open timer state file for writing" << path << file.errorString();
        return;
    }
    // Owner-only: the file holds reminder text, which other local users should not be able to read.
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (file.write(QJsonDocument(array).toJson(QJsonDocument::Indented)) < 0) {
        qWarning() << "Could not write timer state file" << path << file.errorString();
        file.cancelWriting();
        return;
    }
    if (!file.commit()) {
        qWarning() << "Could not commit timer state file" << path << file.errorString();
    }
}

void RemindmeEngine::scheduleExpiryCheck()
{
    if (m_timers.isEmpty()) {
        m_scheduler->setNextDeadline(std::nullopt);
        return;
    }

    qint64 nextDeadline = std::numeric_limits<qint64>::max();
    for (const Remindme::TimerInfo &timer : std::as_const(m_timers)) {
        nextDeadline = std::min(nextDeadline, timer.deadline);
    }
    m_scheduler->setNextDeadline(nextDeadline);
}

void RemindmeEngine::commitTimers()
{
    saveToDisk();
    scheduleExpiryCheck();
    // Count fired alarms too: the host keeps autostart on while any alarm is unacknowledged.
    Q_EMIT timerCountChanged(m_timers.size() + m_firedAlarms.size());
}

qint64 RemindmeEngine::extendedDeadline(qint64 deadline) const
{
    return std::max(nowMs(), deadline) + Remindme::snoozeDurationMs;
}

void RemindmeEngine::snoozeTimer(Remindme::TimerInfo timer)
{
    timer.deadline = extendedDeadline(timer.deadline);
    m_timers.insert(timer.id, timer);
    commitTimers();
}

void RemindmeEngine::checkExpiredTimers()
{
    const qint64 now = nowMs();
    QList<Remindme::TimerInfo> expired;
    for (auto it = m_timers.begin(); it != m_timers.end();) {
        if (it.value().deadline <= now) {
            expired.append(it.value());
            it = m_timers.erase(it);
        } else {
            ++it;
        }
    }

    // Notify before persisting: an expiry alarm renders a window whose existence an idle-exit
    // listener must see before the count signal (emitted from commitTimers) marks the engine
    // as done. Move the timer to the fired set so the alarm survives a restart until the user
    // snoozes or dismisses it.
    for (const Remindme::TimerInfo &timer : std::as_const(expired)) {
        m_firedAlarms.insert(timer.id, timer);
        Q_EMIT timerExpired(timer);
    }

    // Only persist when something actually changed; either way the expiry timer must be
    // re-armed since it was single-shot and just fired.
    if (expired.isEmpty()) {
        scheduleExpiryCheck();
    } else {
        commitTimers();
    }
}

bool RemindmeEngine::snooze(const Remindme::TimerInfo &timer)
{
    // Prefer the live timer so a still-active timer keeps the time that was left (extending rather
    // than restarting is handled by snoozeTimer()). Otherwise the timer is a fired alarm: drop it
    // from that set before snoozeTimer() re-inserts it into m_timers, since an id must live in
    // exactly one set.
    const auto it = m_timers.constFind(timer.id);
    if (it != m_timers.cend()) {
        snoozeTimer(it.value());
        return true;
    }
    if (m_firedAlarms.remove(timer.id) > 0) {
        snoozeTimer(timer);
        return true;
    }
    return false;
}

void RemindmeEngine::show(qint32 id)
{
    const auto it = m_timers.constFind(id);
    if (it == m_timers.cend()) {
        return;
    }
    Q_EMIT showReminderRequested(it.value());
}

bool RemindmeEngine::hasPendingAlarms() const
{
    return !m_firedAlarms.isEmpty();
}

void RemindmeEngine::replayPendingAlarms()
{
    // m_firedAlarms is a QMap, so values() is already id-ordered; replay matches the order the
    // alarms originally fired in.
    const QList<Remindme::TimerInfo> pending = m_firedAlarms.values();
    for (const Remindme::TimerInfo &timer : pending) {
        Q_EMIT timerExpired(timer);
    }
}

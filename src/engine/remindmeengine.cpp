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
#include <QSocketNotifier>
#include <QStandardPaths>

#include <algorithm>
#include <limits>
#include <unistd.h>
#include <utility>

#ifdef Q_OS_LINUX
#include <sys/timerfd.h>
#endif

#include "remindmetime.h"

namespace
{
// Suspend fallback sweep period: bounds how late an alarm can sound after wake where the
// timerfd path is unavailable (non-Linux, or an injected test clock).
constexpr int sweepIntervalMs = 500;

// Realtime-absolute timerfd: fires when the wall clock hits the arming time, which for a deadline
// already reached while suspended means immediately on resume. -1 where unavailable.
int createRealtimeTimerFd()
{
#ifdef Q_OS_LINUX
    return timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC);
#else
    return -1;
#endif
}
}

RemindmeEngine::RemindmeEngine(QObject *parent, const QString &stateFilePath, std::function<qint64()> clock)
    : QObject(parent)
    , m_stateFilePath(stateFilePath.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/timers.json")
                                              : stateFilePath)
    , m_hasInjectedClock(static_cast<bool>(clock))
    , m_clock(clock ? std::move(clock) : []() {
        return QDateTime::currentMSecsSinceEpoch();
    })
{
    qRegisterMetaType<Remindme::TimerInfo>();

    loadFromDisk();
    m_expiryTimer.setSingleShot(true);
    connect(&m_expiryTimer, &QTimer::timeout, this, &RemindmeEngine::checkExpiredTimers);

    // The timerfd's absolute times are real wall clock; an injected test clock is not, so fall
    // back to the QTimer mechanism the tests drive directly.
    if (!m_hasInjectedClock) {
        m_deadlineFd = createRealtimeTimerFd();
        if (m_deadlineFd >= 0) {
            m_useTimerfd = true;
            m_deadlineNotifier = new QSocketNotifier(m_deadlineFd, QSocketNotifier::Read, this);
            connect(m_deadlineNotifier, &QSocketNotifier::activated, this, [this](QSocketDescriptor, QSocketNotifier::Type) {
                quint64 expirations = 0;
                ::read(m_deadlineFd, &expirations, sizeof(expirations));
                checkExpiredTimers();
            });
        }
    }

    if (!m_useTimerfd) {
        m_sweepTimer.setInterval(sweepIntervalMs);
        connect(&m_sweepTimer, &QTimer::timeout, this, &RemindmeEngine::checkExpiredTimers);
        m_sweepTimer.start();
    }

    scheduleExpiryCheck();
}

RemindmeEngine::~RemindmeEngine()
{
#ifdef Q_OS_LINUX
    if (m_deadlineFd >= 0) {
        ::close(m_deadlineFd);
    }
#endif
}

qint64 RemindmeEngine::nowMs() const
{
    return m_clock();
}

Remindme::TimerInfo RemindmeEngine::Create(qint32 durationSeconds, const QString &message)
{
    Remindme::TimerInfo timer;
    if (durationSeconds <= 0 || durationSeconds > Remindme::maxDurationSeconds) {
        return timer; // invalid duration, leave the timer uninitialized (id -1)
    }
    timer.id = m_nextId++;
    timer.deadline = nowMs() + qint64(durationSeconds) * 1000;
    timer.message = message;
    m_timers.insert(timer.id, timer);
    commitTimers();
    return timer;
}

QList<Remindme::TimerInfo> RemindmeEngine::List()
{
    QList<Remindme::TimerInfo> timers = m_timers.values();
    std::sort(timers.begin(), timers.end(), [](const Remindme::TimerInfo &a, const Remindme::TimerInfo &b) {
        return a.id < b.id;
    });
    return timers;
}

bool RemindmeEngine::Cancel(qint32 id)
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

bool RemindmeEngine::CancelByMessage(const QString &message)
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
    std::sort(matches.begin(), matches.end(), [](const Remindme::TimerInfo &a, const Remindme::TimerInfo &b) {
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
        // Preserve the unreadable file so the next save cannot silently overwrite the only copy
        // of whatever the user had. QFile::rename() refuses to clobber an existing target.
        const QString backupPath = m_stateFilePath + QStringLiteral(".bak");
        QFile::remove(backupPath);
        if (!QFile::rename(m_stateFilePath, backupPath)) {
            qWarning() << "Could not back up corrupt timer state file to" << backupPath;
        }
        return;
    }

    const QJsonArray array = doc.array();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        Remindme::TimerInfo timer;
        timer.id = object.value(QStringLiteral("id")).toInt();
        timer.deadline = object.value(QStringLiteral("deadline")).toVariant().toLongLong();
        timer.message = object.value(QStringLiteral("message")).toString();
        if (timer.id >= 0 && timer.deadline > 0) {
            // A fired entry was already shown and is awaiting Snooze/Dismiss; keep it out of the
            // active set so List() and scheduling ignore it while it stays persisted for replay.
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
    const auto appendTimers = [&array](const QHash<int, Remindme::TimerInfo> &timers, bool fired) {
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
        if (m_useTimerfd) {
#ifdef Q_OS_LINUX
            const itimerspec disarm{}; // zero it_value disarms the timerfd
            timerfd_settime(m_deadlineFd, 0, &disarm, nullptr);
#endif
        } else {
            m_expiryTimer.stop();
        }
        return;
    }

    qint64 nextDeadline = std::numeric_limits<qint64>::max();
    for (const Remindme::TimerInfo &timer : std::as_const(m_timers)) {
        nextDeadline = std::min(nextDeadline, timer.deadline);
    }

#ifdef Q_OS_LINUX
    if (m_useTimerfd) {
        // A past deadline (e.g. reached while suspended) makes the fd readable immediately.
        const itimerspec ts{
            {},
            {time_t(nextDeadline / 1000), long(nextDeadline % 1000) * 1000000L},
        };
        timerfd_settime(m_deadlineFd, TFD_TIMER_ABSTIME, &ts, nullptr);
        return;
    }
#endif
    const qint64 now = nowMs();
    m_expiryTimer.start(std::max<qint64>(0, nextDeadline - now));
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

bool RemindmeEngine::Snooze(qint32 id)
{
    const auto it = m_timers.constFind(id);
    if (it == m_timers.cend()) {
        return false;
    }
    // Extending (rather than restarting) the deadline is handled by snoozeTimer(), which keeps
    // the time that was left on a still-active timer instead of collapsing it to "now + snoozeDurationMs"
    snoozeTimer(it.value());
    return true;
}

void RemindmeEngine::Show(qint32 id)
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
    QList<Remindme::TimerInfo> pending = m_firedAlarms.values();
    std::sort(pending.begin(), pending.end(), [](const Remindme::TimerInfo &a, const Remindme::TimerInfo &b) {
        return a.id < b.id;
    });
    for (const Remindme::TimerInfo &timer : std::as_const(pending)) {
        Q_EMIT timerExpired(timer);
    }
}

void RemindmeEngine::rearmExpiredTimer(const Remindme::TimerInfo &timer)
{
    // checkExpiredTimers() moved the timer to m_firedAlarms; drop it from there so snoozeTimer(),
    // which re-inserts it into m_timers under its original id, does not leave it in both sets.
    // Timer ids are never reused within this process, so this cannot collide with an active timer.
    m_firedAlarms.remove(timer.id);
    snoozeTimer(timer);
}

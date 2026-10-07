#include "remindmeadaptor.h"

#include <QDBusMetaType>
#include <QDateTime>
#include <QLocale>
#include <QStringList>

#include <KFormat>
#include <KLocalizedString>

#include <optional>

#include "remindmeengine.h"
#include "remindmetime.h"

namespace
{
const QStringList s_triggerWords{QStringLiteral("remindme"), QStringLiteral("rme")};

// KRunner::QueryMatch::CategoryRelevance::Highest. The DBus API carries the raw int.
constexpr int categoryRelevanceHighest = 100;

QString formatReminderTime(const QDateTime &deadline, bool capitalizeDay = true)
{
    const QDate today = QDate::currentDate();
    const QString time = QLocale().toString(deadline.time(), QLocale::ShortFormat);
    if (deadline.date() == today) {
        return capitalizeDay ? i18n("Today at %1", time) : i18n("today at %1", time);
    }
    if (deadline.date() == today.addDays(1)) {
        return capitalizeDay ? i18n("Tomorrow at %1", time) : i18n("tomorrow at %1", time);
    }
    return i18n("%1 at %2", QLocale().toString(deadline.date(), QLocale::ShortFormat), time);
}

std::optional<QString> termAfterTrigger(const QString &query)
{
    const QString trimmed = query.trimmed();
    for (const QString &trigger : s_triggerWords) {
        if (trimmed.compare(trigger, Qt::CaseInsensitive) == 0) {
            return QString();
        }
        if (trimmed.size() > trigger.size() && trimmed.startsWith(trigger, Qt::CaseInsensitive) && trimmed.at(trigger.size()).isSpace()) {
            return trimmed.mid(trigger.size()).trimmed();
        }
    }
    return std::nullopt;
}
}

RemindmeAdaptor::RemindmeAdaptor(RemindmeEngine *engine)
    : QDBusAbstractAdaptor(engine)
    , m_engine(engine)
{
    // Register the wire types before the engine object is registered on the bus; without these
    // QDBusAbstractAdaptor cannot derive the signatures and silently drops Match()/Actions().
    qDBusRegisterMetaType<RemoteMatch>();
    qDBusRegisterMetaType<RemoteMatches>();
    qDBusRegisterMetaType<RemoteAction>();
    qDBusRegisterMetaType<RemoteActions>();
}

RemoteMatches RemindmeAdaptor::Match(const QString &query)
{
    RemoteMatches matches;

    const std::optional<QString> term = termAfterTrigger(query);
    if (!term.has_value()) {
        return matches;
    }

    if (term->isEmpty()) {
        addUsageMatches(matches, query.trimmed());
        return matches;
    }
    if (term->compare(QLatin1String("list"), Qt::CaseInsensitive) == 0) {
        addListMatches(matches);
        return matches;
    }
    if (term->compare(QLatin1String("cancel"), Qt::CaseInsensitive) == 0) {
        addCancelMatches(matches, QString());
        return matches;
    }
    if (term->startsWith(QLatin1String("cancel "), Qt::CaseInsensitive)) {
        addCancelMatches(matches, term->mid(7).trimmed());
        return matches;
    }

    addCreateMatch(matches, *term);
    return matches;
}

RemoteActions RemindmeAdaptor::Actions()
{
    return {RemoteAction{QStringLiteral("cancel"), i18n("Cancel timer"), QStringLiteral("dialog-close")}};
}

void RemindmeAdaptor::Run(const QString &matchId, const QString &actionId)
{
    if (matchId.startsWith(QLatin1String("create:"))) {
        const Remindme::Duration duration = Remindme::parseDurationAndMessage(matchId.mid(7));
        if (duration.valid) {
            m_engine->Create(duration.seconds, duration.message);
        }
    } else if (matchId.startsWith(QLatin1String("timer"))) {
        // A listed timer shows its reminder by default; its "cancel" action cancels it.
        const int id = matchId.mid(5).toInt();
        if (actionId == QLatin1String("cancel")) {
            m_engine->Cancel(id);
        } else {
            // KRunner hands us a Wayland activation token (SetActivationToken) so the alarm
            // window can take focus. Qt's QWindow::requestActivate(), reached via the dialog's
            // activateWindow(), consumes XDG_ACTIVATION_TOKEN from the environment, so set it
            // before Show() synchronously opens the window.
            if (!m_activationToken.isEmpty()) {
                qputenv("XDG_ACTIVATION_TOKEN", m_activationToken.toUtf8());
            }
            m_engine->Show(id);
        }
    } else if (matchId.startsWith(QLatin1String("cancelid:"))) {
        // Checked before the "cancel:" case below: both ids start with "cancel", so an
        // explicit trailing ":" after each tag keeps the prefixes mutually exclusive
        // regardless of check order.
        m_engine->Cancel(matchId.mid(9).toInt());
    } else if (matchId.startsWith(QLatin1String("cancelmsg:"))) {
        m_engine->CancelByMessage(matchId.mid(10));
    } else if (matchId.startsWith(QLatin1String("cancel:"))) {
        m_engine->Cancel(matchId.mid(7).toInt());
    }
}

void RemindmeAdaptor::Teardown()
{
}

void RemindmeAdaptor::SetActivationToken(const QString &token)
{
    m_activationToken = token;
}

QVariantMap RemindmeAdaptor::Config()
{
    // DBus2 runners receive trigger words through Config() rather than AbstractRunner helpers.
    return {{QStringLiteral("TriggerWords"), s_triggerWords}};
}

void RemindmeAdaptor::addUsageMatches(RemoteMatches &matches, const QString &trigger) const
{
    struct Usage {
        QStringList examples;
        QString description;
    };
    const QList<Usage> usages = {
        {{QStringLiteral("remindme <duration> [<message>]"), QStringLiteral("rme <duration> [<message>]")}, i18n("Create and manage timers.")},
        {{QStringLiteral("rme list")}, i18n("List timers")},
        {{QStringLiteral("rme cancel <id>")}, i18n("Cancel timers with given id")},
        {{QStringLiteral("rme cancel <message>")}, i18n("Cancel all timers matching given message")},
    };

    int index = 0;
    for (const Usage &usage : usages) {
        // Prefer the example that matches the trigger word the user typed ("rme" vs "remindme")
        QString example = usage.examples.constFirst();
        for (const QString &candidate : usage.examples) {
            if (candidate.compare(trigger, Qt::CaseInsensitive) == 0 || candidate.startsWith(trigger + QLatin1Char(' '), Qt::CaseInsensitive)) {
                example = candidate;
                break;
            }
        }

        RemoteMatch match;
        match.id = QStringLiteral("usage%1").arg(index++);
        match.text = example;
        match.iconName = QStringLiteral("kronometer");
        match.categoryRelevance = categoryRelevanceHighest;
        match.relevance = 0.6;
        match.properties.insert(QStringLiteral("subtext"), usage.description);
        match.properties.insert(QStringLiteral("actions"), QStringList());
        matches.append(match);
    }
}

void RemindmeAdaptor::addCreateMatch(RemoteMatches &matches, const QString &term) const
{
    const Remindme::Duration duration = Remindme::parseDurationAndMessage(term);
    if (!duration.valid) {
        addSimpleMatch(matches, i18n("Invalid: %1", term.toHtmlEscaped()), i18n("Use e.g. 5, 20s, 4h, 1:30"));
        return;
    }

    const QDateTime deadline = QDateTime::currentDateTime().addSecs(duration.seconds);
    // Hide seconds only when hours are displayed, folding them into the minutes (nearest);
    // formatSpelloutDuration otherwise silently drops them. Below an hour it shows them.
    const qint64 durationMs = qint64(duration.seconds) * 1000;
    const QString durationText = duration.seconds >= 3600 ? KFormat().formatSpelloutDuration(qint64((duration.seconds + 30) / 60) * 60 * 1000)
                                                          : KFormat().formatSpelloutDuration(durationMs);
    QString text = i18n("Set timer for %1", durationText).toHtmlEscaped();
    text += QStringLiteral("<br>") + i18n("Reminder %1", formatReminderTime(deadline, false)).toHtmlEscaped();
    if (!duration.message.isEmpty()) {
        text += QStringLiteral("<br>") + duration.message.toHtmlEscaped();
    }

    RemoteMatch match;
    // The whole term is encoded in the id so Run() can re-parse it without keeping match state.
    match.id = QStringLiteral("create:") + term;
    match.text = text;
    match.iconName = QStringLiteral("kronometer");
    match.categoryRelevance = categoryRelevanceHighest;
    match.relevance = 0.9;
    match.properties.insert(QStringLiteral("multiline"), true);
    match.properties.insert(QStringLiteral("actions"), QStringList());
    matches.append(match);
}

void RemindmeAdaptor::addListMatches(RemoteMatches &matches) const
{
    const QList<Remindme::TimerInfo> timers = m_engine->List();
    if (timers.isEmpty()) {
        addSimpleMatch(matches, i18n("No timers"), i18n("Use rme with a duration to start one"));
        return;
    }

    for (const Remindme::TimerInfo &timer : timers) {
        RemoteMatch match;
        match.id = QStringLiteral("timer%1").arg(timer.id);
        // All details go in the text line so they are visible regardless of row selection state;
        // the id is included so that "rme cancel <id>" can target a specific timer.
        match.text = timerText(timer);
        match.iconName = QStringLiteral("kronometer");
        match.categoryRelevance = categoryRelevanceHighest;
        match.relevance = 0.7;
        match.properties.insert(QStringLiteral("multiline"), true);
        match.properties.insert(QStringLiteral("actions"), QStringList{QStringLiteral("cancel")});
        matches.append(match);
    }
}

void RemindmeAdaptor::addCancelMatches(RemoteMatches &matches, const QString &target) const
{
    if (target.isEmpty()) {
        // "rme cancel" with no target: list every active timer so the user can pick one to cancel
        const QList<Remindme::TimerInfo> timers = m_engine->List();
        if (timers.isEmpty()) {
            addSimpleMatch(matches, i18n("No timers"), i18n("There are no timers to cancel"));
            return;
        }
        for (const Remindme::TimerInfo &timer : timers) {
            addCancelTimerMatch(matches, timer);
        }
        return;
    }

    bool isNumber = false;
    // Accept a leading "#" as an id marker, matching how ids are shown in "rme list"
    const QString idString = target.startsWith(QLatin1Char('#')) ? target.mid(1) : target;
    const int id = idString.toInt(&isNumber);

    RemoteMatch match;
    match.iconName = QStringLiteral("kronometer");
    match.categoryRelevance = categoryRelevanceHighest;
    match.properties.insert(QStringLiteral("actions"), QStringList());
    if (isNumber) {
        // Validate the id refers to an active timer, so a stale or mistyped id is not
        // silently accepted as a no-op cancellation.
        bool exists = false;
        const QList<Remindme::TimerInfo> timers = m_engine->List();
        for (const Remindme::TimerInfo &timer : timers) {
            if (timer.id == id) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            addSimpleMatch(matches, i18n("No timer with id %1", id), i18n("Use rme list to see timers"));
            return;
        }
        match.id = QStringLiteral("cancelid:%1").arg(id);
        // The "#" prefix mirrors how ids are shown in "rme list", marking this as an id
        match.text = i18n("Cancel timer #%1", idString);
        match.relevance = 0.8;
    } else {
        // The term may be pasted markup; the match text is rendered as rich text, so escape it
        // to keep the message literal (see also the "Invalid:" text in addCreateMatch).
        match.id = QStringLiteral("cancelmsg:") + idString;
        match.text = i18n("Cancel all timers matching \"%1\"", idString.toHtmlEscaped());
        match.relevance = 0.8;
    }
    matches.append(match);
}

void RemindmeAdaptor::addCancelTimerMatch(RemoteMatches &matches, const Remindme::TimerInfo &timer) const
{
    RemoteMatch match;
    match.id = QStringLiteral("cancel:%1").arg(timer.id);
    match.text = timerText(timer);
    match.iconName = QStringLiteral("kronometer");
    match.categoryRelevance = categoryRelevanceHighest;
    match.relevance = 0.8;
    match.properties.insert(QStringLiteral("subtext"), i18n("Cancel this timer"));
    match.properties.insert(QStringLiteral("multiline"), true);
    match.properties.insert(QStringLiteral("actions"), QStringList());
    matches.append(match);
}

void RemindmeAdaptor::addSimpleMatch(RemoteMatches &matches, const QString &text, const QString &subtext) const
{
    RemoteMatch match;
    match.text = text;
    match.iconName = QStringLiteral("kronometer");
    match.categoryRelevance = categoryRelevanceHighest;
    match.relevance = 0.4;
    match.properties.insert(QStringLiteral("subtext"), subtext);
    match.properties.insert(QStringLiteral("actions"), QStringList());
    matches.append(match);
}

QString RemindmeAdaptor::timerText(const Remindme::TimerInfo &timer) const
{
    // The id and reminder time share the first line; the message goes on its own line so long
    // text does not truncate the row. krunner renders the <br> as rich text (multiline).
    const QString reminderTime = formatReminderTime(QDateTime::fromMSecsSinceEpoch(timer.deadline));
    QString text = i18n("#%1 %2", timer.id, reminderTime);
    if (!timer.message.isEmpty()) {
        text += QStringLiteral("<br>") + timer.message.toHtmlEscaped();
    }
    return text;
}

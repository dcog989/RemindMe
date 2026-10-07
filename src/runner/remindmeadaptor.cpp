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
// Keep in sync with the X-Plasma-Runner-Syntaxes hints in data/plasma-runner-remindme.desktop,
// which spell these same trigger words but cannot share this list.
const QStringList s_triggerWords{QStringLiteral("remindme"), QStringLiteral("rme")};

// Match-id tags: Run() dispatches on them and the builders prepend them. Sharing the constants
// keeps parsing and construction from drifting apart.
const QString s_createPrefix{QStringLiteral("create:")};
const QString s_timerPrefix{QStringLiteral("timer")};
const QString s_cancelIdPrefix{QStringLiteral("cancelid:")};
const QString s_cancelMessagePrefix{QStringLiteral("cancelmsg:")};
const QString s_cancelPrefix{QStringLiteral("cancel:")};

// KRunner::QueryMatch::CategoryRelevance::Highest. The DBus API carries the raw int.
constexpr int categoryRelevanceHighest = 100;

// Builds a match carrying the fields every reminder match shares, so a new common field only has
// to be added here. Callers layer on any per-match properties afterwards.
RemoteMatch makeMatch(const QString &id, const QString &text, qreal relevance)
{
    RemoteMatch match;
    match.id = id;
    match.text = text;
    match.iconName = QStringLiteral("kronometer");
    match.categoryRelevance = categoryRelevanceHighest;
    match.relevance = relevance;
    match.properties.insert(QStringLiteral("actions"), QStringList());
    return match;
}

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
    if (matchId.startsWith(s_createPrefix)) {
        const Remindme::Duration duration = Remindme::parseDurationAndMessage(matchId.mid(s_createPrefix.size()));
        if (duration.valid) {
            m_engine->Create(duration.seconds, duration.message);
        }
    } else if (matchId.startsWith(s_timerPrefix)) {
        // A listed timer shows its reminder by default; its "cancel" action cancels it.
        const int id = matchId.mid(s_timerPrefix.size()).toInt();
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
    } else if (matchId.startsWith(s_cancelIdPrefix)) {
        // Checked before the "cancel:" case below: both ids start with "cancel", so an
        // explicit trailing ":" after each tag keeps the prefixes mutually exclusive
        // regardless of check order.
        m_engine->Cancel(matchId.mid(s_cancelIdPrefix.size()).toInt());
    } else if (matchId.startsWith(s_cancelMessagePrefix)) {
        m_engine->CancelByMessage(matchId.mid(s_cancelMessagePrefix.size()));
    } else if (matchId.startsWith(s_cancelPrefix)) {
        m_engine->Cancel(matchId.mid(s_cancelPrefix.size()).toInt());
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
    // Command forms after the trigger word. The trigger is prepended per hint so both aliases
    // ("remindme" and "rme") see the syntax in their own spelling.
    struct Usage {
        QString command;
        QString description;
    };
    const QList<Usage> usages = {
        {QStringLiteral("<duration> [<message>]"), i18n("Create and manage timers.")},
        {QStringLiteral("list"), i18n("List timers")},
        {QStringLiteral("cancel <id>"), i18n("Cancel timers with given id")},
        {QStringLiteral("cancel <message>"), i18n("Cancel all timers matching given message")},
    };

    // Echo the canonical spelling of whichever trigger alias the user typed.
    QString prefix = trigger;
    for (const QString &candidate : s_triggerWords) {
        if (candidate.compare(trigger, Qt::CaseInsensitive) == 0) {
            prefix = candidate;
            break;
        }
    }

    int index = 0;
    for (const Usage &usage : usages) {
        RemoteMatch match = makeMatch(QStringLiteral("usage%1").arg(index++), prefix + QLatin1Char(' ') + usage.command, 0.6);
        match.properties.insert(QStringLiteral("subtext"), usage.description);
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

    // The whole term is encoded in the id so Run() can re-parse it without keeping match state.
    RemoteMatch match = makeMatch(s_createPrefix + term, text, 0.9);
    match.properties.insert(QStringLiteral("multiline"), true);
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
        // All details go in the text line so they are visible regardless of row selection state;
        // the id is included so that "rme cancel <id>" can target a specific timer.
        RemoteMatch match = makeMatch(s_timerPrefix + QString::number(timer.id), timerText(timer), 0.7);
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
    // Accept a leading "#" as an id marker, matching how ids are shown in "rme list". The marker
    // means the term is an id, so a non-numeric "#foo" is reported as a missing id rather than
    // matched as message text with the "#" silently stripped.
    const bool idMarker = target.startsWith(QLatin1Char('#'));
    const QString idString = idMarker ? target.mid(1) : target;
    // Parse through the locale, matching the duration parser, so the native digits a user typed
    // for the duration can also address its id. RejectGroupSeparator keeps a message such as
    // "1,5" or "1.5" from silently reading as the id 15 under a locale that groups either way.
    QLocale idLocale;
    idLocale.setNumberOptions(QLocale::RejectGroupSeparator);
    const int id = idLocale.toInt(idString, &isNumber);

    if (isNumber || idMarker) {
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
            addSimpleMatch(matches, i18n("No timer with id %1", idString.toHtmlEscaped()), i18n("Use rme list to see timers"));
            return;
        }
        matches.append(makeMatch(s_cancelIdPrefix + QString::number(id), i18n("Cancel timer #%1", idString), 0.8));
    } else {
        // The term is a message substring; validate it matches before offering, like the id path,
        // so the result is never a no-op. Both the offer and the actual cancellation use the same
        // case-insensitive match through the engine.
        if (m_engine->findTimersByMessage(target).isEmpty()) {
            addSimpleMatch(matches, i18n("No timer matching \"%1\"", target.toHtmlEscaped()), i18n("Use rme list to see timers"));
            return;
        }
        // The term may be pasted markup; the match text is rendered as rich text, so escape it
        // to keep the message literal (see also the "Invalid:" text in addCreateMatch).
        matches.append(makeMatch(s_cancelMessagePrefix + target, i18n("Cancel all timers matching \"%1\"", target.toHtmlEscaped()), 0.8));
    }
}

void RemindmeAdaptor::addCancelTimerMatch(RemoteMatches &matches, const Remindme::TimerInfo &timer) const
{
    RemoteMatch match = makeMatch(s_cancelPrefix + QString::number(timer.id), timerText(timer), 0.8);
    match.properties.insert(QStringLiteral("subtext"), i18n("Cancel this timer"));
    match.properties.insert(QStringLiteral("multiline"), true);
    matches.append(match);
}

void RemindmeAdaptor::addSimpleMatch(RemoteMatches &matches, const QString &text, const QString &subtext) const
{
    RemoteMatch match = makeMatch(QString(), text, 0.4);
    match.properties.insert(QStringLiteral("subtext"), subtext);
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

#pragma once

#include <QDBusArgument>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QVariantMap>

// Wire types for the org.kde.krunner1 D-Bus runner interface. These mirror the structures the
// KRunner DBusRunner proxy marshals:
//
//   Match   -> a(sssida{sv})   RemoteMatches
//   Actions -> a(sss)          RemoteActions
//
// Defining them here keeps the runner free of any KRunner library dependency; only the field
// layout and order matter.

struct RemoteMatch {
    QString id;
    QString text;
    QString iconName;
    // KRunner::QueryMatch::CategoryRelevance (Highest is 100); the field name follows upstream.
    int categoryRelevance = 0;
    qreal relevance = 0;
    QVariantMap properties;
};

using RemoteMatches = QList<RemoteMatch>;

struct RemoteAction {
    QString id;
    QString text;
    QString iconName;
};

using RemoteActions = QList<RemoteAction>;

inline QDBusArgument &operator<<(QDBusArgument &argument, const RemoteMatch &match)
{
    argument.beginStructure();
    argument << match.id;
    argument << match.text;
    argument << match.iconName;
    argument << match.categoryRelevance;
    argument << match.relevance;
    argument << match.properties;
    argument.endStructure();
    return argument;
}

inline const QDBusArgument &operator>>(const QDBusArgument &argument, RemoteMatch &match)
{
    argument.beginStructure();
    argument >> match.id;
    argument >> match.text;
    argument >> match.iconName;
    argument >> match.categoryRelevance;
    argument >> match.relevance;
    argument >> match.properties;
    argument.endStructure();
    // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter): Qt's D-Bus operator>> signature.
    return argument;
}

inline QDBusArgument &operator<<(QDBusArgument &argument, const RemoteAction &action)
{
    argument.beginStructure();
    argument << action.id;
    argument << action.text;
    argument << action.iconName;
    argument.endStructure();
    return argument;
}

inline const QDBusArgument &operator>>(const QDBusArgument &argument, RemoteAction &action)
{
    argument.beginStructure();
    argument >> action.id;
    argument >> action.text;
    argument >> action.iconName;
    argument.endStructure();
    // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter): Qt's D-Bus operator>> signature.
    return argument;
}

Q_DECLARE_METATYPE(RemoteMatch)
Q_DECLARE_METATYPE(RemoteMatches)
Q_DECLARE_METATYPE(RemoteAction)
Q_DECLARE_METATYPE(RemoteActions)

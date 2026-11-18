#pragma once

#include <QDateTime>
#include <QString>

#include <optional>

namespace Remindme
{
constexpr int maxDurationSeconds = 99 * 3600 + 59 * 60 + 59;

struct Duration {
    int seconds = 0;
    QString message;
    bool valid = false;
};

// Parses text as one whole number through the system locale, so native digits work, while
// rejecting group separators: "1,234" is not 1234 here. Returns nullopt when text is not a
// complete number. The duration parser accepts well-formed grouping in its own number path.
std::optional<int> parseLocaleInt(const QString &text);

Duration parseDurationAndMessage(const QString &input);

// Formats a timer deadline for display, shared by the KRunner list rows and the alarm window so
// their wording cannot drift: "Today at 14:30" / "tomorrow at 14:30" / "12.03.2026 at 14:30".
// capitalizeDay picks the row wording (true) or the sentence wording inside the window (false).
QString formatReminderTime(const QDateTime &deadline, bool capitalizeDay = true);
}

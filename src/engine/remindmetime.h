#pragma once

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
// rejecting group separators: a value such as "1,5" or "1.5" must not silently read as 15 under a
// locale that groups that way. Returns nullopt when text is not a complete number.
std::optional<int> parseLocaleInt(const QString &text);

Duration parseDurationAndMessage(const QString &input);
}

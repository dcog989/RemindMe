#include "remindmetime.h"

#include <QList>
#include <QLocale>
#include <QtMath>

namespace Remindme
{
Duration parseDurationAndMessage(const QString &input)
{
    Duration result;
    if (input.isEmpty()) {
        return result;
    }

    const int firstSpace = input.indexOf(QLatin1Char(' '));
    const int timeTokenLength = firstSpace == -1 ? input.size() : firstSpace;
    const QString timeToken = input.left(timeTokenLength);
    const QString message = input.mid(timeTokenLength).trimmed();

    // Range-checks a candidate and stores it in result, so a computed duration is
    // validated exactly once instead of being checked here and again on assignment.
    const auto setSecondsIfValid = [&result](qint64 computed) {
        if (computed > 0 && computed <= maxDurationSeconds) {
            result.seconds = static_cast<int>(computed);
            result.valid = true;
        }
    };

    // Parse numbers through the locale (so native digits work) but reject group separators, so a
    // grouped value cannot be misread on any path (colon components and suffixed numbers alike).
    QLocale locale;
    locale.setNumberOptions(QLocale::RejectGroupSeparator);

    if (timeToken.contains(QLatin1Char(':'))) {
        const QStringList parts = timeToken.split(QLatin1Char(':'));
        if (parts.size() == 2 || parts.size() == 3) {
            QList<int> values;
            bool ok = true;
            for (const QString &part : parts) {
                bool partOk = false;
                const int value = locale.toInt(part, &partOk);
                // Reject empty or negative components, so "1:-30" cannot borrow from the leading unit.
                if (!partOk || value < 0) {
                    ok = false;
                    break;
                }
                values.append(value);
            }
            // Everything after the leading unit is minutes/seconds and must be < 60, so "0:99"
            // is rejected instead of being read as 99 seconds.
            for (int i = 1; ok && i < values.size(); ++i) {
                if (values.at(i) >= 60) {
                    ok = false;
                }
            }
            if (ok) {
                // Base-60 Horner: [m,s] -> m*60+s, [h,m,s] -> h*3600+m*60+s.
                qint64 computed = 0;
                for (const int value : values) {
                    computed = computed * 60 + value;
                }
                setSecondsIfValid(computed);
            }
        }
    } else {
        // A trailing s/m/h picks the unit; any other token is minutes (bare number or fraction)
        const QChar unit = timeToken.size() > 1 ? timeToken.at(timeToken.size() - 1).toLower() : QChar();
        const bool hasUnit = unit == QLatin1Char('s') || unit == QLatin1Char('m') || unit == QLatin1Char('h');
        qint64 multiplier = 60;
        if (unit == QLatin1Char('s')) {
            multiplier = 1;
        } else if (unit == QLatin1Char('h')) {
            multiplier = 3600;
        }
        const QString numberPart = hasUnit ? timeToken.left(timeToken.size() - 1) : timeToken;

        // Integer form, e.g. "5", "20s", "4h"
        bool numberOk = false;
        const int number = locale.toInt(numberPart, &numberOk);
        if (numberOk) {
            setSecondsIfValid(qint64(number) * multiplier);
        }
        // Fractional form, e.g. ".5", ".5m", "0,5h", rounded to whole seconds. The fraction is
        // detected with the locale's decimal separator, and the ASCII dot a numeric keypad emits
        // is accepted as an alias for it.
        if (!result.valid) {
            const QString decimalPoint = locale.decimalPoint();
            QString fractionPart = numberPart;
            if (decimalPoint != QLatin1String(".")) {
                fractionPart.replace(QLatin1Char('.'), decimalPoint);
            }
            if (fractionPart.contains(decimalPoint)) {
                bool fractionOk = false;
                const double value = locale.toDouble(fractionPart, &fractionOk);
                const double seconds = value * multiplier;
                // Range-check in the double domain before rounding: qRound64 on a huge (or NaN or
                // infinite) value is undefined, so out-of-range input must never reach the cast.
                if (fractionOk && seconds > 0.0 && seconds <= static_cast<double>(maxDurationSeconds) + 0.5) {
                    setSecondsIfValid(qRound64(seconds));
                }
            }
        }
    }

    if (result.valid) {
        result.message = message;
    }
    return result;
}

} // namespace Remindme

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

    if (timeToken.contains(QLatin1Char(':'))) {
        const QStringList parts = timeToken.split(QLatin1Char(':'));
        if (parts.size() == 2 || parts.size() == 3) {
            QList<int> values;
            bool ok = true;
            for (const QString &part : parts) {
                bool partOk = false;
                const int value = QLocale().toInt(part, &partOk);
                ok = ok && partOk;
                values.append(value);
            }
            if (ok) {
                qint64 computed;
                if (values.size() == 2) {
                    computed = qint64(values.at(0)) * 60 + values.at(1);
                } else {
                    computed = qint64(values.at(0)) * 3600 + qint64(values.at(1)) * 60 + values.at(2);
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

        // Parse through the locale (so native digits work) but reject group separators, so a
        // grouped integer cannot pre-empt the fractional form below.
        QLocale locale;
        locale.setNumberOptions(QLocale::RejectGroupSeparator);

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
                if (fractionOk) {
                    setSecondsIfValid(qRound64(value * multiplier));
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

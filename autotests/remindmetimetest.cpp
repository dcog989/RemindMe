#include "remindmetime.h"

#include <KLocalizedString>
#include <QLocale>
#include <QObject>
#include <QTest>

using namespace Remindme;

class RemindmeTimeTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        // A domain with no installed catalog, so i18n returns the source strings the assertions spell out.
        KLocalizedString::setApplicationDomain(QByteArrayLiteral("remindme-autotest"));
    }

    void testBareNumberIsMinutes()
    {
        const Duration duration = parseDurationAndMessage(QStringLiteral("5"));
        QVERIFY(duration.valid);
        QCOMPARE(duration.seconds, 5 * 60);
        QVERIFY(duration.message.isEmpty());
    }

    void testSecondsUnit()
    {
        const Duration duration = parseDurationAndMessage(QStringLiteral("20s"));
        QVERIFY(duration.valid);
        QCOMPARE(duration.seconds, 20);
    }

    void testMinutesUnit()
    {
        const Duration duration = parseDurationAndMessage(QStringLiteral("4m"));
        QVERIFY(duration.valid);
        QCOMPARE(duration.seconds, 4 * 60);
    }

    void testHoursUnit()
    {
        const Duration duration = parseDurationAndMessage(QStringLiteral("4h"));
        QVERIFY(duration.valid);
        QCOMPARE(duration.seconds, 4 * 3600);
    }

    void testMinutesSeconds()
    {
        const Duration duration = parseDurationAndMessage(QStringLiteral("5:30"));
        QVERIFY(duration.valid);
        QCOMPARE(duration.seconds, 5 * 60 + 30);
    }

    void testHoursMinutesSeconds()
    {
        const Duration duration = parseDurationAndMessage(QStringLiteral("4:15:30"));
        QVERIFY(duration.valid);
        QCOMPARE(duration.seconds, 4 * 3600 + 15 * 60 + 30);
    }

    void testMessage()
    {
        const Duration duration = parseDurationAndMessage(QStringLiteral("8 pasta is ready!"));
        QVERIFY(duration.valid);
        QCOMPARE(duration.seconds, 8 * 60);
        QCOMPARE(duration.message, QStringLiteral("pasta is ready!"));
    }

    void testCaseInsensitiveUnit()
    {
        const Duration duration = parseDurationAndMessage(QStringLiteral("20S"));
        QVERIFY(duration.valid);
        QCOMPARE(duration.seconds, 20);
    }

    void testNativeDigits()
    {
        // The parser reads numbers through the system locale, so a native digit set parses
        // where the C-locale QString::toInt would reject it. Restore the locale immediately.
        QLocale::setDefault(QLocale(QLocale::Arabic));
        const Duration duration = parseDurationAndMessage(QStringLiteral("٥"));
        QLocale::setDefault(QLocale::c());
        QVERIFY(duration.valid);
        QCOMPARE(duration.seconds, 5 * 60);
    }

    void testFractionalBareNumberIsFractionOfMinute()
    {
        const Duration duration = parseDurationAndMessage(QStringLiteral(".5"));
        QVERIFY(duration.valid);
        QCOMPARE(duration.seconds, 30);
    }

    void testFractionalMinutesUnit()
    {
        const Duration duration = parseDurationAndMessage(QStringLiteral(".5m"));
        QVERIFY(duration.valid);
        QCOMPARE(duration.seconds, 30);
    }

    void testFractionalHoursUnit()
    {
        const Duration duration = parseDurationAndMessage(QStringLiteral(".5h"));
        QVERIFY(duration.valid);
        QCOMPARE(duration.seconds, 30 * 60);
    }

    void testFractionalSecondsUnit()
    {
        const Duration duration = parseDurationAndMessage(QStringLiteral(".5s"));
        QVERIFY(duration.valid);
        QCOMPARE(duration.seconds, 1);
    }

    void testFractionalCommaLocale()
    {
        // In a comma-decimal locale a fraction is written with ',', and the ASCII dot a numeric
        // keypad emits is accepted as an alias for it. Restore the locale immediately.
        const QLocale previous = QLocale();
        QLocale::setDefault(QLocale(QLocale::German));
        const Duration comma = parseDurationAndMessage(QStringLiteral("0,5"));
        const Duration dot = parseDurationAndMessage(QStringLiteral("0.5"));
        QLocale::setDefault(previous);
        QVERIFY(comma.valid);
        QCOMPARE(comma.seconds, 30);
        QVERIFY(dot.valid);
        QCOMPARE(dot.seconds, 30);
    }

    void testGroupedNumberStaysWhole()
    {
        // In a dot-grouping locale "1.500" is 1500 minutes, not 1.5, while a one-digit fraction
        // still uses the dot alias.
        const QLocale previous = QLocale();
        QLocale::setDefault(QLocale(QLocale::German));
        const Duration grouped = parseDurationAndMessage(QStringLiteral("1.500"));
        const Duration fraction = parseDurationAndMessage(QStringLiteral("1.5"));
        const Duration leadingZero = parseDurationAndMessage(QStringLiteral("0.500h"));
        QLocale::setDefault(previous);
        QVERIFY(grouped.valid);
        QCOMPARE(grouped.seconds, 1500 * 60);
        QVERIFY(fraction.valid);
        QCOMPARE(fraction.seconds, 90);
        QVERIFY(leadingZero.valid);
        QCOMPARE(leadingZero.seconds, 30 * 60);
    }

    void testEnglishGroupSeparator()
    {
        const QLocale previous = QLocale();
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
        const Duration grouped = parseDurationAndMessage(QStringLiteral("1,234"));
        const Duration partialGroup = parseDurationAndMessage(QStringLiteral("1,5"));
        QLocale::setDefault(previous);
        // Well-formed grouping is a whole number; a partial group is not a fraction.
        QVERIFY(grouped.valid);
        QCOMPARE(grouped.seconds, 1234 * 60);
        QVERIFY(!partialGroup.valid);
    }

    void testDocumentedFractionHasWholePart()
    {
        // README documents "5.5" as 5 minutes 30 seconds.
        const Duration duration = parseDurationAndMessage(QStringLiteral("5.5"));
        QVERIFY(duration.valid);
        QCOMPARE(duration.seconds, 5 * 60 + 30);
    }

    void testFormatReminderTime()
    {
        const QDate today = QDate::currentDate();
        const QTime time(14, 30);
        const QString timeText = QLocale().toString(time, QLocale::ShortFormat);

        const QString todayText = formatReminderTime(QDateTime(today, time));
        QVERIFY(todayText.startsWith(QStringLiteral("Today at ")));
        QVERIFY(todayText.endsWith(timeText));
        QVERIFY(formatReminderTime(QDateTime(today, time), false).startsWith(QStringLiteral("today at ")));

        const QString tomorrowText = formatReminderTime(QDateTime(today.addDays(1), time));
        QVERIFY(tomorrowText.startsWith(QStringLiteral("Tomorrow at ")));
        QVERIFY(tomorrowText.endsWith(timeText));

        const QString laterText = formatReminderTime(QDateTime(today.addDays(5), time));
        QVERIFY(!laterText.contains(QLatin1String("Today")));
        QVERIFY(!laterText.contains(QLatin1String("Tomorrow")));
        QVERIFY(laterText.contains(QLocale().toString(today.addDays(5), QLocale::ShortFormat)));
        QVERIFY(laterText.endsWith(timeText));
    }

    void testZeroIsInvalid()
    {
        QVERIFY(!parseDurationAndMessage(QStringLiteral("0")).valid);
        QVERIFY(!parseDurationAndMessage(QStringLiteral("0s")).valid);
    }

    void testEmptyIsInvalid()
    {
        QVERIFY(!parseDurationAndMessage(QString()).valid);
        QVERIFY(!parseDurationAndMessage(QStringLiteral("   ")).valid);
    }

    void testNonNumberIsInvalid()
    {
        QVERIFY(!parseDurationAndMessage(QStringLiteral("abc")).valid);
        QVERIFY(!parseDurationAndMessage(QStringLiteral("5x")).valid);
    }

    void testTooLongIsInvalid()
    {
        QVERIFY(!parseDurationAndMessage(QStringLiteral("100:00:00")).valid);
    }

    void testTooManyColonPartsIsInvalid()
    {
        QVERIFY(!parseDurationAndMessage(QStringLiteral("1:2:3:4")).valid);
    }

    void testMaxDurationBoundary()
    {
        // 99:59:59 is the maximum accepted duration
        const Duration duration = parseDurationAndMessage(QStringLiteral("99:59:59"));
        QVERIFY(duration.valid);
        QCOMPARE(duration.seconds, maxDurationSeconds);
    }

    void testEmptyColonPartsRejected()
    {
        QVERIFY(!parseDurationAndMessage(QStringLiteral("::")).valid);
        QVERIFY(!parseDurationAndMessage(QStringLiteral("5:")).valid);
        QVERIFY(!parseDurationAndMessage(QStringLiteral(":30")).valid);
        QVERIFY(!parseDurationAndMessage(QStringLiteral("1::30")).valid);
    }

    void testColonComponentsMustBeInRange()
    {
        // Trailing components are minutes/seconds and must be < 60
        QVERIFY(!parseDurationAndMessage(QStringLiteral("0:99")).valid);
        QVERIFY(!parseDurationAndMessage(QStringLiteral("1:60")).valid);
        QVERIFY(!parseDurationAndMessage(QStringLiteral("1:2:60")).valid);
        QVERIFY(!parseDurationAndMessage(QStringLiteral("1:60:00")).valid);

        // Only the total is capped, so a large leading unit is fine up to maxDurationSeconds
        const Duration hour = parseDurationAndMessage(QStringLiteral("60:00"));
        QVERIFY(hour.valid);
        QCOMPARE(hour.seconds, 3600);
        QVERIFY(parseDurationAndMessage(QStringLiteral("1:59")).valid);
    }

    void testNegativeColonComponentIsInvalid()
    {
        QVERIFY(!parseDurationAndMessage(QStringLiteral("1:-30")).valid);
        QVERIFY(!parseDurationAndMessage(QStringLiteral("-1:30")).valid);
    }

    void testHugeFractionIsInvalid()
    {
        // Must be range-checked in the double domain before qRound64, which is undefined
        // out of range and must not be reached.
        QVERIFY(!parseDurationAndMessage(QStringLiteral("1.0e300")).valid);
        QVERIFY(!parseDurationAndMessage(QStringLiteral("1.0e300h")).valid);
    }
};

QTEST_GUILESS_MAIN(RemindmeTimeTest)

#include "remindmetimetest.moc"

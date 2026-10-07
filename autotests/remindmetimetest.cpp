#include "remindmetime.h"

#include <QLocale>
#include <QObject>
#include <QTest>

using namespace Remindme;

class RemindmeTimeTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
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
        QVERIFY(!parseDurationAndMessage(QStringLiteral("1e300h")).valid);
    }
};

QTEST_GUILESS_MAIN(RemindmeTimeTest)

#include "remindmetimetest.moc"

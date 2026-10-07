#include "remindmeautostart.h"

#include <KConfigGroup>
#include <KDesktopFile>

#include <QCoreApplication>
#include <QFile>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

class RemindmeAutostartTest : public QObject
{
    Q_OBJECT

private:
    QScopedPointer<QTemporaryDir> tempDir;

    QString entryPath() const
    {
        return tempDir->path() + QStringLiteral("/krunner-remindme.desktop");
    }

    bool hidden(bool defaultHidden) const
    {
        KDesktopFile file(entryPath());
        return file.desktopGroup().readEntry("Hidden", defaultHidden);
    }

private Q_SLOTS:
    void init()
    {
        tempDir.reset(new QTemporaryDir);
        QVERIFY(tempDir->isValid());
    }

    void cleanup()
    {
        tempDir.reset();
    }

    void testEnableWritesRunnableEntry()
    {
        RemindmeAutostart autostart(tempDir->path());
        autostart.setActive(true);

        QVERIFY(QFile::exists(entryPath()));
        QCOMPARE(hidden(true), false); // enabled
        KDesktopFile file(entryPath());
        QCOMPARE(file.desktopGroup().readEntry("Type"), QStringLiteral("Application"));
        // The Exec argument is double-quoted per the Desktop Entry spec, not single-quoted
        QCOMPARE(file.desktopGroup().readEntry("Exec"), RemindmeAutostart::execArgument(QCoreApplication::applicationFilePath()));
    }

    void testExecArgumentQuoting()
    {
        QCOMPARE(RemindmeAutostart::execArgument(QStringLiteral("/usr/bin/krunner-remindme")), QStringLiteral("\"/usr/bin/krunner-remindme\""));
        QCOMPARE(RemindmeAutostart::execArgument(QStringLiteral("/opt/with space/bin")), QStringLiteral("\"/opt/with space/bin\""));
        // '%' introduces a field code and must be doubled even inside a quoted argument
        QCOMPARE(RemindmeAutostart::execArgument(QStringLiteral("/opt/100%")), QStringLiteral("\"/opt/100%%\""));
        // Double quotes and backslashes are escaped for the spec's quoted form
        QCOMPARE(RemindmeAutostart::execArgument(QStringLiteral("a\"b\\c")), QStringLiteral("\"a\\\"b\\\\c\""));
    }

    void testStaleExecIsRewritten()
    {
        // An entry left by an install under another prefix must be updated to the current path,
        // even though Hidden already matches.
        KDesktopFile file(entryPath());
        KConfigGroup group = file.desktopGroup();
        group.writeEntry("Type", QStringLiteral("Application"));
        group.writeEntry("Exec", QStringLiteral("\"/old/prefix/krunner-remindme\""));
        group.writeEntry("Hidden", false);
        file.sync();

        RemindmeAutostart autostart(tempDir->path());
        autostart.setActive(true);

        KDesktopFile result(entryPath());
        const QString exec = result.desktopGroup().readEntry("Exec");
        QVERIFY(exec != QStringLiteral("\"/old/prefix/krunner-remindme\""));
        QVERIFY(exec.startsWith(QLatin1Char('"')));
    }

    void testDisableHidesEntry()
    {
        RemindmeAutostart autostart(tempDir->path());
        autostart.setActive(true);
        autostart.setActive(false);

        QVERIFY(hidden(false)); // disabled
        // The entry is retained (Hidden=true) so it can be re-enabled without recreating it.
        QVERIFY(QFile::exists(entryPath()));
    }

    void testReenableAfterDisable()
    {
        RemindmeAutostart autostart(tempDir->path());
        autostart.setActive(true);
        autostart.setActive(false);
        autostart.setActive(true);

        QCOMPARE(hidden(true), false); // enabled again
    }

    void testNoRewriteWhenStateUnchanged()
    {
        RemindmeAutostart autostart(tempDir->path());
        autostart.setActive(true); // create the entry with the current Exec

        QFile first(entryPath());
        QVERIFY(first.open(QIODevice::ReadOnly));
        const QByteArray original = first.readAll();
        first.close();

        autostart.setActive(true); // already enabled and current, must not touch the entry

        QFile second(entryPath());
        QVERIFY(second.open(QIODevice::ReadOnly));
        QCOMPARE(second.readAll(), original);
    }
};

QTEST_GUILESS_MAIN(RemindmeAutostartTest)

#include "remindmeautostarttest.moc"

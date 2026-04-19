#include <QtTest>

#include <QFile>
#include <QFileInfo>

#include "diagnostics/VaxilErrorCodes.h"
#include "logging/LoggingService.h"

class LoggingFatalFlushTests : public QObject
{
    Q_OBJECT

private slots:
    void fatalAndCrashPathsEmitCodeAndPersistToDisk();
};

void LoggingFatalFlushTests::fatalAndCrashPathsEmitCodeAndPersistToDisk()
{
    LoggingService logging;
    QVERIFY(logging.initialize());

    const QString fatalCode = VaxilErrorCodes::forKey(VaxilErrorCodes::Key::CrashTerminate);
    const QString crashCode = VaxilErrorCodes::forKey(VaxilErrorCodes::Key::CrashUnhandledException);

    logging.fatalFor(QStringLiteral("route_audit"), QStringLiteral("fatal test message"), fatalCode);
    logging.crashFor(QStringLiteral("crash"), QStringLiteral("crash test message"), crashCode);
    logging.flushAll();

    QFile mainLog(logging.logFilePath());
    QVERIFY(mainLog.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString mainText = QString::fromUtf8(mainLog.readAll());
    QVERIFY(mainText.contains(QStringLiteral("[FATAL][") + fatalCode + QStringLiteral("]")));
    QVERIFY(mainText.contains(QStringLiteral("[CRASH][") + crashCode + QStringLiteral("]")));

    const QFileInfo crashLogInfo(QFileInfo(logging.logFilePath()).absolutePath() + QStringLiteral("/crash.log"));
    QVERIFY(crashLogInfo.exists());
}

QTEST_APPLESS_MAIN(LoggingFatalFlushTests)
#include "LoggingFatalFlushTests.moc"

#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include "diagnostics/CrashReportWriter.h"

class CrashReportWriterTests : public QObject
{
    Q_OBJECT

private slots:
    void writesCrashReportWithBreadcrumbsAndContext();
};

void CrashReportWriterTests::writesCrashReportWithBreadcrumbsAndContext()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    CrashReportWriter writer(tempDir.path());

    CrashReportInput input;
    input.timestampUtc = QStringLiteral("2026-04-19T16:00:00.000Z");
    input.applicationName = QStringLiteral("vaxil");
    input.applicationVersion = QStringLiteral("dev");
    input.buildInfo = QStringLiteral("test-build");
    input.crashType = QStringLiteral("terminate");
    input.errorCode = QStringLiteral("VAXIL-CRASH-0002");
    input.reason = QStringLiteral("std::terminate invoked");
    input.exceptionCode = QStringLiteral("0x00000000");
    input.signalName = QStringLiteral("terminate");
    input.processId = 42;
    input.threadId = 77;
    input.uptimeMs = 3210;
    input.runtimeContext.module = QStringLiteral("route");
    input.runtimeContext.route = QStringLiteral("conversation");
    input.runtimeContext.tool = QStringLiteral("web_search");
    input.runtimeContext.traceId = QStringLiteral("trace-1");
    input.runtimeContext.sessionId = QStringLiteral("session-1");
    input.runtimeContext.threadId = QStringLiteral("thread-1");
    input.minidumpPath = QStringLiteral("dumps/vaxil_20260419_160000.dmp");
    input.relatedLogs = {QStringLiteral("vaxil.log"), QStringLiteral("route_audit.log")};

    CrashBreadcrumb breadcrumb;
    breadcrumb.timestampUtc = QStringLiteral("2026-04-19T15:59:59.000Z");
    breadcrumb.module = QStringLiteral("route");
    breadcrumb.event = QStringLiteral("route.decision.begin");
    breadcrumb.detail = QStringLiteral("kind=conversation");
    breadcrumb.traceId = QStringLiteral("trace-1");
    breadcrumb.sessionId = QStringLiteral("session-1");
    breadcrumb.threadId = QStringLiteral("thread-1");
    breadcrumb.uptimeMs = 3000;
    input.breadcrumbs = {breadcrumb};

    const QString path = writer.writeCrashReport(input);
    QVERIFY(!path.isEmpty());
    QVERIFY(QFile::exists(path));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString text = QString::fromUtf8(file.readAll());
    QVERIFY(text.contains(QStringLiteral("error_code=VAXIL-CRASH-0002")));
    QVERIFY(text.contains(QStringLiteral("[runtime_context]")));
    QVERIFY(text.contains(QStringLiteral("route.decision.begin")));
}

QTEST_APPLESS_MAIN(CrashReportWriterTests)
#include "CrashReportWriterTests.moc"

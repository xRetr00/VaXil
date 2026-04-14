#include <QtTest>

#include "cognition/ConnectorResultSignal.h"

class ConnectorResultSignalTests : public QObject
{
    Q_OBJECT

private slots:
    void detectsSchedulePayload();
    void detectsInboxPayload();
    void detectsResearchPayload();
};

void ConnectorResultSignalTests::detectsSchedulePayload()
{
    BackgroundTaskResult result;
    result.type = QStringLiteral("background_sync");
    result.summary = QStringLiteral("Calendar sync complete.");
    result.payload = QJsonObject{
        {QStringLiteral("connectorKind"), QStringLiteral("schedule")},
        {QStringLiteral("events"), QJsonArray{QJsonObject{{QStringLiteral("title"), QStringLiteral("Design review")}}}},
        {QStringLiteral("title"), QStringLiteral("Design review")}
    };

    const ConnectorResultSignal signal = ConnectorResultSignalBuilder::fromBackgroundTaskResult(result);
    QVERIFY(signal.isValid());
    QCOMPARE(signal.sourceKind, QStringLiteral("connector_result"));
    QCOMPARE(signal.taskType, QStringLiteral("calendar_review"));
    QCOMPARE(signal.connectorKind, QStringLiteral("schedule"));
}

void ConnectorResultSignalTests::detectsInboxPayload()
{
    BackgroundTaskResult result;
    result.type = QStringLiteral("background_sync");
    result.summary = QStringLiteral("Inbox sync complete.");
    result.payload = QJsonObject{
        {QStringLiteral("messages"), QJsonArray{QJsonObject{{QStringLiteral("subject"), QStringLiteral("PR update")}}}},
        {QStringLiteral("primarySender"), QStringLiteral("GitHub")}
    };

    const ConnectorResultSignal signal = ConnectorResultSignalBuilder::fromBackgroundTaskResult(result);
    QVERIFY(signal.isValid());
    QCOMPARE(signal.taskType, QStringLiteral("email_fetch"));
    QCOMPARE(signal.connectorKind, QStringLiteral("inbox"));
}

void ConnectorResultSignalTests::detectsResearchPayload()
{
    BackgroundTaskResult result;
    result.type = QStringLiteral("background_sync");
    result.summary = QStringLiteral("Research sync complete.");
    result.payload = QJsonObject{
        {QStringLiteral("provider"), QStringLiteral("brave")},
        {QStringLiteral("query"), QStringLiteral("OpenAI release updates")},
        {QStringLiteral("sources"), QJsonArray{QJsonObject{{QStringLiteral("url"), QStringLiteral("https://example.com")}}}}
    };

    const ConnectorResultSignal signal = ConnectorResultSignalBuilder::fromBackgroundTaskResult(result);
    QVERIFY(signal.isValid());
    QCOMPARE(signal.taskType, QStringLiteral("web_search"));
    QCOMPARE(signal.connectorKind, QStringLiteral("research"));
}

QTEST_APPLESS_MAIN(ConnectorResultSignalTests)
#include "ConnectorResultSignalTests.moc"

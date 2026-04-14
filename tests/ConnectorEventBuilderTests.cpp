#include <QtTest>

#include "cognition/ConnectorEventBuilder.h"

class ConnectorEventBuilderTests : public QObject
{
    Q_OBJECT

private slots:
    void buildsScheduleConnectorEvent();
    void rejectsUnknownPayload();
};

void ConnectorEventBuilderTests::buildsScheduleConnectorEvent()
{
    BackgroundTaskResult result;
    result.taskId = 7;
    result.taskKey = QStringLiteral("calendar-sync");
    result.type = QStringLiteral("background_sync");
    result.success = true;
    result.summary = QStringLiteral("Calendar sync complete.");
    result.title = QStringLiteral("Calendar");
    result.payload = QJsonObject{
        {QStringLiteral("connectorKind"), QStringLiteral("schedule")},
        {QStringLiteral("events"), QJsonArray{
             QJsonObject{{QStringLiteral("title"), QStringLiteral("Design review")}}
         }},
        {QStringLiteral("title"), QStringLiteral("Design review")}
    };

    const ConnectorEvent event = ConnectorEventBuilder::fromBackgroundTaskResult(result);
    QVERIFY(event.isValid());
    QVERIFY(!event.eventId.trimmed().isEmpty());
    QCOMPARE(event.sourceKind, QStringLiteral("connector_result"));
    QCOMPARE(event.connectorKind, QStringLiteral("schedule"));
    QCOMPARE(event.taskType, QStringLiteral("calendar_review"));
    QCOMPARE(event.taskKey, QStringLiteral("calendar-sync"));
    QCOMPARE(event.taskId, 7);
    QCOMPARE(event.priority, QStringLiteral("medium"));
    QCOMPARE(event.metadata.value(QStringLiteral("resultType")).toString(), QStringLiteral("background_sync"));
    QCOMPARE(event.metadata.value(QStringLiteral("resultSuccess")).toBool(), true);
}

void ConnectorEventBuilderTests::rejectsUnknownPayload()
{
    BackgroundTaskResult result;
    result.type = QStringLiteral("background_sync");
    result.summary = QStringLiteral("Nothing relevant.");
    result.payload = QJsonObject{
        {QStringLiteral("items"), QJsonArray{}}
    };

    const ConnectorEvent event = ConnectorEventBuilder::fromBackgroundTaskResult(result);
    QVERIFY(!event.isValid());
}

QTEST_APPLESS_MAIN(ConnectorEventBuilderTests)
#include "ConnectorEventBuilderTests.moc"

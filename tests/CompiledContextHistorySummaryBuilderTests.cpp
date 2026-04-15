#include <QtTest>

#include "cognition/CompiledContextHistorySummaryBuilder.h"

class CompiledContextHistorySummaryBuilderTests : public QObject
{
    Q_OBJECT

private slots:
    void returnsEmptyWhenNoStableHistory();
    void buildsGlobalAndPurposeSummaries();
    void buildsSelectionHintAndPlannerMetadata();
};

void CompiledContextHistorySummaryBuilderTests::returnsEmptyWhenNoStableHistory()
{
    const QList<MemoryRecord> records = CompiledContextHistorySummaryBuilder::build({}, {}, {}, {});
    QVERIFY(records.isEmpty());
}

void CompiledContextHistorySummaryBuilderTests::buildsGlobalAndPurposeSummaries()
{
    const QList<MemoryRecord> records = CompiledContextHistorySummaryBuilder::build(
        {
            {QStringLiteral("conversation"), QStringLiteral("Conversation context stable for 2 cycles.")},
            {QStringLiteral("agent"), QStringLiteral("Agent context stable for 4 cycles.")}
        },
        {
            {QStringLiteral("conversation"), {QStringLiteral("desktop_context_summary")}},
            {QStringLiteral("agent"), {QStringLiteral("desktop_context_document"), QStringLiteral("connector_summary_schedule")}}
        },
        {
            {QStringLiteral("conversation"), 2},
            {QStringLiteral("agent"), 4}
        },
        {
            {QStringLiteral("conversation"), 45000},
            {QStringLiteral("agent"), 120000}
        });

    QVERIFY(!records.isEmpty());
    QCOMPARE(records.first().key, QStringLiteral("compiled_context_history_global"));
    QVERIFY(records.first().value.contains(QStringLiteral("agent(4 cycles, 120000 ms)")));

    bool foundAgent = false;
    bool foundConversation = false;
    for (const MemoryRecord &record : records) {
        if (record.key == QStringLiteral("compiled_context_history_agent")) {
            foundAgent = true;
            QVERIFY(record.value.contains(QStringLiteral("connector_summary_schedule")));
        }
        if (record.key == QStringLiteral("compiled_context_history_conversation")) {
            foundConversation = true;
        }
    }

    QVERIFY(foundAgent);
    QVERIFY(foundConversation);
}

void CompiledContextHistorySummaryBuilderTests::buildsSelectionHintAndPlannerMetadata()
{
    const QList<MemoryRecord> records = CompiledContextHistorySummaryBuilder::build(
        {
            {QStringLiteral("agent"), QStringLiteral("Agent context stable for 4 cycles.")}
        },
        {
            {QStringLiteral("agent"), {QStringLiteral("desktop_context_document"), QStringLiteral("connector_summary_schedule")}}
        },
        {
            {QStringLiteral("agent"), 4}
        },
        {
            {QStringLiteral("agent"), 120000}
        });

    const QString hint = CompiledContextHistorySummaryBuilder::buildSelectionHint(records);
    QVERIFY(hint.contains(QStringLiteral("Long-horizon compiled context history")));

    const QVariantMap metadata = CompiledContextHistorySummaryBuilder::buildPlannerMetadata(records);
    QVERIFY(metadata.value(QStringLiteral("compiledContextHistorySummary")).toString().contains(QStringLiteral("Long-horizon")));
    QVERIFY(metadata.value(QStringLiteral("compiledContextHistoryHasSchedule")).toBool());
    QVERIFY(metadata.value(QStringLiteral("compiledContextHistoryHasDocument")).toBool());
}

QTEST_APPLESS_MAIN(CompiledContextHistorySummaryBuilderTests)
#include "CompiledContextHistorySummaryBuilderTests.moc"

#include <QtTest>

#include <QTemporaryDir>

#include "cognition/CompiledContextHistoryPolicy.h"
#include "core/MemoryPolicyHandler.h"
#include "memory/MemoryStore.h"

class MemoryPolicyHandlerTests : public QObject
{
    Q_OBJECT

private slots:
    void requestMemoryIncludesConnectorSummaries();
    void requestMemoryIncludesCompiledContextPolicy();
    void readsCompiledContextPolicyDecision();
    void buildsCompiledContextPolicySummaryRecords();
    void buildsCompiledContextLayeredMemoryRecords();
    void buildsCompiledContextPolicyEvolutionRecords();
};

void MemoryPolicyHandlerTests::requestMemoryIncludesConnectorSummaries()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MemoryStore store(dir.path() + QStringLiteral("/memory.json"));
    MemoryPolicyHandler handler(nullptr, &store);

    QVERIFY(store.upsertConnectorState(QStringLiteral("connector:inbox:priority"), {
        {QStringLiteral("historyKey"), QStringLiteral("connector:inbox:priority")},
        {QStringLiteral("connectorKind"), QStringLiteral("inbox")},
        {QStringLiteral("sourceKind"), QStringLiteral("connector_inbox_maildrop")},
        {QStringLiteral("seenCount"), 5},
        {QStringLiteral("presentedCount"), 2},
        {QStringLiteral("historyRecentlySeen"), true},
        {QStringLiteral("historyRecentlyPresented"), true},
        {QStringLiteral("lastSeenAtMs"), 4800}
    }));

    const QList<MemoryRecord> memory = handler.requestMemory(QStringLiteral("inbox"), {});
    bool foundSummary = false;
    for (const MemoryRecord &record : memory) {
        if (record.source == QStringLiteral("connector_summary")
            && record.key == QStringLiteral("connector_summary_inbox")) {
            foundSummary = true;
            QVERIFY(record.value.contains(QStringLiteral("Inbox activity")));
        }
    }
    QVERIFY(foundSummary);
}

void MemoryPolicyHandlerTests::requestMemoryIncludesCompiledContextPolicy()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MemoryStore store(dir.path() + QStringLiteral("/memory.json"));
    MemoryPolicyHandler handler(nullptr, &store);

    QVERIFY(store.upsertCompiledContextPolicyState({
        {QStringLiteral("dominantMode"), QStringLiteral("document_work")},
        {QStringLiteral("selectionDirective"), QStringLiteral("History policy: stable document-focused work is ongoing.")},
        {QStringLiteral("promptDirective"), QStringLiteral("Stable mode: document-focused work remains active.")},
        {QStringLiteral("reasonCode"), QStringLiteral("compiled_history_policy.document_work")},
        {QStringLiteral("strength"), 2.4},
        {QStringLiteral("updatedAtMs"), 5100}
    }));

    const QList<MemoryRecord> memory = handler.requestMemory(QStringLiteral("document"), {});
    bool foundPolicy = false;
    for (const MemoryRecord &record : memory) {
        if (record.source == QStringLiteral("compiled_history_policy_memory")
            && record.key == QStringLiteral("compiled_context_history_mode")) {
            foundPolicy = true;
            QVERIFY(record.value.contains(QStringLiteral("document_work")));
        }
    }
    QVERIFY(foundPolicy);
}

void MemoryPolicyHandlerTests::readsCompiledContextPolicyDecision()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MemoryStore store(dir.path() + QStringLiteral("/memory.json"));
    MemoryPolicyHandler handler(nullptr, &store);

    QVERIFY(store.upsertCompiledContextPolicyState({
        {QStringLiteral("dominantMode"), QStringLiteral("research_analysis")},
        {QStringLiteral("selectionDirective"), QStringLiteral("History policy: stable research analysis is ongoing.")},
        {QStringLiteral("promptDirective"), QStringLiteral("Stable mode: research analysis remains active.")},
        {QStringLiteral("reasonCode"), QStringLiteral("compiled_history_policy.research_analysis")},
        {QStringLiteral("strength"), 2.9},
        {QStringLiteral("compiledContextHistoryMode"), QStringLiteral("research_analysis")},
        {QStringLiteral("updatedAtMs"), 6100}
    }));

    const CompiledContextHistoryPolicyDecision decision = handler.compiledContextPolicyDecision();
    QCOMPARE(decision.dominantMode, QStringLiteral("research_analysis"));
    QVERIFY(decision.selectionDirective.contains(QStringLiteral("research analysis")));
}

void MemoryPolicyHandlerTests::buildsCompiledContextPolicySummaryRecords()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MemoryStore store(dir.path() + QStringLiteral("/memory.json"));
    MemoryPolicyHandler handler(nullptr, &store);

    QVERIFY(store.upsertConnectorState(QStringLiteral("connector:inbox:priority"), {
        {QStringLiteral("historyKey"), QStringLiteral("connector:inbox:priority")},
        {QStringLiteral("connectorKind"), QStringLiteral("inbox")},
        {QStringLiteral("sourceKind"), QStringLiteral("connector_inbox_maildrop")},
        {QStringLiteral("seenCount"), 6},
        {QStringLiteral("presentedCount"), 3}
    }));
    QVERIFY(store.upsertCompiledContextPolicyState({
        {QStringLiteral("dominantMode"), QStringLiteral("inbox_triage")},
        {QStringLiteral("selectionDirective"), QStringLiteral("History policy: stable inbox triage is ongoing.")},
        {QStringLiteral("promptDirective"), QStringLiteral("Stable mode: inbox triage remains active.")},
        {QStringLiteral("reasonCode"), QStringLiteral("compiled_history_policy.inbox_triage")},
        {QStringLiteral("strength"), 2.7},
        {QStringLiteral("updatedAtMs"), 8100}
    }));

    const QList<MemoryRecord> records = handler.compiledContextPolicySummaryRecords();
    QVERIFY(records.size() >= 2);
    QCOMPARE(records.first().source, QStringLiteral("compiled_history_policy_summary"));

    bool foundSources = false;
    for (const MemoryRecord &record : records) {
        if (record.key == QStringLiteral("compiled_context_policy_sources")) {
            foundSources = true;
            QVERIFY(record.value.contains(QStringLiteral("connector_inbox_maildrop")));
        }
    }
    QVERIFY(foundSources);
}

void MemoryPolicyHandlerTests::buildsCompiledContextLayeredMemoryRecords()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MemoryStore store(dir.path() + QStringLiteral("/memory.json"));
    MemoryPolicyHandler handler(nullptr, &store);

    QVERIFY(store.upsertConnectorState(QStringLiteral("connector:research:bookmarks"), {
        {QStringLiteral("historyKey"), QStringLiteral("connector:research:bookmarks")},
        {QStringLiteral("connectorKind"), QStringLiteral("research")},
        {QStringLiteral("sourceKind"), QStringLiteral("connector_research_browser")},
        {QStringLiteral("seenCount"), 5},
        {QStringLiteral("presentedCount"), 2}
    }));
    QVERIFY(store.upsertCompiledContextPolicyState({
        {QStringLiteral("dominantMode"), QStringLiteral("research_analysis")},
        {QStringLiteral("selectionDirective"), QStringLiteral("History policy: stable research analysis is ongoing.")},
        {QStringLiteral("promptDirective"), QStringLiteral("Stable mode: research analysis remains active.")},
        {QStringLiteral("reasonCode"), QStringLiteral("compiled_history_policy.research_analysis")},
        {QStringLiteral("strength"), 2.9},
        {QStringLiteral("updatedAtMs"), 9100}
    }));

    const QList<MemoryRecord> records = handler.compiledContextLayeredMemoryRecords();
    QVERIFY(records.size() >= 2);

    bool foundSummary = false;
    bool foundContinuity = false;
    for (const MemoryRecord &record : records) {
        if (record.key == QStringLiteral("compiled_context_layered_summary")) {
            foundSummary = true;
            QVERIFY(record.value.contains(QStringLiteral("research_analysis")));
        }
        if (record.key == QStringLiteral("compiled_context_layered_continuity")) {
            foundContinuity = true;
            QVERIFY(record.value.contains(QStringLiteral("connector_research_browser")));
        }
    }
    QVERIFY(foundSummary);
    QVERIFY(foundContinuity);
}

void MemoryPolicyHandlerTests::buildsCompiledContextPolicyEvolutionRecords()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MemoryStore store(dir.path() + QStringLiteral("/memory.json"));
    MemoryPolicyHandler handler(nullptr, &store);

    QVERIFY(store.upsertCompiledContextPolicyState({
        {QStringLiteral("dominantMode"), QStringLiteral("document_work")},
        {QStringLiteral("selectionDirective"), QStringLiteral("History policy: stable document-focused work is ongoing.")},
        {QStringLiteral("promptDirective"), QStringLiteral("Stable mode: document-focused work remains active.")},
        {QStringLiteral("reasonCode"), QStringLiteral("compiled_history_policy.document_work")},
        {QStringLiteral("strength"), 2.3},
        {QStringLiteral("updatedAtMs"), 7100}
    }));
    QVERIFY(store.upsertCompiledContextPolicyState({
        {QStringLiteral("dominantMode"), QStringLiteral("research_analysis")},
        {QStringLiteral("selectionDirective"), QStringLiteral("History policy: stable research analysis is ongoing.")},
        {QStringLiteral("promptDirective"), QStringLiteral("Stable mode: research analysis remains active.")},
        {QStringLiteral("reasonCode"), QStringLiteral("compiled_history_policy.research_analysis")},
        {QStringLiteral("strength"), 2.9},
        {QStringLiteral("updatedAtMs"), 9100}
    }));

    const QList<MemoryRecord> records = handler.compiledContextPolicyEvolutionRecords();
    QVERIFY(records.size() >= 1);
    QVERIFY(records.first().value.contains(QStringLiteral("document_work -> research_analysis")));
}

QTEST_APPLESS_MAIN(MemoryPolicyHandlerTests)
#include "MemoryPolicyHandlerTests.moc"

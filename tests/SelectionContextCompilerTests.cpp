#include <QtTest>

#include <QTemporaryDir>

#include "cognition/CompiledContextHistorySummaryBuilder.h"
#include "cognition/SelectionContextCompiler.h"
#include "core/AssistantBehaviorPolicy.h"
#include "core/MemoryPolicyHandler.h"
#include "memory/MemoryStore.h"

class SelectionContextCompilerTests : public QObject
{
    Q_OBJECT

private slots:
    void compilesDesktopAndConnectorContextTogether();
    void selectsPromptContextBlocksByIntent();
    void integratesCompiledContextHistoryPolicy();
    void prefersPersistedCompiledContextPolicy();
    void includesPersistedPolicySummaryRecordsInCompiledContext();
    void includesLayeredMemoryRecordsInCompiledContext();
    void usesLayeredMemorySignalsInSelectionAndPromptContext();
    void usesPolicyEvolutionSignalsInSelectionAndPromptContext();
};

void SelectionContextCompilerTests::compilesDesktopAndConnectorContextTogether()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MemoryStore store(dir.path() + QStringLiteral("/memory.json"));
    QVERIFY(store.upsertConnectorState(QStringLiteral("connector:schedule:today"), {
        {QStringLiteral("historyKey"), QStringLiteral("connector:schedule:today")},
        {QStringLiteral("connectorKind"), QStringLiteral("schedule")},
        {QStringLiteral("sourceKind"), QStringLiteral("connector_schedule_calendar")},
        {QStringLiteral("firstSeenAtMs"), 1000},
        {QStringLiteral("seenCount"), 3},
        {QStringLiteral("presentedCount"), 1},
        {QStringLiteral("historyRecentlySeen"), true},
        {QStringLiteral("historyRecentlyPresented"), false},
        {QStringLiteral("lastPresentedAtMs"), 1800},
        {QStringLiteral("lastSeenAtMs"), 2000}
    }));

    MemoryPolicyHandler memoryPolicy(nullptr, &store);
    AssistantBehaviorPolicy behaviorPolicy;
    const QVariantMap desktopContext = {
        {QStringLiteral("taskId"), QStringLiteral("browser_tab")},
        {QStringLiteral("topic"), QStringLiteral("release_notes")},
        {QStringLiteral("appId"), QStringLiteral("edge")},
        {QStringLiteral("documentContext"), QStringLiteral("Vaxil Release Notes")},
        {QStringLiteral("siteContext"), QStringLiteral("github.com")}
    };

    const SelectionContextCompilation compilation = SelectionContextCompiler::compile(
        QStringLiteral("what should I check next"),
        IntentType::GENERAL_CHAT,
        desktopContext,
        QStringLiteral("Desktop context: browser tab \"Vaxil Release Notes\" on github.com in edge."),
        QDateTime::currentMSecsSinceEpoch(),
        false,
        {},
        {},
        &memoryPolicy,
        &behaviorPolicy);

    QVERIFY(!compilation.compiledContextRecords.isEmpty());
    QVERIFY(!compilation.selectedMemoryRecords.isEmpty());
    QVERIFY(compilation.compiledDesktopSummary.contains(QStringLiteral("Document: Vaxil Release Notes.")));
    QVERIFY(compilation.selectionInput.contains(QStringLiteral("Current desktop context:")));
    QVERIFY(compilation.promptContext.contains(QStringLiteral("Task type: browser_tab.")));

    bool foundDesktopSummary = false;
    for (const MemoryRecord &record : compilation.compiledContextRecords) {
        if (record.key == QStringLiteral("desktop_context_summary")) {
            foundDesktopSummary = true;
            QCOMPARE(record.source, QStringLiteral("desktop_context"));
        }
    }
    QVERIFY(foundDesktopSummary);

    bool foundConnectorSummary = false;
    for (const MemoryRecord &record : compilation.memoryContext.activeCommitments) {
        if (record.key == QStringLiteral("connector_summary_schedule")) {
            foundConnectorSummary = true;
        }
    }
    QVERIFY(foundConnectorSummary);
}

void SelectionContextCompilerTests::selectsPromptContextBlocksByIntent()
{
    const QVariantMap desktopContext = {
        {QStringLiteral("taskId"), QStringLiteral("editor_document")},
        {QStringLiteral("topic"), QStringLiteral("cooldown_engine")},
        {QStringLiteral("appId"), QStringLiteral("cursor")},
        {QStringLiteral("documentContext"), QStringLiteral("CooldownEngine.cpp")},
        {QStringLiteral("workspaceContext"), QStringLiteral("D:/Vaxil/src/cognition")},
        {QStringLiteral("siteContext"), QStringLiteral("github.com")},
        {QStringLiteral("threadId"), QStringLiteral("editor_document:cooldown_engine")}
    };

    const QString promptContext = SelectionContextCompiler::buildPromptContext(
        IntentType::WRITE_FILE,
        QStringLiteral("Desktop context: editing CooldownEngine.cpp in cursor."),
        desktopContext,
        {});

    QVERIFY(promptContext.contains(QStringLiteral("Task type: editor_document.")));
    QVERIFY(promptContext.contains(QStringLiteral("Document: CooldownEngine.cpp.")));
    QVERIFY(promptContext.contains(QStringLiteral("Workspace: D:/Vaxil/src/cognition.")));
    QVERIFY(promptContext.contains(QStringLiteral("App: cursor.")));
    QVERIFY(!promptContext.contains(QStringLiteral("Site: github.com.")));

    const SelectionContextCompilation compilation = SelectionContextCompiler::compile(
        QStringLiteral("tighten cooldown gating"),
        IntentType::WRITE_FILE,
        desktopContext,
        QStringLiteral("Desktop context: editing CooldownEngine.cpp in cursor."),
        QDateTime::currentMSecsSinceEpoch(),
        false,
        {},
        {},
        nullptr,
        nullptr);

    QStringList promptKeys;
    QStringList promptReasons;
    for (const MemoryRecord &record : compilation.promptContextRecords) {
        promptKeys.push_back(record.key);
        promptReasons.push_back(record.source);
    }

    QVERIFY(promptKeys.contains(QStringLiteral("desktop_prompt_document")));
    QVERIFY(promptKeys.contains(QStringLiteral("desktop_prompt_workspace")));
    QVERIFY(promptKeys.contains(QStringLiteral("desktop_prompt_app")));
    QVERIFY(!promptKeys.contains(QStringLiteral("desktop_prompt_site")));
    QVERIFY(promptReasons.contains(QStringLiteral("prompt.document_relevance")));
    QVERIFY(promptReasons.contains(QStringLiteral("prompt.workspace_relevance")));
}

void SelectionContextCompilerTests::integratesCompiledContextHistoryPolicy()
{
    const QList<MemoryRecord> historyRecords = CompiledContextHistorySummaryBuilder::build(
        {
            {QStringLiteral("WRITE_FILE"),
             QStringLiteral("Stable compiled context: desktop_context_document CooldownEngine.cpp connector_summary_research.")},
            {QStringLiteral("GENERAL_CHAT"),
             QStringLiteral("Stable compiled context: desktop_context_document CooldownEngine.cpp connector_summary_research.")}
        },
        {
            {QStringLiteral("WRITE_FILE"), {QStringLiteral("desktop_context_document"), QStringLiteral("connector_summary_research")}},
            {QStringLiteral("GENERAL_CHAT"), {QStringLiteral("desktop_context_document"), QStringLiteral("connector_summary_research")}}
        },
        {
            {QStringLiteral("WRITE_FILE"), 4},
            {QStringLiteral("GENERAL_CHAT"), 3}
        },
        {
            {QStringLiteral("WRITE_FILE"), 3600000},
            {QStringLiteral("GENERAL_CHAT"), 1800000}
        });

    const QVariantMap desktopContext = {
        {QStringLiteral("taskId"), QStringLiteral("editor_document")},
        {QStringLiteral("topic"), QStringLiteral("cooldown_engine")},
        {QStringLiteral("appId"), QStringLiteral("cursor")},
        {QStringLiteral("documentContext"), QStringLiteral("CooldownEngine.cpp")},
        {QStringLiteral("workspaceContext"), QStringLiteral("D:/Vaxil/src/cognition")}
    };

    const SelectionContextCompilation compilation = SelectionContextCompiler::compile(
        QStringLiteral("tighten cooldown gating"),
        IntentType::WRITE_FILE,
        desktopContext,
        QStringLiteral("Desktop context: editing CooldownEngine.cpp in cursor."),
        QDateTime::currentMSecsSinceEpoch(),
        false,
        {},
        historyRecords,
        nullptr,
        nullptr);

    QVERIFY(compilation.selectionInput.contains(QStringLiteral("History policy: stable document-focused work is ongoing.")));
    QVERIFY(compilation.promptContext.contains(QStringLiteral("Stable mode: document-focused work remains active.")));
    QCOMPARE(compilation.historyPolicyMetadata.value(QStringLiteral("compiledContextHistoryMode")).toString(),
             QStringLiteral("document_work"));

    bool foundHistoryModeRecord = false;
    for (const MemoryRecord &record : compilation.compiledContextRecords) {
        if (record.key == QStringLiteral("compiled_context_history_mode")) {
            foundHistoryModeRecord = true;
            QCOMPARE(record.source, QStringLiteral("compiled_history_policy"));
        }
    }
    QVERIFY(foundHistoryModeRecord);
}

void SelectionContextCompilerTests::prefersPersistedCompiledContextPolicy()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MemoryStore store(dir.path() + QStringLiteral("/memory.json"));
    MemoryPolicyHandler memoryPolicy(nullptr, &store);

    QVERIFY(store.upsertCompiledContextPolicyState({
        {QStringLiteral("dominantMode"), QStringLiteral("inbox_triage")},
        {QStringLiteral("selectionDirective"), QStringLiteral("History policy: stable inbox triage is ongoing.")},
        {QStringLiteral("promptDirective"), QStringLiteral("Stable mode: inbox triage remains active.")},
        {QStringLiteral("reasonCode"), QStringLiteral("compiled_history_policy.inbox_triage")},
        {QStringLiteral("strength"), 2.6},
        {QStringLiteral("compiledContextHistoryMode"), QStringLiteral("inbox_triage")},
        {QStringLiteral("compiledContextHistoryModeStrength"), 2.6},
        {QStringLiteral("updatedAtMs"), 7300}
    }));

    const QVariantMap desktopContext = {
        {QStringLiteral("taskId"), QStringLiteral("browser_tab")},
        {QStringLiteral("topic"), QStringLiteral("mail_review")},
        {QStringLiteral("appId"), QStringLiteral("edge")},
        {QStringLiteral("siteContext"), QStringLiteral("mail.example.com")}
    };

    const SelectionContextCompilation compilation = SelectionContextCompiler::compile(
        QStringLiteral("what matters here"),
        IntentType::GENERAL_CHAT,
        desktopContext,
        QStringLiteral("Desktop context: browser tab mail.example.com in edge."),
        QDateTime::currentMSecsSinceEpoch(),
        false,
        {},
        {},
        &memoryPolicy,
        nullptr);

    QCOMPARE(compilation.historyPolicyMetadata.value(QStringLiteral("compiledContextHistoryMode")).toString(),
             QStringLiteral("inbox_triage"));
    QVERIFY(compilation.selectionInput.contains(QStringLiteral("stable inbox triage is ongoing")));
    QVERIFY(compilation.promptContext.contains(QStringLiteral("inbox triage remains active")));
}

void SelectionContextCompilerTests::includesPersistedPolicySummaryRecordsInCompiledContext()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MemoryStore store(dir.path() + QStringLiteral("/memory.json"));
    MemoryPolicyHandler memoryPolicy(nullptr, &store);

    QVERIFY(store.upsertConnectorState(QStringLiteral("connector:schedule:today"), {
        {QStringLiteral("historyKey"), QStringLiteral("connector:schedule:today")},
        {QStringLiteral("connectorKind"), QStringLiteral("schedule")},
        {QStringLiteral("sourceKind"), QStringLiteral("connector_schedule_calendar")},
        {QStringLiteral("seenCount"), 4},
        {QStringLiteral("presentedCount"), 1}
    }));
    QVERIFY(store.upsertCompiledContextPolicyState({
        {QStringLiteral("dominantMode"), QStringLiteral("schedule_coordination")},
        {QStringLiteral("selectionDirective"), QStringLiteral("History policy: stable schedule coordination is ongoing.")},
        {QStringLiteral("promptDirective"), QStringLiteral("Stable mode: schedule coordination remains active.")},
        {QStringLiteral("reasonCode"), QStringLiteral("compiled_history_policy.schedule_coordination")},
        {QStringLiteral("strength"), 2.8},
        {QStringLiteral("compiledContextHistoryMode"), QStringLiteral("schedule_coordination")},
        {QStringLiteral("updatedAtMs"), 9100}
    }));

    const SelectionContextCompilation compilation = SelectionContextCompiler::compile(
        QStringLiteral("what should I prepare"),
        IntentType::GENERAL_CHAT,
        {
            {QStringLiteral("taskId"), QStringLiteral("browser_tab")},
            {QStringLiteral("topic"), QStringLiteral("meeting_prep")},
            {QStringLiteral("appId"), QStringLiteral("edge")}
        },
        QStringLiteral("Desktop context: browser tab in edge."),
        QDateTime::currentMSecsSinceEpoch(),
        false,
        {},
        {},
        &memoryPolicy,
        nullptr);

    bool foundSummary = false;
    bool foundSources = false;
    for (const MemoryRecord &record : compilation.compiledContextRecords) {
        if (record.key == QStringLiteral("compiled_context_policy_summary")) {
            foundSummary = true;
        }
        if (record.key == QStringLiteral("compiled_context_policy_sources")) {
            foundSources = true;
            QVERIFY(record.value.contains(QStringLiteral("connector_schedule_calendar")));
        }
    }
    QVERIFY(foundSummary);
    QVERIFY(foundSources);
}

void SelectionContextCompilerTests::includesLayeredMemoryRecordsInCompiledContext()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MemoryStore store(dir.path() + QStringLiteral("/memory.json"));
    MemoryPolicyHandler memoryPolicy(nullptr, &store);

    QVERIFY(store.upsertConnectorState(QStringLiteral("connector:inbox:maildrop"), {
        {QStringLiteral("historyKey"), QStringLiteral("connector:inbox:maildrop")},
        {QStringLiteral("connectorKind"), QStringLiteral("inbox")},
        {QStringLiteral("sourceKind"), QStringLiteral("connector_inbox_maildrop")},
        {QStringLiteral("seenCount"), 7},
        {QStringLiteral("presentedCount"), 3}
    }));
    QVERIFY(store.upsertCompiledContextPolicyState({
        {QStringLiteral("dominantMode"), QStringLiteral("inbox_triage")},
        {QStringLiteral("selectionDirective"), QStringLiteral("History policy: stable inbox triage is ongoing.")},
        {QStringLiteral("promptDirective"), QStringLiteral("Stable mode: inbox triage remains active.")},
        {QStringLiteral("reasonCode"), QStringLiteral("compiled_history_policy.inbox_triage")},
        {QStringLiteral("strength"), 2.8},
        {QStringLiteral("compiledContextHistoryMode"), QStringLiteral("inbox_triage")},
        {QStringLiteral("updatedAtMs"), 9900}
    }));

    const SelectionContextCompilation compilation = SelectionContextCompiler::compile(
        QStringLiteral("what should I answer first"),
        IntentType::GENERAL_CHAT,
        {
            {QStringLiteral("taskId"), QStringLiteral("browser_tab")},
            {QStringLiteral("topic"), QStringLiteral("mail_review")},
            {QStringLiteral("appId"), QStringLiteral("edge")}
        },
        QStringLiteral("Desktop context: browser tab in edge."),
        QDateTime::currentMSecsSinceEpoch(),
        false,
        {},
        {},
        &memoryPolicy,
        nullptr);

    bool foundLayeredSummary = false;
    bool foundLayeredContinuity = false;
    for (const MemoryRecord &record : compilation.compiledContextRecords) {
        if (record.key == QStringLiteral("compiled_context_layered_summary")) {
            foundLayeredSummary = true;
            QVERIFY(record.value.contains(QStringLiteral("inbox_triage")));
        }
        if (record.key == QStringLiteral("compiled_context_layered_continuity")) {
            foundLayeredContinuity = true;
            QVERIFY(record.value.contains(QStringLiteral("connector_inbox_maildrop")));
        }
    }
    QVERIFY(foundLayeredSummary);
    QVERIFY(foundLayeredContinuity);
}

void SelectionContextCompilerTests::usesLayeredMemorySignalsInSelectionAndPromptContext()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MemoryStore store(dir.path() + QStringLiteral("/memory.json"));
    MemoryPolicyHandler memoryPolicy(nullptr, &store);

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
        {QStringLiteral("compiledContextHistoryMode"), QStringLiteral("research_analysis")},
        {QStringLiteral("updatedAtMs"), 10010}
    }));

    const SelectionContextCompilation compilation = SelectionContextCompiler::compile(
        QStringLiteral("what should I read next"),
        IntentType::GENERAL_CHAT,
        {
            {QStringLiteral("taskId"), QStringLiteral("browser_tab")},
            {QStringLiteral("topic"), QStringLiteral("papers")},
            {QStringLiteral("appId"), QStringLiteral("edge")}
        },
        QStringLiteral("Desktop context: browser tab in edge."),
        QDateTime::currentMSecsSinceEpoch(),
        false,
        {},
        {},
        &memoryPolicy,
        nullptr);

    QVERIFY(compilation.selectionInput.contains(QStringLiteral("research analysis remains active")));
    QVERIFY(compilation.promptContext.contains(QStringLiteral("connector_research_browser")));
}

void SelectionContextCompilerTests::usesPolicyEvolutionSignalsInSelectionAndPromptContext()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MemoryStore store(dir.path() + QStringLiteral("/memory.json"));
    MemoryPolicyHandler memoryPolicy(nullptr, &store);

    QVERIFY(store.upsertCompiledContextPolicyState({
        {QStringLiteral("dominantMode"), QStringLiteral("document_work")},
        {QStringLiteral("selectionDirective"), QStringLiteral("History policy: stable document-focused work is ongoing.")},
        {QStringLiteral("promptDirective"), QStringLiteral("Stable mode: document-focused work remains active.")},
        {QStringLiteral("reasonCode"), QStringLiteral("compiled_history_policy.document_work")},
        {QStringLiteral("strength"), 2.2},
        {QStringLiteral("updatedAtMs"), 8100}
    }));
    QVERIFY(store.upsertCompiledContextPolicyState({
        {QStringLiteral("dominantMode"), QStringLiteral("research_analysis")},
        {QStringLiteral("selectionDirective"), QStringLiteral("History policy: stable research analysis is ongoing.")},
        {QStringLiteral("promptDirective"), QStringLiteral("Stable mode: research analysis remains active.")},
        {QStringLiteral("reasonCode"), QStringLiteral("compiled_history_policy.research_analysis")},
        {QStringLiteral("strength"), 2.9},
        {QStringLiteral("updatedAtMs"), 10100}
    }));

    const SelectionContextCompilation compilation = SelectionContextCompiler::compile(
        QStringLiteral("what changed in my work"),
        IntentType::GENERAL_CHAT,
        {
            {QStringLiteral("taskId"), QStringLiteral("browser_tab")},
            {QStringLiteral("topic"), QStringLiteral("papers")},
            {QStringLiteral("appId"), QStringLiteral("edge")}
        },
        QStringLiteral("Desktop context: browser tab in edge."),
        QDateTime::currentMSecsSinceEpoch(),
        false,
        {},
        {},
        &memoryPolicy,
        nullptr);

    QVERIFY(compilation.selectionInput.contains(QStringLiteral("Policy evolution")));
    QVERIFY(compilation.promptContext.contains(QStringLiteral("document_work -> research_analysis")));
}

QTEST_APPLESS_MAIN(SelectionContextCompilerTests)
#include "SelectionContextCompilerTests.moc"

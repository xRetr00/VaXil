#include <QtTest>

#include <QTemporaryDir>

#include "cognition/SelectionContextCompiler.h"
#include "core/AssistantBehaviorPolicy.h"
#include "core/MemoryPolicyHandler.h"
#include "memory/MemoryStore.h"

class SelectionContextCompilerTests : public QObject
{
    Q_OBJECT

private slots:
    void compilesDesktopAndConnectorContextTogether();
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

QTEST_APPLESS_MAIN(SelectionContextCompilerTests)
#include "SelectionContextCompilerTests.moc"

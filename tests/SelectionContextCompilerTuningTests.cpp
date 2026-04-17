#include <QtTest>

#include <QTemporaryDir>

#include "cognition/SelectionContextCompiler.h"
#include "core/MemoryPolicyHandler.h"
#include "memory/MemoryStore.h"

class SelectionContextCompilerTuningTests : public QObject
{
    Q_OBJECT

private slots:
    void usesPolicyTuningSignalsInSelectionAndPromptContext();
};

void SelectionContextCompilerTuningTests::usesPolicyTuningSignalsInSelectionAndPromptContext()
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
    QVERIFY(store.upsertCompiledContextPolicyState({
        {QStringLiteral("dominantMode"), QStringLiteral("research_analysis")},
        {QStringLiteral("selectionDirective"), QStringLiteral("History policy: stable research analysis is ongoing.")},
        {QStringLiteral("promptDirective"), QStringLiteral("Stable mode: research analysis remains active.")},
        {QStringLiteral("reasonCode"), QStringLiteral("compiled_history_policy.research_analysis")},
        {QStringLiteral("strength"), 3.0},
        {QStringLiteral("updatedAtMs"), 10100}
    }));
    QVERIFY(store.promoteCompiledContextPolicyTuningState({
        {QStringLiteral("tuningCurrentMode"), QStringLiteral("research_analysis")},
        {QStringLiteral("tuningVolatilityLevel"), QStringLiteral("elevated")},
        {QStringLiteral("tuningAlignmentBoost"), 0.10},
        {QStringLiteral("tuningDefocusPenalty"), 0.08},
        {QStringLiteral("tuningVolatilityPenalty"), 0.08},
        {QStringLiteral("tuningSuppressionScoreThreshold"), 0.78},
        {QStringLiteral("tuningObservedCount"), 3},
        {QStringLiteral("tuningShiftCount"), 3},
        {QStringLiteral("tuningTotalObservations"), 8},
        {QStringLiteral("tuningPromotionAction"), QStringLiteral("promote")},
        {QStringLiteral("tuningPromotionReason"), QStringLiteral("behavior_tuning.test_seed")},
        {QStringLiteral("updatedAtMs"), 10200}
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

    QVERIFY(compilation.selectionInput.contains(QStringLiteral("Tuning signal:")));
    QVERIFY(compilation.selectionInput.contains(QStringLiteral("Tuning episode:")));
    QVERIFY(compilation.promptContext.contains(QStringLiteral("Tuning signal:")));
    QVERIFY(compilation.promptContext.contains(QStringLiteral("Tuning episode:")));

    bool foundTuning = false;
    bool foundKnobs = false;
    bool foundEpisode = false;
    for (const MemoryRecord &record : compilation.compiledContextRecords) {
        if (record.key == QStringLiteral("compiled_context_policy_tuning_signal")) {
            foundTuning = true;
        }
        if (record.key == QStringLiteral("compiled_context_policy_tuning_knobs")) {
            foundKnobs = true;
        }
        if (record.source == QStringLiteral("compiled_history_policy_tuning_episode")) {
            foundEpisode = true;
        }
    }
    QVERIFY(foundTuning);
    QVERIFY(foundKnobs);
    QVERIFY(foundEpisode);
}

QTEST_APPLESS_MAIN(SelectionContextCompilerTuningTests)
#include "SelectionContextCompilerTuningTests.moc"

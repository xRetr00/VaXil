#include <QtTest>

#include <QTemporaryDir>

#include "memory/MemoryStore.h"

class MemoryStoreTests : public QObject
{
    Q_OBJECT

private slots:
    void sanitizesAssistantConversationHistoryBeforeReuse();
    void returnsConnectorMemoryRecordsForRelevantQuery();
    void persistsCompiledContextPolicyMemory();
    void persistsCompiledContextPolicyHistory();
    void persistsAndRollsBackCompiledContextPolicyTuningState();
    void recordsCompiledContextPolicyTuningEpisodes();
    void persistsFeedbackSignalsAndBuildsAggregateMemory();
    void scoresTuningEpisodesAgainstFeedbackSignals();
};

void MemoryStoreTests::sanitizesAssistantConversationHistoryBeforeReuse()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryStore store(dir.path() + QStringLiteral("/memory.json"));

    store.appendConversation(
        QStringLiteral("assistant"),
        QStringLiteral("Tone: concise, confident, non-robotic.\nYou are Vaxil.\n<answer>Hello there.</answer>"));

    const QList<AiMessage> history = store.recentMessages(4);
    QCOMPARE(history.size(), 1);
    QCOMPARE(history.first().role, QStringLiteral("assistant"));
    QVERIFY(!history.first().content.contains(QStringLiteral("Tone:"), Qt::CaseInsensitive));
    QVERIFY(!history.first().content.contains(QStringLiteral("You are Vaxil"), Qt::CaseInsensitive));
    QVERIFY(history.first().content.contains(QStringLiteral("Hello there.")));
}

void MemoryStoreTests::returnsConnectorMemoryRecordsForRelevantQuery()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryStore store(dir.path() + QStringLiteral("/memory.json"));

    QVERIFY(store.upsertConnectorState(QStringLiteral("connector:schedule:today.ics"), {
        {QStringLiteral("sourceKind"), QStringLiteral("connector_schedule_calendar")},
        {QStringLiteral("connectorKind"), QStringLiteral("schedule")},
        {QStringLiteral("seenCount"), 3},
        {QStringLiteral("presentedCount"), 1},
        {QStringLiteral("historyRecentlySeen"), true},
        {QStringLiteral("historyRecentlyPresented"), false},
        {QStringLiteral("lastSeenAtMs"), 1200}
    }));

    const QList<MemoryRecord> records = store.connectorMemory(QStringLiteral("schedule"));
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.first().source, QStringLiteral("connector_memory"));
    QCOMPARE(records.first().key, QStringLiteral("connector_history_schedule"));
    QVERIFY(records.first().value.contains(QStringLiteral("Schedule signals seen 3 times")));
}

void MemoryStoreTests::persistsCompiledContextPolicyMemory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryStore store(dir.path() + QStringLiteral("/memory.json"));

    QVERIFY(store.upsertCompiledContextPolicyState({
        {QStringLiteral("dominantMode"), QStringLiteral("document_work")},
        {QStringLiteral("selectionDirective"), QStringLiteral("History policy: stable document-focused work is ongoing.")},
        {QStringLiteral("promptDirective"), QStringLiteral("Stable mode: document-focused work remains active.")},
        {QStringLiteral("reasonCode"), QStringLiteral("compiled_history_policy.document_work")},
        {QStringLiteral("strength"), 2.8},
        {QStringLiteral("updatedAtMs"), 4200}
    }));

    const QList<MemoryRecord> records = store.compiledContextPolicyMemory(QStringLiteral("document"));
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.first().source, QStringLiteral("compiled_history_policy_memory"));
    QCOMPARE(records.first().key, QStringLiteral("compiled_context_history_mode"));
    QVERIFY(records.first().value.contains(QStringLiteral("document_work")));
}

void MemoryStoreTests::persistsCompiledContextPolicyHistory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryStore store(dir.path() + QStringLiteral("/memory.json"));

    QVERIFY(store.upsertCompiledContextPolicyState({
        {QStringLiteral("dominantMode"), QStringLiteral("document_work")},
        {QStringLiteral("selectionDirective"), QStringLiteral("History policy: stable document-focused work is ongoing.")},
        {QStringLiteral("promptDirective"), QStringLiteral("Stable mode: document-focused work remains active.")},
        {QStringLiteral("reasonCode"), QStringLiteral("compiled_history_policy.document_work")},
        {QStringLiteral("strength"), 2.4},
        {QStringLiteral("updatedAtMs"), 4200}
    }));
    QVERIFY(store.upsertCompiledContextPolicyState({
        {QStringLiteral("dominantMode"), QStringLiteral("research_analysis")},
        {QStringLiteral("selectionDirective"), QStringLiteral("History policy: stable research analysis is ongoing.")},
        {QStringLiteral("promptDirective"), QStringLiteral("Stable mode: research analysis remains active.")},
        {QStringLiteral("reasonCode"), QStringLiteral("compiled_history_policy.research_analysis")},
        {QStringLiteral("strength"), 2.8},
        {QStringLiteral("updatedAtMs"), 5200}
    }));

    const QVariantList history = store.compiledContextPolicyHistory();
    QCOMPARE(history.size(), 2);
    QCOMPARE(history.first().toMap().value(QStringLiteral("dominantMode")).toString(),
             QStringLiteral("document_work"));
    QCOMPARE(history.last().toMap().value(QStringLiteral("dominantMode")).toString(),
             QStringLiteral("research_analysis"));
}

void MemoryStoreTests::persistsAndRollsBackCompiledContextPolicyTuningState()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryStore store(dir.path() + QStringLiteral("/memory.json"));

    QVERIFY(store.promoteCompiledContextPolicyTuningState({
        {QStringLiteral("tuningCurrentMode"), QStringLiteral("document_work")},
        {QStringLiteral("tuningVolatilityLevel"), QStringLiteral("steady")},
        {QStringLiteral("tuningAlignmentBoost"), 0.08},
        {QStringLiteral("tuningDefocusPenalty"), 0.07},
        {QStringLiteral("tuningVolatilityPenalty"), 0.05},
        {QStringLiteral("tuningSuppressionScoreThreshold"), 0.72},
        {QStringLiteral("updatedAtMs"), 4200}
    }));
    QVERIFY(store.promoteCompiledContextPolicyTuningState({
        {QStringLiteral("tuningCurrentMode"), QStringLiteral("research_analysis")},
        {QStringLiteral("tuningVolatilityLevel"), QStringLiteral("elevated")},
        {QStringLiteral("tuningAlignmentBoost"), 0.10},
        {QStringLiteral("tuningDefocusPenalty"), 0.08},
        {QStringLiteral("tuningVolatilityPenalty"), 0.08},
        {QStringLiteral("tuningSuppressionScoreThreshold"), 0.78},
        {QStringLiteral("updatedAtMs"), 5200}
    }));

    QCOMPARE(store.compiledContextPolicyTuningState().value(QStringLiteral("tuningCurrentMode")).toString(),
             QStringLiteral("research_analysis"));
    QCOMPARE(store.compiledContextPolicyTuningState().value(QStringLiteral("version")).toInt(), 2);
    QCOMPARE(store.compiledContextPolicyTuningHistory().size(), 2);

    QVERIFY(store.rollbackCompiledContextPolicyTuningState({
        {QStringLiteral("tuningPromotionAction"), QStringLiteral("rollback")},
        {QStringLiteral("tuningPromotionReason"), QStringLiteral("behavior_tuning.rollback_test")}
    }));
    QCOMPARE(store.compiledContextPolicyTuningState().value(QStringLiteral("tuningCurrentMode")).toString(),
             QStringLiteral("document_work"));
    QCOMPARE(store.compiledContextPolicyTuningState().value(QStringLiteral("tuningPromotionAction")).toString(),
             QStringLiteral("rollback"));
    QCOMPARE(store.compiledContextPolicyTuningHistory().size(), 1);
}

void MemoryStoreTests::recordsCompiledContextPolicyTuningEpisodes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryStore store(dir.path() + QStringLiteral("/memory.json"));

    QVERIFY(store.promoteCompiledContextPolicyTuningState({
        {QStringLiteral("tuningCurrentMode"), QStringLiteral("document_work")},
        {QStringLiteral("tuningVolatilityLevel"), QStringLiteral("steady")},
        {QStringLiteral("tuningAlignmentBoost"), 0.08},
        {QStringLiteral("tuningDefocusPenalty"), 0.07},
        {QStringLiteral("tuningVolatilityPenalty"), 0.05},
        {QStringLiteral("tuningSuppressionScoreThreshold"), 0.72},
        {QStringLiteral("tuningPromotionAction"), QStringLiteral("promote")},
        {QStringLiteral("tuningPromotionReason"), QStringLiteral("behavior_tuning.promote_test")},
        {QStringLiteral("updatedAtMs"), 4200}
    }));
    QVERIFY(store.promoteCompiledContextPolicyTuningState({
        {QStringLiteral("tuningCurrentMode"), QStringLiteral("research_analysis")},
        {QStringLiteral("tuningVolatilityLevel"), QStringLiteral("elevated")},
        {QStringLiteral("tuningAlignmentBoost"), 0.10},
        {QStringLiteral("tuningDefocusPenalty"), 0.08},
        {QStringLiteral("tuningVolatilityPenalty"), 0.08},
        {QStringLiteral("tuningSuppressionScoreThreshold"), 0.78},
        {QStringLiteral("tuningPromotionAction"), QStringLiteral("promote")},
        {QStringLiteral("tuningPromotionReason"), QStringLiteral("behavior_tuning.promote_shift")},
        {QStringLiteral("updatedAtMs"), 5200}
    }));

    const QVariantList episodes = store.compiledContextPolicyTuningEpisodes();
    QCOMPARE(episodes.size(), 2);
    QCOMPARE(episodes.last().toMap().value(QStringLiteral("mode")).toString(),
             QStringLiteral("research_analysis"));

    const QList<MemoryRecord> records = store.compiledContextPolicyTuningEpisodeMemory();
    QCOMPARE(records.size(), 2);
    QVERIFY(records.last().value.contains(QStringLiteral("behavior_tuning.promote_shift")));
}

void MemoryStoreTests::persistsFeedbackSignalsAndBuildsAggregateMemory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryStore store(dir.path() + QStringLiteral("/memory.json"));

    FeedbackSignal accepted;
    accepted.signalId = QStringLiteral("signal-1");
    accepted.signalType = QStringLiteral("accepted");
    accepted.traceId = QStringLiteral("thread-a");
    accepted.value = QStringLiteral("proactive_coding_followup");
    accepted.metadata = {
        {QStringLiteral("suggestionType"), QStringLiteral("proactive_coding_followup")},
        {QStringLiteral("occurredAtMs"), 4200}
    };

    FeedbackSignal dismissed = accepted;
    dismissed.signalId = QStringLiteral("signal-2");
    dismissed.signalType = QStringLiteral("dismissed");
    dismissed.metadata.insert(QStringLiteral("occurredAtMs"), 5200);

    QVERIFY(store.appendFeedbackSignal(accepted));
    QVERIFY(store.appendFeedbackSignal(dismissed));

    const QVariantList history = store.feedbackSignalHistory();
    QCOMPARE(history.size(), 2);
    QCOMPARE(history.last().toMap().value(QStringLiteral("signalType")).toString(),
             QStringLiteral("dismissed"));

    const QList<MemoryRecord> records = store.feedbackSignalMemory();
    QVERIFY(records.size() >= 2);
    QCOMPARE(records.first().key, QStringLiteral("behavior_tuning_feedback_aggregate"));
    QVERIFY(records.first().value.contains(QStringLiteral("accepted=1")));
    QVERIFY(records.first().value.contains(QStringLiteral("dismissed=1")));
}

void MemoryStoreTests::scoresTuningEpisodesAgainstFeedbackSignals()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryStore store(dir.path() + QStringLiteral("/memory.json"));

    QVERIFY(store.promoteCompiledContextPolicyTuningState({
        {QStringLiteral("tuningCurrentMode"), QStringLiteral("document_work")},
        {QStringLiteral("tuningVolatilityLevel"), QStringLiteral("steady")},
        {QStringLiteral("tuningAlignmentBoost"), 0.08},
        {QStringLiteral("tuningDefocusPenalty"), 0.07},
        {QStringLiteral("tuningVolatilityPenalty"), 0.05},
        {QStringLiteral("tuningSuppressionScoreThreshold"), 0.72},
        {QStringLiteral("tuningPromotionAction"), QStringLiteral("promote")},
        {QStringLiteral("tuningPromotionReason"), QStringLiteral("behavior_tuning.promote_test")},
        {QStringLiteral("updatedAtMs"), 4200}
    }));

    FeedbackSignal accepted;
    accepted.signalId = QStringLiteral("signal-1");
    accepted.signalType = QStringLiteral("accepted");
    accepted.traceId = QStringLiteral("thread-a");
    accepted.value = QStringLiteral("proactive_coding_followup");
    accepted.metadata = {
        {QStringLiteral("suggestionType"), QStringLiteral("proactive_coding_followup")},
        {QStringLiteral("occurredAtMs"), 4300}
    };
    QVERIFY(store.appendFeedbackSignal(accepted));

    const QVariantList scores = store.compiledContextPolicyTuningFeedbackScores();
    QCOMPARE(scores.size(), 1);
    QCOMPARE(scores.first().toMap().value(QStringLiteral("outcome")).toString(),
             QStringLiteral("supported"));

    const QList<MemoryRecord> records = store.compiledContextPolicyTuningFeedbackScoreMemory();
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.first().source, QStringLiteral("compiled_history_policy_tuning_feedback_score"));
    QVERIFY(records.first().value.contains(QStringLiteral("accepted=1")));
}

QTEST_APPLESS_MAIN(MemoryStoreTests)
#include "MemoryStoreTests.moc"

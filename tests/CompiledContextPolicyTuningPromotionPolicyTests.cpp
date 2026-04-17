#include <QtTest>

#include "behavior_tuning/CompiledContextPolicyTuningPromotionPolicy.h"

class CompiledContextPolicyTuningPromotionPolicyTests : public QObject
{
    Q_OBJECT

private slots:
    void promotesBootstrapCandidateWhenStable();
    void holdsCandidateWithInsufficientObservations();
    void boundsPromotionAgainstPersistedState();
    void rollsBackVolatileCandidateWhenHistoryExists();
    void rollsBackPromotedVersionRejectedByFeedback();
    void holdsRejectedFeedbackWithoutRollbackTarget();
};

void CompiledContextPolicyTuningPromotionPolicyTests::promotesBootstrapCandidateWhenStable()
{
    const auto decision = CompiledContextPolicyTuningPromotionPolicy::evaluate(
        {
            {QStringLiteral("tuningCurrentMode"), QStringLiteral("document_work")},
            {QStringLiteral("tuningVolatilityLevel"), QStringLiteral("steady")},
            {QStringLiteral("tuningAlignmentBoost"), 0.09},
            {QStringLiteral("tuningDefocusPenalty"), 0.08},
            {QStringLiteral("tuningVolatilityPenalty"), 0.05},
            {QStringLiteral("tuningSuppressionScoreThreshold"), 0.73},
            {QStringLiteral("tuningObservedCount"), 2},
            {QStringLiteral("tuningShiftCount"), 1},
            {QStringLiteral("tuningTotalObservations"), 4},
            {QStringLiteral("updatedAtMs"), 5000}
        },
        {},
        {},
        5000);

    QCOMPARE(decision.action, CompiledContextPolicyTuningPromotionDecision::Action::Promote);
    QCOMPARE(decision.reasonCode, QStringLiteral("behavior_tuning.promote_bootstrap"));
    QCOMPARE(decision.nextState.value(QStringLiteral("tuningPolicySource")).toString(),
             QStringLiteral("bounded_promotion_policy"));
}

void CompiledContextPolicyTuningPromotionPolicyTests::holdsCandidateWithInsufficientObservations()
{
    const auto decision = CompiledContextPolicyTuningPromotionPolicy::evaluate(
        {
            {QStringLiteral("tuningCurrentMode"), QStringLiteral("research_analysis")},
            {QStringLiteral("tuningVolatilityLevel"), QStringLiteral("elevated")},
            {QStringLiteral("tuningAlignmentBoost"), 0.10},
            {QStringLiteral("tuningDefocusPenalty"), 0.08},
            {QStringLiteral("tuningVolatilityPenalty"), 0.08},
            {QStringLiteral("tuningSuppressionScoreThreshold"), 0.78},
            {QStringLiteral("tuningObservedCount"), 2},
            {QStringLiteral("tuningShiftCount"), 2},
            {QStringLiteral("tuningTotalObservations"), 5},
            {QStringLiteral("updatedAtMs"), 6200}
        },
        {},
        {},
        6200);

    QCOMPARE(decision.action, CompiledContextPolicyTuningPromotionDecision::Action::Hold);
    QCOMPARE(decision.reasonCode, QStringLiteral("behavior_tuning.hold_bootstrap_observations"));
    QVERIFY(decision.nextState.isEmpty());
}

void CompiledContextPolicyTuningPromotionPolicyTests::boundsPromotionAgainstPersistedState()
{
    const auto decision = CompiledContextPolicyTuningPromotionPolicy::evaluate(
        {
            {QStringLiteral("tuningCurrentMode"), QStringLiteral("research_analysis")},
            {QStringLiteral("tuningVolatilityLevel"), QStringLiteral("steady")},
            {QStringLiteral("tuningAlignmentBoost"), 0.15},
            {QStringLiteral("tuningDefocusPenalty"), 0.14},
            {QStringLiteral("tuningVolatilityPenalty"), 0.10},
            {QStringLiteral("tuningSuppressionScoreThreshold"), 0.84},
            {QStringLiteral("tuningObservedCount"), 3},
            {QStringLiteral("tuningShiftCount"), 3},
            {QStringLiteral("tuningTotalObservations"), 9},
            {QStringLiteral("updatedAtMs"), 400000}
        },
        {
            {QStringLiteral("tuningCurrentMode"), QStringLiteral("document_work")},
            {QStringLiteral("tuningVolatilityLevel"), QStringLiteral("steady")},
            {QStringLiteral("tuningAlignmentBoost"), 0.08},
            {QStringLiteral("tuningDefocusPenalty"), 0.07},
            {QStringLiteral("tuningVolatilityPenalty"), 0.05},
            {QStringLiteral("tuningSuppressionScoreThreshold"), 0.72},
            {QStringLiteral("updatedAtMs"), 1000},
            {QStringLiteral("version"), 4}
        },
        {
            QVariantMap{{QStringLiteral("version"), 3}},
            QVariantMap{{QStringLiteral("version"), 4}}
        },
        400000);

    QCOMPARE(decision.action, CompiledContextPolicyTuningPromotionDecision::Action::Promote);
    QCOMPARE(decision.reasonCode, QStringLiteral("behavior_tuning.promote_mode_shift"));
    QCOMPARE(decision.nextState.value(QStringLiteral("tuningAlignmentBoost")).toDouble(), 0.10);
    QCOMPARE(decision.nextState.value(QStringLiteral("tuningDefocusPenalty")).toDouble(), 0.09);
    QCOMPARE(decision.nextState.value(QStringLiteral("tuningVolatilityPenalty")).toDouble(), 0.07);
    QCOMPARE(decision.nextState.value(QStringLiteral("tuningSuppressionScoreThreshold")).toDouble(), 0.75);
}

void CompiledContextPolicyTuningPromotionPolicyTests::rollsBackVolatileCandidateWhenHistoryExists()
{
    const auto decision = CompiledContextPolicyTuningPromotionPolicy::evaluate(
        {
            {QStringLiteral("tuningCurrentMode"), QStringLiteral("research_analysis")},
            {QStringLiteral("tuningVolatilityLevel"), QStringLiteral("elevated")},
            {QStringLiteral("tuningAlignmentBoost"), 0.10},
            {QStringLiteral("tuningDefocusPenalty"), 0.08},
            {QStringLiteral("tuningVolatilityPenalty"), 0.08},
            {QStringLiteral("tuningSuppressionScoreThreshold"), 0.78},
            {QStringLiteral("tuningObservedCount"), 1},
            {QStringLiteral("tuningShiftCount"), 5},
            {QStringLiteral("tuningTotalObservations"), 7},
            {QStringLiteral("updatedAtMs"), 9000}
        },
        {
            {QStringLiteral("tuningCurrentMode"), QStringLiteral("document_work")},
            {QStringLiteral("tuningVolatilityLevel"), QStringLiteral("steady")},
            {QStringLiteral("tuningAlignmentBoost"), 0.08},
            {QStringLiteral("tuningDefocusPenalty"), 0.07},
            {QStringLiteral("tuningVolatilityPenalty"), 0.05},
            {QStringLiteral("tuningSuppressionScoreThreshold"), 0.72},
            {QStringLiteral("updatedAtMs"), 5000}
        },
        {
            QVariantMap{{QStringLiteral("version"), 1}},
            QVariantMap{{QStringLiteral("version"), 2}}
        },
        9000);

    QCOMPARE(decision.action, CompiledContextPolicyTuningPromotionDecision::Action::Rollback);
    QCOMPARE(decision.reasonCode, QStringLiteral("behavior_tuning.rollback_volatile_candidate"));
}

void CompiledContextPolicyTuningPromotionPolicyTests::rollsBackPromotedVersionRejectedByFeedback()
{
    const auto decision = CompiledContextPolicyTuningPromotionPolicy::evaluate(
        {
            {QStringLiteral("tuningCurrentMode"), QStringLiteral("research_analysis")},
            {QStringLiteral("tuningVolatilityLevel"), QStringLiteral("steady")},
            {QStringLiteral("tuningAlignmentBoost"), 0.10},
            {QStringLiteral("tuningDefocusPenalty"), 0.08},
            {QStringLiteral("tuningVolatilityPenalty"), 0.07},
            {QStringLiteral("tuningSuppressionScoreThreshold"), 0.76},
            {QStringLiteral("tuningObservedCount"), 4},
            {QStringLiteral("tuningShiftCount"), 1},
            {QStringLiteral("tuningTotalObservations"), 10},
            {QStringLiteral("updatedAtMs"), 500000}
        },
        {
            {QStringLiteral("tuningCurrentMode"), QStringLiteral("research_analysis")},
            {QStringLiteral("tuningVolatilityLevel"), QStringLiteral("steady")},
            {QStringLiteral("tuningAlignmentBoost"), 0.10},
            {QStringLiteral("tuningDefocusPenalty"), 0.09},
            {QStringLiteral("tuningVolatilityPenalty"), 0.07},
            {QStringLiteral("tuningSuppressionScoreThreshold"), 0.75},
            {QStringLiteral("updatedAtMs"), 400000},
            {QStringLiteral("version"), 4}
        },
        {
            QVariantMap{{QStringLiteral("version"), 3}},
            QVariantMap{{QStringLiteral("version"), 4}}
        },
        500000,
        {
            QVariantMap{
                {QStringLiteral("action"), QStringLiteral("promote")},
                {QStringLiteral("toVersion"), 4},
                {QStringLiteral("totalFeedbackCount"), 2},
                {QStringLiteral("supportScore"), -2.0},
                {QStringLiteral("outcome"), QStringLiteral("rejected")}
            }
        });

    QCOMPARE(decision.action, CompiledContextPolicyTuningPromotionDecision::Action::Rollback);
    QCOMPARE(decision.reasonCode, QStringLiteral("behavior_tuning.rollback_feedback_rejected"));
}

void CompiledContextPolicyTuningPromotionPolicyTests::holdsRejectedFeedbackWithoutRollbackTarget()
{
    const auto decision = CompiledContextPolicyTuningPromotionPolicy::evaluate(
        {
            {QStringLiteral("tuningCurrentMode"), QStringLiteral("document_work")},
            {QStringLiteral("tuningVolatilityLevel"), QStringLiteral("steady")},
            {QStringLiteral("tuningAlignmentBoost"), 0.10},
            {QStringLiteral("tuningDefocusPenalty"), 0.08},
            {QStringLiteral("tuningVolatilityPenalty"), 0.07},
            {QStringLiteral("tuningSuppressionScoreThreshold"), 0.76},
            {QStringLiteral("tuningObservedCount"), 4},
            {QStringLiteral("tuningShiftCount"), 1},
            {QStringLiteral("tuningTotalObservations"), 10},
            {QStringLiteral("updatedAtMs"), 500000}
        },
        {
            {QStringLiteral("tuningCurrentMode"), QStringLiteral("document_work")},
            {QStringLiteral("tuningVolatilityLevel"), QStringLiteral("steady")},
            {QStringLiteral("tuningAlignmentBoost"), 0.10},
            {QStringLiteral("tuningDefocusPenalty"), 0.08},
            {QStringLiteral("tuningVolatilityPenalty"), 0.07},
            {QStringLiteral("tuningSuppressionScoreThreshold"), 0.76},
            {QStringLiteral("updatedAtMs"), 400000},
            {QStringLiteral("version"), 1}
        },
        {
            QVariantMap{{QStringLiteral("version"), 1}}
        },
        500000,
        {
            QVariantMap{
                {QStringLiteral("action"), QStringLiteral("promote")},
                {QStringLiteral("toVersion"), 1},
                {QStringLiteral("totalFeedbackCount"), 2},
                {QStringLiteral("supportScore"), -1.5},
                {QStringLiteral("outcome"), QStringLiteral("rejected")}
            }
        });

    QCOMPARE(decision.action, CompiledContextPolicyTuningPromotionDecision::Action::Hold);
    QCOMPARE(decision.reasonCode,
             QStringLiteral("behavior_tuning.hold_feedback_rejected_no_rollback_target"));
}

QTEST_APPLESS_MAIN(CompiledContextPolicyTuningPromotionPolicyTests)
#include "CompiledContextPolicyTuningPromotionPolicyTests.moc"

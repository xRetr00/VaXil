#include "behavior_tuning/CompiledContextPolicyTuningPromotionPolicy.h"

#include <QtGlobal>

namespace {
constexpr double kAlignmentMin = 0.04;
constexpr double kAlignmentMax = 0.16;
constexpr double kPenaltyMin = 0.03;
constexpr double kPenaltyMax = 0.16;
constexpr double kVolatilityPenaltyMin = 0.02;
constexpr double kVolatilityPenaltyMax = 0.14;
constexpr double kSuppressionThresholdMin = 0.64;
constexpr double kSuppressionThresholdMax = 0.88;
constexpr double kAlignmentStepMax = 0.02;
constexpr double kPenaltyStepMax = 0.02;
constexpr double kThresholdStepMax = 0.03;
constexpr qint64 kPromotionCooldownMs = 120000;
constexpr int kFeedbackRollbackMinSignals = 2;

double clampValue(double value, double minimum, double maximum)
{
    return qBound(minimum, value, maximum);
}

double boundedStep(double current, double target, double maxStep)
{
    const double delta = target - current;
    if (qAbs(delta) <= maxStep) {
        return target;
    }
    return current + (delta > 0.0 ? maxStep : -maxStep);
}

QVariantMap normalizedState(const QVariantMap &state, qint64 fallbackNowMs)
{
    if (state.isEmpty()) {
        return {};
    }

    QVariantMap normalized = state;
    normalized.insert(QStringLiteral("updatedAtMs"),
                      normalized.value(QStringLiteral("updatedAtMs"), fallbackNowMs).toLongLong());
    normalized.insert(QStringLiteral("tuningAlignmentBoost"),
                      clampValue(normalized.value(QStringLiteral("tuningAlignmentBoost"), 0.06).toDouble(),
                                 kAlignmentMin,
                                 kAlignmentMax));
    normalized.insert(QStringLiteral("tuningDefocusPenalty"),
                      clampValue(normalized.value(QStringLiteral("tuningDefocusPenalty"), 0.05).toDouble(),
                                 kPenaltyMin,
                                 kPenaltyMax));
    normalized.insert(QStringLiteral("tuningVolatilityPenalty"),
                      clampValue(normalized.value(QStringLiteral("tuningVolatilityPenalty"), 0.04).toDouble(),
                                 kVolatilityPenaltyMin,
                                 kVolatilityPenaltyMax));
    normalized.insert(QStringLiteral("tuningSuppressionScoreThreshold"),
                      clampValue(normalized.value(QStringLiteral("tuningSuppressionScoreThreshold"), 0.72).toDouble(),
                                 kSuppressionThresholdMin,
                                 kSuppressionThresholdMax));
    return normalized;
}

bool materiallyChanged(const QVariantMap &lhs, const QVariantMap &rhs)
{
    const QStringList keys = {
        QStringLiteral("tuningCurrentMode"),
        QStringLiteral("tuningVolatilityLevel"),
        QStringLiteral("tuningAlignmentBoost"),
        QStringLiteral("tuningDefocusPenalty"),
        QStringLiteral("tuningVolatilityPenalty"),
        QStringLiteral("tuningSuppressionScoreThreshold")
    };
    for (const QString &key : keys) {
        if (lhs.value(key) != rhs.value(key)) {
            return true;
        }
    }
    return false;
}

QVariantMap boundedPromotionState(const QVariantMap &persistedState,
                                  const QVariantMap &candidateState,
                                  qint64 nowMs)
{
    QVariantMap next = persistedState;
    next.insert(QStringLiteral("updatedAtMs"), nowMs);
    next.insert(QStringLiteral("tuningCurrentMode"),
                candidateState.value(QStringLiteral("tuningCurrentMode")).toString().trimmed());
    next.insert(QStringLiteral("tuningVolatilityLevel"),
                candidateState.value(QStringLiteral("tuningVolatilityLevel")).toString().trimmed());
    next.insert(QStringLiteral("tuningObservedCount"),
                candidateState.value(QStringLiteral("tuningObservedCount")).toInt());
    next.insert(QStringLiteral("tuningShiftCount"),
                candidateState.value(QStringLiteral("tuningShiftCount")).toInt());
    next.insert(QStringLiteral("tuningTotalObservations"),
                candidateState.value(QStringLiteral("tuningTotalObservations")).toInt());
    next.insert(QStringLiteral("tuningAlignmentBoost"),
                boundedStep(persistedState.value(QStringLiteral("tuningAlignmentBoost"), 0.06).toDouble(),
                            candidateState.value(QStringLiteral("tuningAlignmentBoost"), 0.06).toDouble(),
                            kAlignmentStepMax));
    next.insert(QStringLiteral("tuningDefocusPenalty"),
                boundedStep(persistedState.value(QStringLiteral("tuningDefocusPenalty"), 0.05).toDouble(),
                            candidateState.value(QStringLiteral("tuningDefocusPenalty"), 0.05).toDouble(),
                            kPenaltyStepMax));
    next.insert(QStringLiteral("tuningVolatilityPenalty"),
                boundedStep(persistedState.value(QStringLiteral("tuningVolatilityPenalty"), 0.04).toDouble(),
                            candidateState.value(QStringLiteral("tuningVolatilityPenalty"), 0.04).toDouble(),
                            kPenaltyStepMax));
    next.insert(QStringLiteral("tuningSuppressionScoreThreshold"),
                boundedStep(persistedState.value(QStringLiteral("tuningSuppressionScoreThreshold"), 0.72).toDouble(),
                            candidateState.value(QStringLiteral("tuningSuppressionScoreThreshold"), 0.72).toDouble(),
                            kThresholdStepMax));
    return normalizedState(next, nowMs);
}

QVariantMap latestPromotedScoreForPersistedVersion(const QVariantMap &persistedState,
                                                   const QVariantList &feedbackScores)
{
    const int version = persistedState.value(QStringLiteral("version"), 0).toInt();
    if (version <= 0) {
        return {};
    }

    for (int i = feedbackScores.size() - 1; i >= 0; --i) {
        const QVariantMap score = feedbackScores.at(i).toMap();
        if (score.value(QStringLiteral("action")).toString().trimmed() != QStringLiteral("promote")) {
            continue;
        }
        if (score.value(QStringLiteral("toVersion")).toInt() == version) {
            return score;
        }
    }
    return {};
}

bool isRejectedFeedbackScore(const QVariantMap &score)
{
    return score.value(QStringLiteral("outcome")).toString().trimmed() == QStringLiteral("rejected")
        && score.value(QStringLiteral("totalFeedbackCount")).toInt() >= kFeedbackRollbackMinSignals
        && score.value(QStringLiteral("supportScore")).toDouble() <= -1.0;
}
}

CompiledContextPolicyTuningPromotionDecision CompiledContextPolicyTuningPromotionPolicy::evaluate(
    const QVariantMap &candidateState,
    const QVariantMap &persistedState,
    const QVariantList &persistedHistory,
    qint64 nowMs,
    const QVariantList &feedbackScores)
{
    CompiledContextPolicyTuningPromotionDecision decision;
    const QVariantMap candidate = normalizedState(candidateState, nowMs);
    if (candidate.value(QStringLiteral("tuningCurrentMode")).toString().trimmed().isEmpty()) {
        decision.reasonCode = QStringLiteral("behavior_tuning.hold_invalid_candidate");
        return decision;
    }

    const QVariantMap persisted = normalizedState(persistedState, nowMs);
    const QVariantMap latestFeedbackScore =
        latestPromotedScoreForPersistedVersion(persisted, feedbackScores);
    if (isRejectedFeedbackScore(latestFeedbackScore)) {
        if (persistedHistory.size() >= 2) {
            decision.action = CompiledContextPolicyTuningPromotionDecision::Action::Rollback;
            decision.reasonCode = QStringLiteral("behavior_tuning.rollback_feedback_rejected");
            return decision;
        }
        decision.reasonCode = QStringLiteral("behavior_tuning.hold_feedback_rejected_no_rollback_target");
        return decision;
    }

    const int observations = candidate.value(QStringLiteral("tuningObservedCount")).toInt();
    const int shifts = candidate.value(QStringLiteral("tuningShiftCount")).toInt();
    const QString volatility = candidate.value(QStringLiteral("tuningVolatilityLevel")).toString().trimmed();
    const int minimumObservations = volatility == QStringLiteral("elevated") ? 3 : 2;

    if (persisted.isEmpty()) {
        if (observations < minimumObservations) {
            decision.reasonCode = QStringLiteral("behavior_tuning.hold_bootstrap_observations");
            return decision;
        }

        QVariantMap next = candidate;
        next.insert(QStringLiteral("tuningPromotionAction"), QStringLiteral("promote"));
        next.insert(QStringLiteral("tuningPromotionReason"), QStringLiteral("behavior_tuning.promote_bootstrap"));
        next.insert(QStringLiteral("tuningPromotedFromVersion"), 0);
        next.insert(QStringLiteral("tuningPolicySource"), QStringLiteral("bounded_promotion_policy"));
        decision.action = CompiledContextPolicyTuningPromotionDecision::Action::Promote;
        decision.reasonCode = QStringLiteral("behavior_tuning.promote_bootstrap");
        decision.nextState = next;
        return decision;
    }

    const qint64 lastUpdatedAtMs = persisted.value(QStringLiteral("updatedAtMs"), 0).toLongLong();
    const bool promotionCoolingDown = lastUpdatedAtMs > 0 && (nowMs - lastUpdatedAtMs) < kPromotionCooldownMs;
    if (observations < minimumObservations) {
        if (persistedHistory.size() >= 2 && shifts >= 4) {
            decision.action = CompiledContextPolicyTuningPromotionDecision::Action::Rollback;
            decision.reasonCode = QStringLiteral("behavior_tuning.rollback_volatile_candidate");
            return decision;
        }
        decision.reasonCode = QStringLiteral("behavior_tuning.hold_insufficient_observations");
        return decision;
    }

    QVariantMap next = boundedPromotionState(persisted, candidate, nowMs);
    if (!materiallyChanged(persisted, next)) {
        decision.reasonCode = QStringLiteral("behavior_tuning.hold_no_material_change");
        return decision;
    }
    if (promotionCoolingDown && shifts < 3) {
        decision.reasonCode = QStringLiteral("behavior_tuning.hold_promotion_cooldown");
        return decision;
    }

    const int previousVersion = persisted.value(QStringLiteral("version"), 0).toInt();
    const bool modeChanged =
        persisted.value(QStringLiteral("tuningCurrentMode")).toString().trimmed()
        != next.value(QStringLiteral("tuningCurrentMode")).toString().trimmed();
    next.insert(QStringLiteral("tuningPromotionAction"), QStringLiteral("promote"));
    next.insert(QStringLiteral("tuningPromotionReason"),
                modeChanged
                    ? QStringLiteral("behavior_tuning.promote_mode_shift")
                    : QStringLiteral("behavior_tuning.promote_bounded_adjustment"));
    next.insert(QStringLiteral("tuningPromotedFromVersion"), previousVersion);
    next.insert(QStringLiteral("tuningPolicySource"), QStringLiteral("bounded_promotion_policy"));

    decision.action = CompiledContextPolicyTuningPromotionDecision::Action::Promote;
    decision.reasonCode = next.value(QStringLiteral("tuningPromotionReason")).toString();
    decision.nextState = next;
    return decision;
}

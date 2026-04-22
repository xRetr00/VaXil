#include "core/intent/RouteArbitrator.h"
#include "core/intent/IntentTuningConfig.h"

#include <algorithm>
#include <optional>

namespace {
QString routeToString(const InputRouteDecision &decision)
{
    switch (decision.kind) {
    case InputRouteKind::LocalResponse:
        return QStringLiteral("LocalResponse");
    case InputRouteKind::DeterministicTasks:
        return QStringLiteral("DeterministicTasks");
    case InputRouteKind::BackgroundTasks:
        return QStringLiteral("BackgroundTasks");
    case InputRouteKind::Conversation:
        return QStringLiteral("Conversation");
    case InputRouteKind::AgentConversation:
        return QStringLiteral("AgentConversation");
    case InputRouteKind::CommandExtraction:
        return QStringLiteral("CommandExtraction");
    case InputRouteKind::AgentCapabilityError:
        return QStringLiteral("AgentCapabilityError");
    case InputRouteKind::None:
    default:
        return QStringLiteral("None");
    }
}
}

RouteArbitrationResult RouteArbitrator::arbitrate(const InputRouteDecision &policyDecision,
                                                  const TurnSignals &turnSignals,
                                                  const TurnState &state,
                                                  const TurnGoalSet &goals,
                                                  const QList<ExecutionIntentCandidate> &candidates,
                                                  const IntentConfidence &confidence,
                                                  float ambiguityScore,
                                                  const IntentAdvisorSuggestion &advisorSuggestion,
                                                  bool hasDeterministicTask) const
{
    const IntentTuningThresholds &thresholds = IntentTuningConfig::thresholds();

    RouteArbitrationResult result;
    result.decision = policyDecision;
    result.finalRoute = routeToString(policyDecision);
    result.confidence = 0.65f;
    result.reasonCodes.push_back(QStringLiteral("arbitrator.shadow_policy_default"));

    result.scores.socialScore = turnSignals.socialOnly ? 0.9f : 0.1f;
    result.scores.commandScore = turnSignals.hasCommandCue ? 0.75f : 0.05f;
    result.scores.infoQueryScore = turnSignals.hasQuestionCue ? 0.8f : 0.1f;
    result.scores.deterministicScore = hasDeterministicTask ? 0.95f : 0.1f;
    result.scores.continuationScore = state.isContinuation ? 0.8f : 0.05f;
    result.scores.contextReferenceScore = turnSignals.hasContextReference ? 0.7f : 0.05f;
    result.scores.backendNeedScore =
        goals.primaryGoal.kind == UserGoalKind::InfoQuery ? 0.8f : 0.2f;

    auto findCandidate = [&candidates](InputRouteKind kind) -> std::optional<ExecutionIntentCandidate> {
        for (const ExecutionIntentCandidate &candidate : candidates) {
            if (candidate.route.kind == kind) {
                return candidate;
            }
        }
        return std::nullopt;
    };

    const float effectiveAmbiguity = std::clamp(ambiguityScore + advisorSuggestion.ambiguityBoost, 0.0f, 1.0f);
    const float effectiveConfidence = std::clamp(confidence.finalConfidence, 0.0f, 1.0f);
    const bool strongSignal = turnSignals.hasDeterministicCue
        || turnSignals.socialOnly
        || turnSignals.hasContextReference
        || turnSignals.hasContinuationCue;
    const bool shouldBackendAssist = (effectiveAmbiguity >= thresholds.highAmbiguity)
        || (effectiveConfidence < thresholds.lowConfidence)
        || (effectiveConfidence < thresholds.mediumConfidence && advisorSuggestion.backendNecessity >= thresholds.backendAssistNeed);

    if (hasDeterministicTask) {
        if (const std::optional<ExecutionIntentCandidate> deterministic = findCandidate(InputRouteKind::DeterministicTasks)) {
            result.decision = deterministic->route;
            result.confidence = deterministic->score;
            result.reasonCodes = deterministic->reasonCodes;
        } else {
            result.decision.kind = InputRouteKind::DeterministicTasks;
            result.confidence = 0.95f;
            result.reasonCodes = {QStringLiteral("arbitrator.deterministic_priority")};
        }
    } else if (effectiveAmbiguity >= thresholds.highAmbiguity && effectiveConfidence <= thresholds.lowConfidence) {
        if (const std::optional<ExecutionIntentCandidate> clarify = findCandidate(InputRouteKind::LocalResponse)) {
            result.decision = clarify->route;
            result.confidence = clarify->score;
            if (!clarify->reasonCodes.isEmpty()) {
                result.reasonCodes = clarify->reasonCodes;
            }
        } else {
            result.decision.kind = InputRouteKind::LocalResponse;
            result.confidence = 0.82f;
            result.decision.message = QStringLiteral("Can you clarify what you want me to do?");
            result.decision.status = QStringLiteral("Clarification needed");
        }
        result.reasonCodes.push_back(QStringLiteral("arbitrator.ask_clarification"));
        result.reasonCodes.push_back(QStringLiteral("arbitrator.low_confidence_high_ambiguity"));
    } else if (effectiveAmbiguity >= 0.85f) {
        if (const std::optional<ExecutionIntentCandidate> clarify = findCandidate(InputRouteKind::LocalResponse)) {
            result.decision = clarify->route;
            result.confidence = clarify->score;
            result.reasonCodes = clarify->reasonCodes;
        } else {
            result.decision.kind = InputRouteKind::LocalResponse;
            result.confidence = 0.78f;
            result.decision.message = QStringLiteral("Can you clarify what you want me to do?");
            result.decision.status = QStringLiteral("Clarification needed");
            result.reasonCodes = {QStringLiteral("arbitrator.ask_clarification")};
        }
        result.reasonCodes.push_back(QStringLiteral("arbitrator.very_high_ambiguity_clarify"));
    } else if (state.isContinuation
               && advisorSuggestion.continuationLikelihood >= 0.55f
               && effectiveAmbiguity < 0.8f) {
        if (const std::optional<ExecutionIntentCandidate> continuation = findCandidate(InputRouteKind::AgentConversation)) {
            result.decision = continuation->route;
            result.confidence = continuation->score;
            result.reasonCodes = continuation->reasonCodes;
        } else {
            result.decision.kind = InputRouteKind::Conversation;
            result.confidence = 0.78f;
            result.reasonCodes = {QStringLiteral("arbitrator.continuation")};
        }
    } else if (shouldBackendAssist) {
        if (const std::optional<ExecutionIntentCandidate> backend = findCandidate(InputRouteKind::Conversation)) {
            result.decision = backend->route;
            result.confidence = std::max(backend->score, 0.68f);
            result.reasonCodes = {QStringLiteral("arbitrator.backend_escalation")};
        } else {
            result.decision.kind = InputRouteKind::Conversation;
            result.confidence = 0.7f;
            result.reasonCodes = {QStringLiteral("arbitrator.backend_escalation_fallback")};
        }
    } else if (turnSignals.socialOnly) {
        result.decision.kind = InputRouteKind::LocalResponse;
        result.confidence = 0.9f;
        result.reasonCodes = {QStringLiteral("arbitrator.social_only_local_response")};
    } else if (turnSignals.hasCommandCue && turnSignals.hasQuestionCue) {
        result.decision.kind = InputRouteKind::Conversation;
        result.confidence = 0.85f;
        result.reasonCodes = {QStringLiteral("arbitrator.command_vs_info_prefers_info_query")};
    } else if (!candidates.isEmpty()) {
        ExecutionIntentCandidate selected = candidates.first();
        for (const ExecutionIntentCandidate &candidate : candidates) {
            if (effectiveConfidence >= thresholds.highConfidence
                && strongSignal
                && candidate.canRunLocal
                && !candidate.requiresBackend) {
                selected = candidate;
                break;
            }
            if (effectiveConfidence < thresholds.mediumConfidence
                && candidate.requiresBackend
                && candidate.backendPriority >= selected.backendPriority) {
                selected = candidate;
            }
        }
        result.decision = selected.route;
        result.confidence = std::max(0.0f, selected.score - selected.confidencePenalty);
        result.reasonCodes = selected.reasonCodes;
    }

    result.finalRoute = routeToString(result.decision);
    if (result.reasonCodes.isEmpty()) {
        result.reasonCodes.push_back(QStringLiteral("arbitrator.default_selection"));
    }
    if (result.decision.kind != policyDecision.kind) {
        result.overridesApplied.push_back(
            QStringLiteral("arbitrator_override:%1->%2")
                .arg(routeToString(policyDecision), routeToString(result.decision)));
    }

    return result;
}

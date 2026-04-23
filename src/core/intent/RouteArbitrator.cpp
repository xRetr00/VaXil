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
    auto findCandidateByReason = [&candidates](const QString &reasonCode) -> std::optional<ExecutionIntentCandidate> {
        for (const ExecutionIntentCandidate &candidate : candidates) {
            if (candidate.reasonCodes.contains(reasonCode)) {
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
    const bool highAmbiguity = effectiveAmbiguity >= thresholds.highAmbiguity;
    const bool belowMediumConfidence = effectiveConfidence < thresholds.mediumConfidence;

    std::optional<ExecutionIntentCandidate> bestBackendCandidate;
    for (const ExecutionIntentCandidate &candidate : candidates) {
        if (!candidate.requiresBackend) {
            continue;
        }
        if (!bestBackendCandidate.has_value()
            || candidate.backendPriority > bestBackendCandidate->backendPriority
            || (candidate.backendPriority == bestBackendCandidate->backendPriority
                && candidate.score > bestBackendCandidate->score)) {
            bestBackendCandidate = candidate;
        }
    }
    const bool backendClearlyNeededByAdvisor =
        bestBackendCandidate.has_value()
        && advisorSuggestion.backendNecessity >= (thresholds.backendAssistNeed + 0.12f);
    const bool backendCandidatePenalizedForMissingContinuationContext =
        bestBackendCandidate.has_value()
        && (bestBackendCandidate->reasonCodes.contains(QStringLiteral("candidate.continuation_missing_context_penalty"))
            || bestBackendCandidate->reasonCodes.contains(QStringLiteral("replay.continuation_missing_context_penalty")));
    const bool backendClearlyNeededByCandidate =
        bestBackendCandidate.has_value()
        && !backendCandidatePenalizedForMissingContinuationContext
        && (bestBackendCandidate->backendPriority >= 85)
        && (bestBackendCandidate->score >= 0.7f);
    const bool backendClearlyNeeded = backendClearlyNeededByAdvisor || backendClearlyNeededByCandidate;
    const bool shouldAskClarification = highAmbiguity && belowMediumConfidence && !backendClearlyNeeded;
    const bool terminalIntent = findCandidateByReason(QStringLiteral("candidate.end_conversation")).has_value()
        || turnSignals.matchedCues.contains(QStringLiteral("stop"))
        || turnSignals.normalizedInput.contains(QStringLiteral("never mind"))
        || turnSignals.normalizedInput.contains(QStringLiteral("nevermind"))
        || turnSignals.normalizedInput.contains(QStringLiteral("quit"))
        || turnSignals.normalizedInput.contains(QStringLiteral("you can go"))
        || turnSignals.normalizedInput.contains(QStringLiteral("that's all"))
        || turnSignals.normalizedInput.contains(QStringLiteral("that is all"))
        || turnSignals.normalizedInput.contains(QStringLiteral("we're done"))
        || turnSignals.normalizedInput.contains(QStringLiteral("we are done"))
        || turnSignals.normalizedInput.contains(QStringLiteral("that's enough"))
        || turnSignals.normalizedInput.contains(QStringLiteral("that is enough"));
    const bool socialQuestionOnly =
        !terminalIntent
        && (turnSignals.hasGreeting || turnSignals.hasSmallTalk)
        && turnSignals.hasQuestionCue
        && !turnSignals.hasCommandCue
        && !turnSignals.hasDeterministicCue
        && !turnSignals.hasContextReference
        && !turnSignals.hasContinuationCue
        && (turnSignals.matchedCues.contains(QStringLiteral("how are you"))
            || turnSignals.matchedCues.contains(QStringLiteral("what's up"))
            || turnSignals.matchedCues.contains(QStringLiteral("whats up"))
            || turnSignals.matchedCues.contains(QStringLiteral("how's it going")));
    const bool shouldBackendAssist = (backendClearlyNeeded && (belowMediumConfidence || highAmbiguity))
        || (effectiveConfidence < thresholds.lowConfidence && !highAmbiguity)
        || (effectiveConfidence < thresholds.mediumConfidence
            && advisorSuggestion.backendNecessity >= thresholds.backendAssistNeed
            && !highAmbiguity);
    const bool localTimeOrDate = findCandidateByReason(QStringLiteral("candidate.time_or_date")).has_value()
        || ((policyDecision.kind == InputRouteKind::LocalResponse)
            && (policyDecision.status.compare(QStringLiteral("Local time response"), Qt::CaseInsensitive) == 0
                || policyDecision.status.compare(QStringLiteral("Local date response"), Qt::CaseInsensitive) == 0));

    if (hasDeterministicTask) {
        if (const std::optional<ExecutionIntentCandidate> deterministic = findCandidate(InputRouteKind::DeterministicTasks)) {
            result.decision = deterministic->route;
            if (result.decision.tasks.isEmpty() && !deterministic->tasks.isEmpty()) {
                result.decision.tasks = deterministic->tasks;
            }
            result.confidence = deterministic->score;
            result.reasonCodes = deterministic->reasonCodes;
        } else {
            result.decision.kind = InputRouteKind::DeterministicTasks;
            result.confidence = 0.95f;
            result.reasonCodes = {QStringLiteral("arbitrator.deterministic_priority")};
        }
    } else if (terminalIntent) {
        if (const std::optional<ExecutionIntentCandidate> terminal = findCandidateByReason(QStringLiteral("candidate.end_conversation"))) {
            result.decision = terminal->route;
            result.confidence = terminal->score;
            result.reasonCodes = terminal->reasonCodes;
        } else {
            result.decision.kind = InputRouteKind::LocalResponse;
            result.decision.status = QStringLiteral("Conversation ended");
            result.confidence = 0.96f;
            result.reasonCodes = {QStringLiteral("arbitrator.terminal_local_response")};
        }
        result.reasonCodes.push_back(QStringLiteral("override.blocked.backend_for_terminal"));
    } else if (socialQuestionOnly) {
        if (const std::optional<ExecutionIntentCandidate> local = findCandidate(InputRouteKind::LocalResponse)) {
            result.decision = local->route;
            result.confidence = local->score;
            result.reasonCodes = local->reasonCodes;
        } else {
            result.decision.kind = InputRouteKind::LocalResponse;
            result.decision.status = QStringLiteral("Local response");
            result.decision.message = QStringLiteral("I am doing well. How can I help?");
            result.confidence = 0.9f;
            result.reasonCodes = {QStringLiteral("arbitrator.social_question_local_response")};
        }
        result.reasonCodes.push_back(QStringLiteral("override.blocked.backend_for_social"));
    } else if (localTimeOrDate) {
        if (const std::optional<ExecutionIntentCandidate> local = findCandidateByReason(QStringLiteral("candidate.time_or_date"))) {
            result.decision = local->route;
            result.confidence = local->score;
            result.reasonCodes = local->reasonCodes;
        } else {
            result.decision = policyDecision;
            result.confidence = 0.98f;
            result.reasonCodes = {QStringLiteral("arbitrator.local_time_or_date_priority")};
        }
    } else if (shouldAskClarification) {
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
        result.reasonCodes.push_back(QStringLiteral("arbitrator.ask_clarification"));
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
    } else if (turnSignals.hasCommandCue && turnSignals.hasQuestionCue) {
        result.decision.kind = InputRouteKind::Conversation;
        result.confidence = 0.85f;
        result.reasonCodes = {QStringLiteral("arbitrator.command_vs_info_prefers_info_query")};
    } else if (shouldBackendAssist) {
        if (bestBackendCandidate.has_value()) {
            const ExecutionIntentCandidate backend = *bestBackendCandidate;
            result.decision = backend.route;
            result.confidence = std::max(backend.score, 0.68f);
            result.reasonCodes = {QStringLiteral("arbitrator.backend_escalation")};
            if (highAmbiguity && belowMediumConfidence && backendClearlyNeeded) {
                result.reasonCodes.push_back(QStringLiteral("arbitrator.high_ambiguity_backend_needed"));
            }
        } else {
            result.decision.kind = InputRouteKind::Conversation;
            result.confidence = 0.7f;
            result.reasonCodes = {QStringLiteral("arbitrator.backend_escalation_fallback")};
        }
    } else if (turnSignals.socialOnly) {
        result.decision.kind = InputRouteKind::LocalResponse;
        result.confidence = 0.9f;
        result.reasonCodes = {QStringLiteral("arbitrator.social_only_local_response")};
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
        if (result.decision.tasks.isEmpty() && !selected.tasks.isEmpty()) {
            result.decision.tasks = selected.tasks;
        }
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

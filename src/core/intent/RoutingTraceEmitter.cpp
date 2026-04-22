#include "core/intent/RoutingTraceEmitter.h"

#include <QJsonArray>
#include <QJsonDocument>

#include "logging/LoggingService.h"

namespace {
QString inputRouteKindToString(InputRouteKind kind)
{
    switch (kind) {
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

QJsonObject routeDecisionToJson(const InputRouteDecision &decision)
{
    QJsonObject object;
    object.insert(QStringLiteral("kind"), inputRouteKindToString(decision.kind));
    object.insert(QStringLiteral("intent"), static_cast<int>(decision.intent));
    object.insert(QStringLiteral("task_count"), decision.tasks.size());
    object.insert(QStringLiteral("status"), decision.status);
    return object;
}

QJsonArray stringsToArray(const QStringList &items)
{
    QJsonArray array;
    for (const QString &item : items) {
        array.push_back(item);
    }
    return array;
}

QString advisorModeToString(IntentAdvisorMode mode)
{
    switch (mode) {
    case IntentAdvisorMode::Heuristic:
        return QStringLiteral("heuristic");
    case IntentAdvisorMode::ShadowLearned:
        return QStringLiteral("shadow_learned");
    case IntentAdvisorMode::Learned:
        return QStringLiteral("learned");
    }
    return QStringLiteral("heuristic");
}
}

QJsonObject RoutingTraceEmitter::buildRouteFinalPayload(const RoutingTrace &trace) const
{
    QJsonObject payload;
    payload.insert(QStringLiteral("record"), QStringLiteral("route_final"));
    payload.insert(QStringLiteral("raw_input"), trace.rawInput);
    payload.insert(QStringLiteral("normalized_input"), trace.normalizedInput);
    payload.insert(QStringLiteral("deterministic_matched"), trace.deterministicMatched);
    payload.insert(QStringLiteral("deterministic_task_type"), trace.deterministicTaskType);
    payload.insert(QStringLiteral("ambiguity_score"), trace.ambiguityScore);
    payload.insert(QStringLiteral("advisor_mode"), advisorModeToString(trace.advisorMode));
    payload.insert(QStringLiteral("used_arbitrator_authority"), trace.usedArbitratorAuthority);
    payload.insert(QStringLiteral("final_executed_route"), trace.finalExecutedRoute);
    payload.insert(QStringLiteral("tool_selection_reason"), trace.toolSelectionReason);
    payload.insert(QStringLiteral("tool_suppression_reason"), trace.toolSuppressionReason);
    payload.insert(QStringLiteral("tools_available_count"), trace.toolsAvailableCount);
    payload.insert(QStringLiteral("clarification_trigger_reason"), trace.clarificationTriggerReason);
    payload.insert(QStringLiteral("ambiguity_threshold_used"), trace.ambiguityThresholdUsed);
    payload.insert(QStringLiteral("budget_enforcement_enabled"), trace.budgetEnforcementEnabled);
    payload.insert(QStringLiteral("budget_enforcement_disabled_reason"), trace.budgetEnforcementDisabledReason);
    payload.insert(QStringLiteral("technical_guard_triggered"), trace.technicalGuardTriggered);
    payload.insert(QStringLiteral("tool_loop_breaker_triggered"), trace.toolLoopBreakerTriggered);
    payload.insert(QStringLiteral("tool_loop_breaker_reason"), trace.toolLoopBreakerReason);
    payload.insert(QStringLiteral("failed_tool_attempt_count"), trace.failedToolAttemptCount);
    payload.insert(QStringLiteral("same_family_attempt_count"), trace.sameFamilyAttemptCount);
    payload.insert(QStringLiteral("graceful_fallback_reason"), trace.gracefulFallbackReason);
    payload.insert(QStringLiteral("confirmation_gate_triggered"), trace.confirmationGateTriggered);
    payload.insert(QStringLiteral("confirmation_outcome"), trace.confirmationOutcome);
    payload.insert(QStringLiteral("reason_codes"), stringsToArray(trace.reasonCodes));
    payload.insert(QStringLiteral("overrides"), stringsToArray(trace.overridesApplied));

    QJsonObject signalObject;
    signalObject.insert(QStringLiteral("has_greeting"), trace.turnSignals.hasGreeting);
    signalObject.insert(QStringLiteral("has_smalltalk"), trace.turnSignals.hasSmallTalk);
    signalObject.insert(QStringLiteral("social_only"), trace.turnSignals.socialOnly);
    signalObject.insert(QStringLiteral("has_command_cue"), trace.turnSignals.hasCommandCue);
    signalObject.insert(QStringLiteral("has_question_cue"), trace.turnSignals.hasQuestionCue);
    signalObject.insert(QStringLiteral("has_context_reference"), trace.turnSignals.hasContextReference);
    signalObject.insert(QStringLiteral("has_continuation_cue"), trace.turnSignals.hasContinuationCue);
    signalObject.insert(QStringLiteral("matched_cues"), stringsToArray(trace.turnSignals.matchedCues));
    payload.insert(QStringLiteral("turn_signals"), signalObject);

    QJsonObject legacyObject;
    legacyObject.insert(QStringLiteral("local_intent"), static_cast<int>(trace.legacySignals.localIntent));
    legacyObject.insert(QStringLiteral("likely_command"), trace.legacySignals.likelyCommand);
    legacyObject.insert(QStringLiteral("has_deterministic_task"), trace.legacySignals.hasDeterministicTask);
    legacyObject.insert(QStringLiteral("explicit_web_search"), trace.legacySignals.explicitWebSearch);
    legacyObject.insert(QStringLiteral("likely_knowledge_lookup"), trace.legacySignals.likelyKnowledgeLookup);
    legacyObject.insert(QStringLiteral("freshness_sensitive"), trace.legacySignals.freshnessSensitive);
    legacyObject.insert(QStringLiteral("desktop_context_recall"), trace.legacySignals.desktopContextRecall);
    legacyObject.insert(QStringLiteral("explicit_agent_world_query"), trace.legacySignals.explicitAgentWorldQuery);
    legacyObject.insert(QStringLiteral("explicit_computer_control"), trace.legacySignals.explicitComputerControl);
    payload.insert(QStringLiteral("legacy_signals"), legacyObject);

    QJsonObject turnState;
    turnState.insert(QStringLiteral("is_new_turn"), trace.turnState.isNewTurn);
    turnState.insert(QStringLiteral("is_continuation"), trace.turnState.isContinuation);
    turnState.insert(QStringLiteral("is_confirmation_reply"), trace.turnState.isConfirmationReply);
    turnState.insert(QStringLiteral("is_correction"), trace.turnState.isCorrection);
    turnState.insert(QStringLiteral("refers_to_previous_task"), trace.turnState.refersToPreviousTask);
    turnState.insert(QStringLiteral("reason_codes"), stringsToArray(trace.turnState.reasonCodes));
    payload.insert(QStringLiteral("turn_state"), turnState);

    QJsonObject goals;
    goals.insert(QStringLiteral("primary_kind"), static_cast<int>(trace.goals.primaryGoal.kind));
    goals.insert(QStringLiteral("primary_label"), trace.goals.primaryGoal.label);
    goals.insert(QStringLiteral("primary_confidence"), trace.goals.primaryGoal.confidence);
    goals.insert(QStringLiteral("mixed_intent"), trace.goals.mixedIntent);
    goals.insert(QStringLiteral("ambiguity"), trace.goals.ambiguity);
    if (trace.goals.secondaryGoal.has_value()) {
        goals.insert(QStringLiteral("secondary_kind"), static_cast<int>(trace.goals.secondaryGoal->kind));
        goals.insert(QStringLiteral("secondary_label"), trace.goals.secondaryGoal->label);
    }
    payload.insert(QStringLiteral("goals"), goals);

    QJsonArray candidateArray;
    for (const ExecutionIntentCandidate &candidate : trace.candidates) {
        QJsonObject candidateObject;
        candidateObject.insert(QStringLiteral("kind"), static_cast<int>(candidate.kind));
        candidateObject.insert(QStringLiteral("route"), inputRouteKindToString(candidate.route.kind));
        candidateObject.insert(QStringLiteral("score"), candidate.score);
        candidateObject.insert(QStringLiteral("requires_backend"), candidate.requiresBackend);
        candidateObject.insert(QStringLiteral("can_run_local"), candidate.canRunLocal);
        candidateObject.insert(QStringLiteral("backend_priority"), candidate.backendPriority);
        candidateObject.insert(QStringLiteral("confidence_penalty"), candidate.confidencePenalty);
        candidateObject.insert(QStringLiteral("reason_codes"), stringsToArray(candidate.reasonCodes));
        candidateArray.push_back(candidateObject);
    }
    payload.insert(QStringLiteral("execution_candidates"), candidateArray);

    QJsonObject confidence;
    confidence.insert(QStringLiteral("signal"), trace.intentConfidence.signalConfidence);
    confidence.insert(QStringLiteral("goal"), trace.intentConfidence.goalConfidence);
    confidence.insert(QStringLiteral("execution"), trace.intentConfidence.executionConfidence);
    confidence.insert(QStringLiteral("final"), trace.intentConfidence.finalConfidence);
    payload.insert(QStringLiteral("intent_confidence"), confidence);

    QJsonObject advisor;
    advisor.insert(QStringLiteral("available"), trace.advisorSuggestion.available);
    advisor.insert(QStringLiteral("ambiguity_boost"), trace.advisorSuggestion.ambiguityBoost);
    advisor.insert(QStringLiteral("continuation_likelihood"), trace.advisorSuggestion.continuationLikelihood);
    advisor.insert(QStringLiteral("backend_necessity"), trace.advisorSuggestion.backendNecessity);
    advisor.insert(QStringLiteral("reason_codes"), stringsToArray(trace.advisorSuggestion.reasonCodes));
    payload.insert(QStringLiteral("advisor_suggestion"), advisor);

    QJsonObject advisorEval;
    advisorEval.insert(QStringLiteral("base_ambiguity"), trace.advisorEvaluation.baseAmbiguity);
    advisorEval.insert(QStringLiteral("adjusted_ambiguity"), trace.advisorEvaluation.adjustedAmbiguity);
    advisorEval.insert(QStringLiteral("ambiguity_preference_changed"), trace.advisorEvaluation.ambiguityPreferenceChanged);
    advisorEval.insert(QStringLiteral("base_backend_preference"), trace.advisorEvaluation.baseBackendPreference);
    advisorEval.insert(QStringLiteral("adjusted_backend_preference"), trace.advisorEvaluation.adjustedBackendPreference);
    advisorEval.insert(QStringLiteral("backend_preference_changed"), trace.advisorEvaluation.backendPreferenceChanged);
    advisorEval.insert(QStringLiteral("reason_codes"), stringsToArray(trace.advisorEvaluation.reasonCodes));
    payload.insert(QStringLiteral("advisor_evaluation"), advisorEval);

    QJsonObject scores;
    scores.insert(QStringLiteral("social"), trace.arbitratorResult.scores.socialScore);
    scores.insert(QStringLiteral("command"), trace.arbitratorResult.scores.commandScore);
    scores.insert(QStringLiteral("info_query"), trace.arbitratorResult.scores.infoQueryScore);
    scores.insert(QStringLiteral("deterministic"), trace.arbitratorResult.scores.deterministicScore);
    scores.insert(QStringLiteral("continuation"), trace.arbitratorResult.scores.continuationScore);
    scores.insert(QStringLiteral("context_reference"), trace.arbitratorResult.scores.contextReferenceScore);
    scores.insert(QStringLiteral("backend_need"), trace.arbitratorResult.scores.backendNeedScore);
    payload.insert(QStringLiteral("scores"), scores);

    QJsonObject intentSnapshot;
    intentSnapshot.insert(QStringLiteral("ml_intent"), static_cast<int>(trace.intentSnapshot.mlIntent));
    intentSnapshot.insert(QStringLiteral("ml_confidence"), trace.intentSnapshot.mlConfidence);
    intentSnapshot.insert(QStringLiteral("detector_intent"), static_cast<int>(trace.intentSnapshot.detectorIntent));
    intentSnapshot.insert(QStringLiteral("detector_confidence"), trace.intentSnapshot.detectorConfidence);
    intentSnapshot.insert(QStringLiteral("effective_intent"), static_cast<int>(trace.intentSnapshot.effectiveIntent));
    intentSnapshot.insert(QStringLiteral("effective_confidence"), trace.intentSnapshot.effectiveConfidence);
    payload.insert(QStringLiteral("intent_snapshot"), intentSnapshot);

    payload.insert(QStringLiteral("policy_decision"), routeDecisionToJson(trace.policyDecision));
    payload.insert(QStringLiteral("arbitrator_decision"), routeDecisionToJson(trace.arbitratorResult.decision));
    payload.insert(QStringLiteral("final_decision"), routeDecisionToJson(trace.finalDecision));
    payload.insert(QStringLiteral("arbitrator_reason_codes"), stringsToArray(trace.arbitratorResult.reasonCodes));
    payload.insert(QStringLiteral("arbitrator_overrides"), stringsToArray(trace.arbitratorResult.overridesApplied));

    return payload;
}

void RoutingTraceEmitter::emitRouteFinal(LoggingService *loggingService, const RoutingTrace &trace) const
{
    if (loggingService == nullptr) {
        return;
    }
    const QJsonDocument document(buildRouteFinalPayload(trace));
    loggingService->infoFor(
        QStringLiteral("route_audit"),
        QStringLiteral("[route_final] %1").arg(QString::fromUtf8(document.toJson(QJsonDocument::Compact))));
}

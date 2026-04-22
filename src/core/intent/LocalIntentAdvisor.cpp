#include "core/intent/LocalIntentAdvisor.h"

#include <algorithm>

IntentAdvisorSuggestion LocalIntentAdvisor::suggest(const TurnSignals &turnSignals,
                                                    const TurnGoalSet &goals,
                                                    const TurnState &turnState,
                                                    const QList<ExecutionIntentCandidate> &candidates,
                                                    IntentAdvisorMode mode) const
{
    IntentAdvisorSuggestion suggestion;
    suggestion.available = false; // no local model wired yet; heuristic-only path

    float ambiguityBoost = 0.0f;
    if (turnSignals.hasCommandCue && turnSignals.hasQuestionCue) {
        ambiguityBoost += 0.2f;
        suggestion.reasonCodes.push_back(QStringLiteral("advisor.heuristic.conflicting_cues"));
    }
    if (goals.mixedIntent) {
        ambiguityBoost += 0.1f;
        suggestion.reasonCodes.push_back(QStringLiteral("advisor.heuristic.mixed_intent"));
    }
    suggestion.ambiguityBoost = std::clamp(ambiguityBoost, 0.0f, 1.0f);

    suggestion.continuationLikelihood = (turnState.isContinuation || turnSignals.hasContinuationCue) ? 0.85f : 0.15f;
    if (turnState.isContinuation && !turnState.refersToPreviousTask) {
        suggestion.continuationLikelihood = 0.35f;
        suggestion.reasonCodes.push_back(QStringLiteral("advisor.heuristic.continuation_missing_context"));
    }
    if (turnState.isContinuation) {
        suggestion.reasonCodes.push_back(QStringLiteral("advisor.heuristic.continuation_state"));
    }

    float backendNecessity = 0.2f;
    if (goals.primaryGoal.kind == UserGoalKind::InfoQuery) {
        backendNecessity += 0.35f;
    }
    if (candidates.size() > 1) {
        const float gap = std::max(0.0f, candidates.first().score - candidates.at(1).score);
        if (gap < 0.2f) {
            backendNecessity += 0.25f;
        }
    }
    suggestion.backendNecessity = std::clamp(backendNecessity, 0.0f, 1.0f);
    if (suggestion.backendNecessity >= 0.55f) {
        suggestion.reasonCodes.push_back(QStringLiteral("advisor.heuristic.backend_needed"));
    }

    if (mode == IntentAdvisorMode::ShadowLearned) {
        suggestion.reasonCodes.push_back(QStringLiteral("advisor.mode.shadow_learned"));
    } else if (mode == IntentAdvisorMode::Learned) {
        suggestion.reasonCodes.push_back(QStringLiteral("advisor.mode.learned_fallback_heuristic"));
    } else {
        suggestion.reasonCodes.push_back(QStringLiteral("advisor.mode.heuristic"));
    }

    return suggestion;
}

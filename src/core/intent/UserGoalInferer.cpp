#include "core/intent/UserGoalInferer.h"

namespace {
UserGoal makeGoal(UserGoalKind kind, const QString &label, float confidence, const QString &reason)
{
    UserGoal goal;
    goal.kind = kind;
    goal.label = label;
    goal.confidence = confidence;
    if (!reason.isEmpty()) {
        goal.reasonCodes.push_back(reason);
    }
    return goal;
}
}

TurnGoalSet UserGoalInferer::infer(const TurnSignals &turnSignals,
                                   const TurnState &state,
                                   bool hasDeterministicTask) const
{
    TurnGoalSet goals;
    goals.primaryGoal = makeGoal(UserGoalKind::Unknown,
                                 QStringLiteral("unknown"),
                                 0.2f,
                                 QStringLiteral("goal.unknown"));

    if (state.isConfirmationReply) {
        goals.primaryGoal = makeGoal(UserGoalKind::Confirmation,
                                     QStringLiteral("confirmation"),
                                     0.95f,
                                     QStringLiteral("goal.confirmation"));
    } else if (state.isCorrection) {
        goals.primaryGoal = makeGoal(UserGoalKind::Correction,
                                     QStringLiteral("correction"),
                                     std::max(0.75f, state.correctionConfidence),
                                     QStringLiteral("goal.correction"));
    } else if (hasDeterministicTask || turnSignals.hasDeterministicCue) {
        goals.primaryGoal = makeGoal(UserGoalKind::DeterministicAction,
                                     QStringLiteral("deterministic_action"),
                                     0.9f,
                                     QStringLiteral("goal.deterministic"));
    } else if (state.isContinuation) {
        goals.primaryGoal = makeGoal(UserGoalKind::Continuation,
                                     QStringLiteral("continuation"),
                                     0.85f,
                                     QStringLiteral("goal.continuation"));
    } else if (turnSignals.socialOnly) {
        goals.primaryGoal = makeGoal(UserGoalKind::Social,
                                     QStringLiteral("social"),
                                     0.85f,
                                     QStringLiteral("goal.social"));
    } else if (turnSignals.hasContextReference) {
        goals.primaryGoal = makeGoal(UserGoalKind::ContextReference,
                                     QStringLiteral("context_reference"),
                                     0.8f,
                                     QStringLiteral("goal.context_reference"));
    } else if (turnSignals.hasQuestionCue) {
        goals.primaryGoal = makeGoal(UserGoalKind::InfoQuery,
                                     QStringLiteral("info_query"),
                                     0.8f,
                                     QStringLiteral("goal.info_query"));
    } else if (turnSignals.hasCommandCue) {
        goals.primaryGoal = makeGoal(UserGoalKind::CommandRequest,
                                     QStringLiteral("command_request"),
                                     0.75f,
                                     QStringLiteral("goal.command"));
    }

    if (turnSignals.hasGreeting && (turnSignals.hasQuestionCue || turnSignals.hasCommandCue || hasDeterministicTask)) {
        goals.mixedIntent = true;
        goals.secondaryGoal = makeGoal(UserGoalKind::Social,
                                       QStringLiteral("social_prefix"),
                                       0.55f,
                                       QStringLiteral("goal.secondary.social_prefix"));
        goals.ambiguity = 0.35f;
    } else if (turnSignals.hasCommandCue && turnSignals.hasQuestionCue) {
        goals.mixedIntent = true;
        goals.secondaryGoal = makeGoal(UserGoalKind::CommandRequest,
                                       QStringLiteral("command_conflict"),
                                       0.5f,
                                       QStringLiteral("goal.secondary.command_conflict"));
        goals.ambiguity = 0.45f;
    }

    goals.confidence = goals.primaryGoal.confidence;
    return goals;
}

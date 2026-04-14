#include "cognition/ProactiveSuggestionPlanner.h"

#include <algorithm>

#include "cognition/CooldownEngine.h"
#include "cognition/SuggestionProposalBuilder.h"

namespace {
CompanionContextSnapshot contextFromDesktopState(const QVariantMap &desktopContext)
{
    CompanionContextSnapshot context;
    context.threadId = ContextThreadId{desktopContext.value(QStringLiteral("threadId")).toString().trimmed()};
    context.appId = desktopContext.value(QStringLiteral("appId")).toString().trimmed();
    context.taskId = desktopContext.value(QStringLiteral("taskId")).toString().trimmed();
    context.topic = desktopContext.value(QStringLiteral("topic")).toString().trimmed();
    context.recentIntent = desktopContext.value(QStringLiteral("recentIntent")).toString().trimmed();
    context.confidence = desktopContext.value(QStringLiteral("confidence")).toDouble();
    context.metadata = desktopContext;
    return context;
}

double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double proposalConfidence(const RankedSuggestionProposal &proposal,
                          const CompanionContextSnapshot &context,
                          bool success)
{
    double confidence = context.confidence > 0.0 ? context.confidence : 0.60;
    confidence += proposal.score * 0.18;
    if (!success) {
        confidence += 0.08;
    }
    if (proposal.proposal.priority.compare(QStringLiteral("high"), Qt::CaseInsensitive) == 0
        || proposal.proposal.priority.compare(QStringLiteral("critical"), Qt::CaseInsensitive) == 0) {
        confidence += 0.06;
    }
    return clamp01(confidence);
}

double proposalNovelty(const RankedSuggestionProposal &proposal,
                       const ProactiveSuggestionPlanner::Input &input,
                       const CompanionContextSnapshot &context)
{
    double novelty = 0.42;
    const QString currentThreadId = context.threadId.value;
    const bool meaningfulThreadShift = !currentThreadId.isEmpty()
        && currentThreadId != input.cooldownState.threadId;

    if (meaningfulThreadShift) {
        novelty += 0.28;
    }
    if (input.sourceKind == QStringLiteral("desktop_context")) {
        novelty += 0.12;
    }
    if (!input.success) {
        novelty += 0.14;
    }
    if (!input.sourceUrls.isEmpty()) {
        novelty += 0.05;
    }
    if (!input.cooldownState.lastTopic.isEmpty() && input.cooldownState.lastTopic != context.topic) {
        novelty += 0.10;
    }
    if (input.cooldownState.isActive(input.nowMs) && !meaningfulThreadShift) {
        novelty -= 0.16;
    }
    if (proposal.proposal.capabilityId == QStringLiteral("failure_recovery")) {
        novelty += 0.07;
    }

    return clamp01(novelty);
}

BehaviorDecision combineDecisions(const BehaviorDecision &cooldownDecision,
                                  const BehaviorDecision &gateDecision)
{
    if (!cooldownDecision.allowed) {
        return cooldownDecision;
    }
    if (!gateDecision.allowed) {
        return gateDecision;
    }

    BehaviorDecision decision = gateDecision;
    decision.reasonCode = cooldownDecision.reasonCode;
    decision.score = std::max(cooldownDecision.score, gateDecision.score);
    decision.details.insert(QStringLiteral("gateReasonCode"), gateDecision.reasonCode);
    decision.details.insert(QStringLiteral("cooldownReasonCode"), cooldownDecision.reasonCode);
    return decision;
}
}

ProactiveSuggestionPlan ProactiveSuggestionPlanner::plan(const Input &input)
{
    ProactiveSuggestionPlan plan;
    plan.context = contextFromDesktopState(input.desktopContext);
    plan.generatedProposals = SuggestionProposalBuilder::build({
        .sourceKind = input.sourceKind,
        .taskType = input.taskType,
        .resultSummary = input.resultSummary,
        .sourceUrls = input.sourceUrls,
        .success = input.success
    });

    if (plan.generatedProposals.isEmpty()) {
        plan.decision.allowed = false;
        plan.decision.action = QStringLiteral("no_proposal");
        plan.decision.reasonCode = QStringLiteral("proposal.none_generated");
        plan.decision.score = 1.0;
        plan.cooldownDecision = plan.decision;
        return plan;
    }

    plan.rankedProposals = SuggestionProposalRanker::rank({
        .proposals = plan.generatedProposals,
        .desktopContext = input.desktopContext,
        .desktopContextAtMs = input.desktopContextAtMs,
        .cooldownState = input.cooldownState,
        .focusMode = input.focusMode,
        .nowMs = input.nowMs
    });

    if (plan.rankedProposals.isEmpty()) {
        plan.decision.allowed = false;
        plan.decision.action = QStringLiteral("no_proposal");
        plan.decision.reasonCode = QStringLiteral("proposal.none_ranked");
        plan.decision.score = 1.0;
        plan.cooldownDecision = plan.decision;
        return plan;
    }

    plan.selectedProposal = plan.rankedProposals.first().proposal;
    plan.confidenceScore = proposalConfidence(plan.rankedProposals.first(), plan.context, input.success);
    plan.noveltyScore = proposalNovelty(plan.rankedProposals.first(), input, plan.context);

    CooldownEngine cooldownEngine;
    plan.cooldownDecision = cooldownEngine.evaluate({
        .context = plan.context,
        .state = input.cooldownState,
        .focusMode = input.focusMode,
        .priority = plan.selectedProposal.priority,
        .confidence = plan.confidenceScore,
        .novelty = plan.noveltyScore,
        .nowMs = input.nowMs
    });
    plan.nextCooldownState = cooldownEngine.advanceState({
        .context = plan.context,
        .state = input.cooldownState,
        .focusMode = input.focusMode,
        .priority = plan.selectedProposal.priority,
        .confidence = plan.confidenceScore,
        .novelty = plan.noveltyScore,
        .nowMs = input.nowMs
    }, plan.cooldownDecision);

    const BehaviorDecision gateDecision = ProactiveSuggestionGate::evaluate({
        .proposal = plan.selectedProposal,
        .desktopContext = input.desktopContext,
        .desktopContextAtMs = input.desktopContextAtMs,
        .focusMode = input.focusMode,
        .nowMs = input.nowMs
    });
    plan.decision = combineDecisions(plan.cooldownDecision, gateDecision);

    if (plan.decision.allowed) {
        plan.selectedSummary = plan.selectedProposal.summary.trimmed();
    }

    return plan;
}

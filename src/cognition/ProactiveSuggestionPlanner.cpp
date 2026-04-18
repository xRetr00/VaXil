#include "cognition/ProactiveSuggestionPlanner.h"

#include <algorithm>

#include "cognition/CooldownEngine.h"
#include "cognition/ProactivePlannerInputEnricher.h"
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
                       const CompanionContextSnapshot &context,
                       const QVariantMap &plannerMetadata,
                       QStringList *reasonCodes)
{
    double novelty = 0.42;
    const QString currentThreadId = context.threadId.value;
    const bool meaningfulThreadShift = !currentThreadId.isEmpty()
        && currentThreadId != input.cooldownState.threadId;
    const QString keyHint = proposal.proposal.arguments.value(QStringLiteral("presentationKeyHint")).toString().trimmed();
    const QString effectivePresentationKey = input.presentationKey.trimmed().isEmpty()
        ? keyHint
        : input.presentationKey.trimmed();
    const bool recentDuplicateKey = !effectivePresentationKey.isEmpty()
        && effectivePresentationKey == input.lastPresentedKey.trimmed()
        && input.lastPresentedAtMs > 0
        && input.nowMs > 0
        && (input.nowMs - input.lastPresentedAtMs) <= 120000;

    if (meaningfulThreadShift) {
        novelty += 0.28;
        *reasonCodes << QStringLiteral("novelty.thread_shift");
    }
    if (input.sourceKind == QStringLiteral("desktop_context")) {
        novelty += 0.12;
        *reasonCodes << QStringLiteral("novelty.desktop_context");
    }
    if (!input.success) {
        novelty += 0.14;
        *reasonCodes << QStringLiteral("novelty.failure");
    }
    if (!input.sourceUrls.isEmpty()) {
        novelty += 0.05;
        *reasonCodes << QStringLiteral("novelty.source_urls");
    }
    if (!input.cooldownState.lastTopic.isEmpty() && input.cooldownState.lastTopic != context.topic) {
        novelty += 0.10;
        *reasonCodes << QStringLiteral("novelty.topic_shift");
    }
    if (input.cooldownState.isActive(input.nowMs) && !meaningfulThreadShift) {
        novelty -= 0.16;
        *reasonCodes << QStringLiteral("novelty.active_cooldown");
    }
    if (recentDuplicateKey) {
        novelty -= 0.22;
        *reasonCodes << QStringLiteral("novelty.recent_duplicate");
    } else if (!keyHint.isEmpty()
               || !proposal.proposal.arguments.value(QStringLiteral("sourceLabel")).toString().trimmed().isEmpty()) {
        novelty += 0.04;
        *reasonCodes << QStringLiteral("novelty.presentation_evidence");
    }
    if (proposal.proposal.capabilityId == QStringLiteral("failure_recovery")) {
        novelty += 0.07;
        *reasonCodes << QStringLiteral("novelty.failure_recovery");
    }

    const QString plannerInputClass = plannerMetadata.value(QStringLiteral("plannerInputClass")).toString().trimmed();
    const QString sourcePolicy = plannerMetadata.value(QStringLiteral("sourceSpecificPolicy")).toString().trimmed();
    const QString freshnessBand = plannerMetadata.value(QStringLiteral("eventFreshnessBand")).toString().trimmed();
    if (plannerInputClass == QStringLiteral("connector_change")
        || sourcePolicy == QStringLiteral("connector_task_result")) {
        novelty += 0.04;
        *reasonCodes << QStringLiteral("novelty.connector_signal");
    }
    if (sourcePolicy == QStringLiteral("research_task_result")
        && plannerMetadata.value(QStringLiteral("sourceUrlCount")).toInt() > 0) {
        novelty += 0.06;
        *reasonCodes << QStringLiteral("novelty.rich_research");
    }
    if (sourcePolicy == QStringLiteral("low_signal_task_result")) {
        novelty -= 0.12;
        *reasonCodes << QStringLiteral("novelty.low_signal_task_result");
    }
    if (freshnessBand == QStringLiteral("fresh")) {
        novelty += 0.06;
        *reasonCodes << QStringLiteral("novelty.fresh_connector_event");
    } else if (freshnessBand == QStringLiteral("older")) {
        novelty -= 0.06;
        *reasonCodes << QStringLiteral("novelty.older_connector_event");
    }

    const int historySeenCount = plannerMetadata.value(QStringLiteral("historySeenCount")).toInt();
    const int recentSeenCount = plannerMetadata.value(QStringLiteral("connectorKindRecentSeenCount")).toInt();
    const int recentPresentedCount = plannerMetadata.value(QStringLiteral("connectorKindRecentPresentedCount")).toInt();
    if (recentSeenCount >= 4 && recentPresentedCount >= 2) {
        novelty -= 0.08;
        *reasonCodes << QStringLiteral("novelty.connector_burst");
    } else if (historySeenCount >= 3) {
        novelty -= 0.06;
        *reasonCodes << QStringLiteral("novelty.repeated_source");
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
    const QVariantMap plannerMetadata = ProactivePlannerInputEnricher::enrich({
        .sourceKind = input.sourceKind,
        .taskType = input.taskType,
        .resultSummary = input.resultSummary,
        .sourceUrls = input.sourceUrls,
        .sourceMetadata = input.sourceMetadata,
        .desktopContext = input.desktopContext,
        .nowMs = input.nowMs,
        .success = input.success
    });
    plan.generatedProposals = SuggestionProposalBuilder::build({
        .sourceKind = input.sourceKind,
        .taskType = input.taskType,
        .resultSummary = input.resultSummary,
        .sourceUrls = input.sourceUrls,
        .sourceMetadata = plannerMetadata,
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
        .sourceKind = input.sourceKind,
        .taskType = input.taskType,
        .sourceMetadata = plannerMetadata,
        .presentationKey = input.presentationKey,
        .lastPresentedKey = input.lastPresentedKey,
        .lastPresentedAtMs = input.lastPresentedAtMs,
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
    QStringList noveltyReasonCodes;
    plan.noveltyScore = proposalNovelty(plan.rankedProposals.first(),
                                        input,
                                        plan.context,
                                        plannerMetadata,
                                        &noveltyReasonCodes);

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
    plan.cooldownDecision.details.insert(QStringLiteral("noveltyReasonCodes"), noveltyReasonCodes);
    plan.cooldownDecision.details.insert(QStringLiteral("plannerInputClass"),
                                         plannerMetadata.value(QStringLiteral("plannerInputClass")).toString());
    plan.cooldownDecision.details.insert(QStringLiteral("sourceSpecificPolicy"),
                                         plannerMetadata.value(QStringLiteral("sourceSpecificPolicy")).toString());
    plan.cooldownDecision.details.insert(QStringLiteral("eventFreshnessBand"),
                                         plannerMetadata.value(QStringLiteral("eventFreshnessBand")).toString());
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
        .proposalScore = plan.rankedProposals.first().score,
        .sourceMetadata = plannerMetadata,
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

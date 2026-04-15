#include "cognition/SuggestionProposalRanker.h"
#include "cognition/SuggestionProposalPolicyScoring.h"

#include <algorithm>

#include <QDateTime>

namespace {
QString desktopTaskId(const QVariantMap &desktopContext)
{
    return desktopContext.value(QStringLiteral("taskId")).toString().trimmed();
}

QString desktopThreadId(const QVariantMap &desktopContext)
{
    return desktopContext.value(QStringLiteral("threadId")).toString().trimmed();
}

QString metadataString(const QVariantMap &metadata, const QString &key)
{
    return metadata.value(key).toString().trimmed();
}

QDateTime metadataDateTime(const QVariantMap &metadata, const QString &key)
{
    const QString value = metadataString(metadata, key);
    if (value.isEmpty()) {
        return {};
    }
    return QDateTime::fromString(value, Qt::ISODateWithMs);
}

QString effectiveConnectorKind(const SuggestionProposalRanker::Input &input)
{
    const QString connectorKind = metadataString(input.sourceMetadata, QStringLiteral("connectorKind"));
    if (!connectorKind.isEmpty()) {
        return connectorKind.toLower();
    }

    const QString taskType = input.taskType.trimmed().toLower();
    if (taskType.contains(QStringLiteral("calendar")) || taskType.contains(QStringLiteral("schedule"))) {
        return QStringLiteral("schedule");
    }
    if (taskType.contains(QStringLiteral("email")) || taskType.contains(QStringLiteral("mail")) || taskType.contains(QStringLiteral("inbox"))) {
        return QStringLiteral("inbox");
    }
    if (taskType.contains(QStringLiteral("note"))) {
        return QStringLiteral("notes");
    }
    if (taskType.contains(QStringLiteral("search")) || taskType.contains(QStringLiteral("web"))) {
        return QStringLiteral("research");
    }
    return {};
}

QString compiledHistoryMode(const SuggestionProposalRanker::Input &input)
{
    return metadataString(input.sourceMetadata, QStringLiteral("compiledContextHistoryMode")).toLower();
}

double connectorAffinityBonus(const SuggestionProposalRanker::Input &input,
                              const ActionProposal &proposal,
                              QString *reasonCode)
{
    const QString connectorKind = effectiveConnectorKind(input);
    if (connectorKind == QStringLiteral("inbox") && proposal.capabilityId == QStringLiteral("inbox_follow_up")) {
        *reasonCode = QStringLiteral("proposal_rank.connector_inbox_affinity");
        return 0.14;
    }
    if (connectorKind == QStringLiteral("schedule") && proposal.capabilityId == QStringLiteral("schedule_follow_up")) {
        *reasonCode = QStringLiteral("proposal_rank.connector_schedule_affinity");
        return 0.12;
    }
    if (connectorKind == QStringLiteral("notes") && proposal.capabilityId == QStringLiteral("note_follow_up")) {
        *reasonCode = QStringLiteral("proposal_rank.connector_notes_affinity");
        return 0.08;
    }
    if (connectorKind == QStringLiteral("research")
        && (proposal.capabilityId == QStringLiteral("web_follow_up")
            || proposal.capabilityId == QStringLiteral("source_review"))) {
        *reasonCode = QStringLiteral("proposal_rank.connector_research_affinity");
        return 0.10;
    }
    return 0.0;
}

double connectorFreshnessBonus(const SuggestionProposalRanker::Input &input, QString *reasonCode)
{
    const qint64 nowMs = input.nowMs;
    if (nowMs <= 0) {
        return 0.0;
    }

    const QDateTime occurredAt = metadataDateTime(input.sourceMetadata, QStringLiteral("occurredAtUtc"));
    if (occurredAt.isValid()) {
        const qint64 ageMs = occurredAt.toMSecsSinceEpoch() > 0 ? (nowMs - occurredAt.toMSecsSinceEpoch()) : 0;
        if (ageMs >= 0 && ageMs <= 10 * 60 * 1000) {
            *reasonCode = QStringLiteral("proposal_rank.connector_freshness_bonus");
            return 0.06;
        }
        if (ageMs > 6 * 60 * 60 * 1000) {
            *reasonCode = QStringLiteral("proposal_rank.connector_staleness_penalty");
            return -0.05;
        }
    }

    const bool upcoming = input.sourceMetadata.value(QStringLiteral("eventUpcoming")).toBool();
    const QDateTime startUtc = metadataDateTime(input.sourceMetadata, QStringLiteral("eventStartUtc"));
    if (upcoming && startUtc.isValid()) {
        const qint64 deltaMs = startUtc.toMSecsSinceEpoch() - nowMs;
        if (deltaMs >= 0 && deltaMs <= 4 * 60 * 60 * 1000) {
            *reasonCode = QStringLiteral("proposal_rank.schedule_upcoming_bonus");
            return 0.08;
        }
    }

    return 0.0;
}

double duplicatePenalty(const SuggestionProposalRanker::Input &input, QString *reasonCode)
{
    if (input.presentationKey.trimmed().isEmpty()
        || input.lastPresentedKey.trimmed().isEmpty()
        || input.lastPresentedAtMs <= 0
        || input.nowMs <= 0) {
        return 0.0;
    }

    if (input.presentationKey.trimmed() != input.lastPresentedKey.trimmed()) {
        return 0.0;
    }

    if ((input.nowMs - input.lastPresentedAtMs) > 120000) {
        return 0.0;
    }

    *reasonCode = QStringLiteral("proposal_rank.recent_duplicate_penalty");
    return -0.22;
}

double historyBurstPenalty(const SuggestionProposalRanker::Input &input, QString *reasonCode)
{
    const int connectorKindRecentSeenCount = input.sourceMetadata.value(QStringLiteral("connectorKindRecentSeenCount")).toInt();
    const int connectorKindRecentPresentedCount = input.sourceMetadata.value(QStringLiteral("connectorKindRecentPresentedCount")).toInt();
    const int historySeenCount = input.sourceMetadata.value(QStringLiteral("historySeenCount")).toInt();

    if (connectorKindRecentSeenCount >= 4 && connectorKindRecentPresentedCount >= 2) {
        *reasonCode = QStringLiteral("proposal_rank.connector_burst_penalty");
        return -0.10;
    }

    if (historySeenCount >= 3) {
        *reasonCode = QStringLiteral("proposal_rank.repeated_source_penalty");
        return -0.08;
    }

    return 0.0;
}

double compiledHistoryAffinityBonus(const SuggestionProposalRanker::Input &input,
                                    const ActionProposal &proposal,
                                    QString *reasonCode)
{
    if (input.sourceMetadata.value(QStringLiteral("compiledContextHistoryHasSchedule")).toBool()
        && proposal.capabilityId == QStringLiteral("schedule_follow_up")) {
        *reasonCode = QStringLiteral("proposal_rank.compiled_history_schedule_affinity");
        return 0.07;
    }
    if (input.sourceMetadata.value(QStringLiteral("compiledContextHistoryHasInbox")).toBool()
        && proposal.capabilityId == QStringLiteral("inbox_follow_up")) {
        *reasonCode = QStringLiteral("proposal_rank.compiled_history_inbox_affinity");
        return 0.07;
    }
    if (input.sourceMetadata.value(QStringLiteral("compiledContextHistoryHasResearch")).toBool()
        && (proposal.capabilityId == QStringLiteral("source_review")
            || proposal.capabilityId == QStringLiteral("web_follow_up"))) {
        *reasonCode = QStringLiteral("proposal_rank.compiled_history_research_affinity");
        return 0.06;
    }
    if (input.sourceMetadata.value(QStringLiteral("compiledContextHistoryHasDocument")).toBool()
        && (proposal.capabilityId == QStringLiteral("document_follow_up")
            || proposal.capabilityId == QStringLiteral("source_review"))) {
        *reasonCode = QStringLiteral("proposal_rank.compiled_history_document_affinity");
        return 0.06;
    }
    return 0.0;
}

double compiledHistoryStructuralAdjustment(const SuggestionProposalRanker::Input &input,
                                           const ActionProposal &proposal,
                                           QString *reasonCode)
{
    const QString mode = compiledHistoryMode(input);
    const QString capabilityId = proposal.capabilityId;

    if (mode == QStringLiteral("document_work")) {
        if (capabilityId == QStringLiteral("document_follow_up")
            || capabilityId == QStringLiteral("source_review")
            || capabilityId == QStringLiteral("browser_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.compiled_history_document_structure");
            return 0.12;
        }
        if (capabilityId == QStringLiteral("inbox_follow_up")
            || capabilityId == QStringLiteral("schedule_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.compiled_history_document_defocus");
            return -0.09;
        }
    }

    if (mode == QStringLiteral("schedule_coordination")) {
        if (capabilityId == QStringLiteral("schedule_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.compiled_history_schedule_structure");
            return 0.14;
        }
        if (capabilityId == QStringLiteral("document_follow_up")
            || capabilityId == QStringLiteral("source_review")) {
            *reasonCode = QStringLiteral("proposal_rank.compiled_history_schedule_defocus");
            return -0.07;
        }
    }

    if (mode == QStringLiteral("inbox_triage")) {
        if (capabilityId == QStringLiteral("inbox_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.compiled_history_inbox_structure");
            return 0.14;
        }
        if (capabilityId == QStringLiteral("document_follow_up")
            || capabilityId == QStringLiteral("schedule_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.compiled_history_inbox_defocus");
            return -0.07;
        }
    }

    if (mode == QStringLiteral("research_analysis")) {
        if (capabilityId == QStringLiteral("source_review")
            || capabilityId == QStringLiteral("web_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.compiled_history_research_structure");
            return 0.12;
        }
        if (capabilityId == QStringLiteral("inbox_follow_up")
            || capabilityId == QStringLiteral("schedule_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.compiled_history_research_defocus");
            return -0.08;
        }
    }

    return 0.0;
}
}

QList<RankedSuggestionProposal> SuggestionProposalRanker::rank(const Input &input)
{
    QList<RankedSuggestionProposal> ranked;
    const bool freshDesktopContext = hasFreshDesktopContext(input);
    const bool meaningfulThreadShift = hasMeaningfulThreadShift(input);
    const QString activeTaskId = desktopTaskId(input.desktopContext);

    for (const ActionProposal &proposal : input.proposals) {
        RankedSuggestionProposal rankedProposal;
        rankedProposal.proposal = proposal;
        rankedProposal.score = priorityScore(proposal.priority);
        rankedProposal.reasonCode = QStringLiteral("proposal_rank.priority_baseline");

        if (input.focusMode.enabled && proposal.priority.compare(QStringLiteral("high"), Qt::CaseInsensitive) != 0
            && proposal.priority.compare(QStringLiteral("critical"), Qt::CaseInsensitive) != 0) {
            rankedProposal.score -= 0.18;
            rankedProposal.reasonCode = QStringLiteral("proposal_rank.focus_penalty");
        }

        if (freshDesktopContext
            && (activeTaskId == QStringLiteral("browser_tab") || activeTaskId == QStringLiteral("editor_document"))) {
            if (proposal.capabilityId == QStringLiteral("source_review")
                || proposal.capabilityId == QStringLiteral("document_follow_up")
                || proposal.capabilityId == QStringLiteral("browser_follow_up")) {
                rankedProposal.score += 0.08;
                rankedProposal.reasonCode = QStringLiteral("proposal_rank.document_affinity");
            }
        }

        if (proposal.capabilityId == QStringLiteral("failure_recovery")) {
            rankedProposal.score += 0.05;
            rankedProposal.reasonCode = QStringLiteral("proposal_rank.failure_recovery_bonus");
        }

        QString connectorReasonCode;
        const double connectorBonus = connectorAffinityBonus(input, proposal, &connectorReasonCode);
        if (connectorBonus != 0.0) {
            rankedProposal.score += connectorBonus;
            rankedProposal.reasonCode = connectorReasonCode;
        }

        QString freshnessReasonCode;
        const double freshnessBonus = connectorFreshnessBonus(input, &freshnessReasonCode);
        if (freshnessBonus != 0.0) {
            rankedProposal.score += freshnessBonus;
            rankedProposal.reasonCode = freshnessReasonCode;
        }

        QString duplicateReasonCode;
        const double duplicateScore = duplicatePenalty(input, &duplicateReasonCode);
        if (duplicateScore != 0.0) {
            rankedProposal.score += duplicateScore;
            rankedProposal.reasonCode = duplicateReasonCode;
        }

        QString historyReasonCode;
        const double historyPenalty = historyBurstPenalty(input, &historyReasonCode);
        if (historyPenalty != 0.0) {
            rankedProposal.score += historyPenalty;
            rankedProposal.reasonCode = historyReasonCode;
        }

        QString compiledHistoryReasonCode;
        const double compiledHistoryBonus = compiledHistoryAffinityBonus(input, proposal, &compiledHistoryReasonCode);
        if (compiledHistoryBonus != 0.0) {
            rankedProposal.score += compiledHistoryBonus;
            rankedProposal.reasonCode = compiledHistoryReasonCode;
        }

        QString compiledHistoryStructureReasonCode;
        const double compiledHistoryStructureScore =
            compiledHistoryStructuralAdjustment(input, proposal, &compiledHistoryStructureReasonCode);
        if (compiledHistoryStructureScore != 0.0) {
            rankedProposal.score += compiledHistoryStructureScore;
            rankedProposal.reasonCode = compiledHistoryStructureReasonCode;
        }

        QString compiledPolicyFocusReasonCode;
        const double compiledPolicyFocusScore =
            SuggestionProposalPolicyScoring::compiledPolicyFocusAdjustment(input,
                                                                           proposal,
                                                                           &compiledPolicyFocusReasonCode);
        if (compiledPolicyFocusScore != 0.0) {
            rankedProposal.score += compiledPolicyFocusScore;
            rankedProposal.reasonCode = compiledPolicyFocusReasonCode;
        }

        QString compiledPolicySourceReasonCode;
        const double compiledPolicySourceScore =
            SuggestionProposalPolicyScoring::compiledPolicySourceAdjustment(input,
                                                                            proposal,
                                                                            &compiledPolicySourceReasonCode);
        if (compiledPolicySourceScore != 0.0) {
            rankedProposal.score += compiledPolicySourceScore;
            rankedProposal.reasonCode = compiledPolicySourceReasonCode;
        }

        QString compiledLayeredReasonCode;
        const double compiledLayeredScore =
            SuggestionProposalPolicyScoring::compiledLayeredAdjustment(input,
                                                                       proposal,
                                                                       &compiledLayeredReasonCode);
        if (compiledLayeredScore != 0.0) {
            rankedProposal.score += compiledLayeredScore;
            rankedProposal.reasonCode = compiledLayeredReasonCode;
        }

        QString compiledEvolutionReasonCode;
        const double compiledEvolutionScore =
            SuggestionProposalPolicyScoring::compiledEvolutionAdjustment(input,
                                                                         proposal,
                                                                         &compiledEvolutionReasonCode);
        if (compiledEvolutionScore != 0.0) {
            rankedProposal.score += compiledEvolutionScore;
            rankedProposal.reasonCode = compiledEvolutionReasonCode;
        }

        QString compiledTuningReasonCode;
        const double compiledTuningScore =
            SuggestionProposalPolicyScoring::compiledTuningAdjustment(input,
                                                                      proposal,
                                                                      &compiledTuningReasonCode);
        if (compiledTuningScore != 0.0) {
            rankedProposal.score += compiledTuningScore;
            rankedProposal.reasonCode = compiledTuningReasonCode;
        }

        if (input.cooldownState.isActive(input.nowMs) && !meaningfulThreadShift) {
            if (proposal.priority.compare(QStringLiteral("high"), Qt::CaseInsensitive) == 0
                || proposal.priority.compare(QStringLiteral("critical"), Qt::CaseInsensitive) == 0) {
                rankedProposal.score += 0.04;
                rankedProposal.reasonCode = QStringLiteral("proposal_rank.cooldown_break_candidate");
            } else {
                rankedProposal.score -= 0.12;
                rankedProposal.reasonCode = QStringLiteral("proposal_rank.cooldown_penalty");
            }
        } else if (meaningfulThreadShift) {
            rankedProposal.score += 0.06;
            rankedProposal.reasonCode = QStringLiteral("proposal_rank.thread_shift_bonus");
        }

        ranked.push_back(rankedProposal);
    }

    std::sort(ranked.begin(), ranked.end(), [](const RankedSuggestionProposal &lhs, const RankedSuggestionProposal &rhs) {
        return lhs.score > rhs.score;
    });
    return ranked;
}

double SuggestionProposalRanker::priorityScore(const QString &priority)
{
    const QString normalized = priority.trimmed().toLower();
    if (normalized == QStringLiteral("critical")) {
        return 0.95;
    }
    if (normalized == QStringLiteral("high")) {
        return 0.84;
    }
    if (normalized == QStringLiteral("medium")) {
        return 0.68;
    }
    return 0.52;
}

bool SuggestionProposalRanker::hasFreshDesktopContext(const Input &input)
{
    return input.desktopContextAtMs > 0 && (input.nowMs - input.desktopContextAtMs) <= 90000;
}

bool SuggestionProposalRanker::hasMeaningfulThreadShift(const Input &input)
{
    const QString currentThreadId = desktopThreadId(input.desktopContext);
    return !currentThreadId.isEmpty() && currentThreadId != input.cooldownState.threadId;
}

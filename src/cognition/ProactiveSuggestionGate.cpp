#include "cognition/ProactiveSuggestionGate.h"

#include <QRegularExpression>

namespace {
QString metadataString(const QVariantMap &metadata, const QString &key)
{
    return metadata.value(key).toString().trimmed().toLower();
}

double metadataDouble(const QVariantMap &metadata, const QString &key, double fallback)
{
    bool ok = false;
    const double value = metadata.value(key).toDouble(&ok);
    return ok ? value : fallback;
}

int observedCount(const QString &summary)
{
    const QRegularExpression regex(QStringLiteral("observed\\s+(\\d+)\\s+times"));
    const QRegularExpressionMatch match = regex.match(summary);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

int modeShiftCount(const QString &summary)
{
    const QRegularExpression regex(QStringLiteral("across\\s+(\\d+)\\s+mode\\s+shifts"));
    const QRegularExpressionMatch match = regex.match(summary);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

bool isResearchProposal(const QString &capabilityId)
{
    return capabilityId == QStringLiteral("source_review")
        || capabilityId == QStringLiteral("web_follow_up");
}

bool isDocumentProposal(const QString &capabilityId)
{
    return capabilityId == QStringLiteral("document_follow_up")
        || capabilityId == QStringLiteral("browser_follow_up");
}

bool isInboxProposal(const QString &capabilityId)
{
    return capabilityId == QStringLiteral("inbox_follow_up");
}

bool isScheduleProposal(const QString &capabilityId)
{
    return capabilityId == QStringLiteral("schedule_follow_up");
}
}

BehaviorDecision ProactiveSuggestionGate::evaluate(const Input &input)
{
    BehaviorDecision decision;
    decision.allowed = true;
    decision.action = QStringLiteral("allow_proposal");
    decision.reasonCode = QStringLiteral("proposal.default_allow");
    decision.score = 0.74;
    decision.details.insert(QStringLiteral("proposalId"), input.proposal.proposalId);
    decision.details.insert(QStringLiteral("capabilityId"), input.proposal.capabilityId);
    decision.details.insert(QStringLiteral("proposalTitle"), input.proposal.title);
    decision.details.insert(QStringLiteral("proposalPriority"), input.proposal.priority);
    decision.details.insert(QStringLiteral("desktopTaskId"), input.desktopContext.value(QStringLiteral("taskId")).toString());
    decision.details.insert(QStringLiteral("desktopThreadId"), input.desktopContext.value(QStringLiteral("threadId")).toString());
    decision.details.insert(QStringLiteral("focusModeEnabled"), input.focusMode.enabled);

    if (isHighPriority(input.proposal.priority)) {
        decision.reasonCode = QStringLiteral("proposal.high_priority_allow");
        decision.score = 0.92;
        return decision;
    }

    if (input.focusMode.enabled) {
        decision.allowed = false;
        decision.action = QStringLiteral("suppress_proposal");
        decision.reasonCode = QStringLiteral("proposal.focus_mode_suppressed");
        decision.score = 0.97;
        return decision;
    }

    const QString capabilityId = input.proposal.capabilityId.trimmed();
    const QString layeredSummary = metadataString(input.sourceMetadata,
                                                  QStringLiteral("compiledContextLayeredSummary"));
    const QString evolutionSummary = metadataString(input.sourceMetadata,
                                                    QStringLiteral("compiledContextEvolutionSummary"));
    const QString tuningSummary = metadataString(input.sourceMetadata,
                                                 QStringLiteral("compiledContextTuningSummary"));
    if (!layeredSummary.isEmpty()) {
        if (layeredSummary.contains(QStringLiteral("research analysis remains active"))
            && (isInboxProposal(capabilityId) || isScheduleProposal(capabilityId))) {
            decision.allowed = false;
            decision.action = QStringLiteral("suppress_proposal");
            decision.reasonCode = QStringLiteral("proposal.layered_policy_research_defocus");
            decision.score = 0.89;
            return decision;
        }
        if (layeredSummary.contains(QStringLiteral("inbox triage remains active"))
            && (isDocumentProposal(capabilityId) || isScheduleProposal(capabilityId))) {
            decision.allowed = false;
            decision.action = QStringLiteral("suppress_proposal");
            decision.reasonCode = QStringLiteral("proposal.layered_policy_inbox_defocus");
            decision.score = 0.89;
            return decision;
        }
        if (layeredSummary.contains(QStringLiteral("schedule coordination remains active"))
            && (isDocumentProposal(capabilityId) || isResearchProposal(capabilityId))) {
            decision.allowed = false;
            decision.action = QStringLiteral("suppress_proposal");
            decision.reasonCode = QStringLiteral("proposal.layered_policy_schedule_defocus");
            decision.score = 0.89;
            return decision;
        }
        if (layeredSummary.contains(QStringLiteral("document-focused work"))
            && (isInboxProposal(capabilityId) || isScheduleProposal(capabilityId))) {
            decision.allowed = false;
            decision.action = QStringLiteral("suppress_proposal");
            decision.reasonCode = QStringLiteral("proposal.layered_policy_document_defocus");
            decision.score = 0.87;
            return decision;
        }
    }

    if (!evolutionSummary.isEmpty()) {
        const int observations = observedCount(evolutionSummary);
        const int shifts = modeShiftCount(evolutionSummary);
        if (evolutionSummary.contains(QStringLiteral("current mode research_analysis"))
            && observations >= 3
            && (isInboxProposal(capabilityId) || isScheduleProposal(capabilityId))) {
            decision.allowed = false;
            decision.action = QStringLiteral("suppress_proposal");
            decision.reasonCode = QStringLiteral("proposal.evolution_research_defocus");
            decision.score = 0.9;
            return decision;
        }
        if (evolutionSummary.contains(QStringLiteral("current mode inbox_triage"))
            && observations >= 3
            && (isDocumentProposal(capabilityId) || isScheduleProposal(capabilityId))) {
            decision.allowed = false;
            decision.action = QStringLiteral("suppress_proposal");
            decision.reasonCode = QStringLiteral("proposal.evolution_inbox_defocus");
            decision.score = 0.9;
            return decision;
        }
        if (shifts >= 2
            && input.proposal.priority.trimmed().compare(QStringLiteral("medium"), Qt::CaseInsensitive) == 0
            && evolutionSummary.contains(QStringLiteral("current mode research_analysis"))
            && (isInboxProposal(capabilityId) || isScheduleProposal(capabilityId))) {
            decision.allowed = false;
            decision.action = QStringLiteral("suppress_proposal");
            decision.reasonCode = QStringLiteral("proposal.evolution_transition_defocus");
            decision.score = 0.91;
            return decision;
        }
    }

    if (!tuningSummary.isEmpty()
        && input.proposal.priority.trimmed().compare(QStringLiteral("medium"), Qt::CaseInsensitive) == 0) {
        const double suppressionThreshold = metadataDouble(input.sourceMetadata,
                                                           QStringLiteral("tuningSuppressionScoreThreshold"),
                                                           0.72);
        if (tuningSummary.contains(QStringLiteral("policy volatility: elevated"))
            && tuningSummary.contains(QStringLiteral("policy stability bias: research_analysis"))
            && input.proposalScore <= suppressionThreshold
            && (isInboxProposal(capabilityId) || isScheduleProposal(capabilityId))) {
            decision.allowed = false;
            decision.action = QStringLiteral("suppress_proposal");
            decision.reasonCode = QStringLiteral("proposal.tuning_volatility_research_defocus");
            decision.score = 0.92;
            return decision;
        }
        if (tuningSummary.contains(QStringLiteral("policy volatility: elevated"))
            && tuningSummary.contains(QStringLiteral("policy stability bias: inbox_triage"))
            && input.proposalScore <= suppressionThreshold
            && (isDocumentProposal(capabilityId) || isScheduleProposal(capabilityId))) {
            decision.allowed = false;
            decision.action = QStringLiteral("suppress_proposal");
            decision.reasonCode = QStringLiteral("proposal.tuning_volatility_inbox_defocus");
            decision.score = 0.92;
            return decision;
        }
    }

    if (hasFreshDesktopContext(input)) {
        const QString desktopTaskId = input.desktopContext.value(QStringLiteral("taskId")).toString().trimmed();
        if (desktopTaskId == QStringLiteral("editor_document")
            || desktopTaskId == QStringLiteral("browser_tab")) {
            decision.allowed = false;
            decision.action = QStringLiteral("suppress_proposal");
            decision.reasonCode = QStringLiteral("proposal.focused_context_suppressed");
            decision.score = 0.88;
            return decision;
        }

        decision.reasonCode = QStringLiteral("proposal.context_checked_allow");
        decision.score = 0.81;
    }

    return decision;
}

bool ProactiveSuggestionGate::hasFreshDesktopContext(const Input &input)
{
    return input.desktopContextAtMs > 0 && (input.nowMs - input.desktopContextAtMs) <= 90000;
}

bool ProactiveSuggestionGate::isHighPriority(const QString &priority)
{
    const QString normalized = priority.trimmed().toLower();
    return normalized == QStringLiteral("high") || normalized == QStringLiteral("critical");
}

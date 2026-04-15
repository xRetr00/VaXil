#include "cognition/SuggestionProposalPolicyScoring.h"

#include <QMetaType>
#include <QRegularExpression>
#include <QVariantList>

namespace {
QString metadataString(const QVariantMap &metadata, const QString &key)
{
    return metadata.value(key).toString().trimmed();
}

QStringList metadataStringList(const QVariantMap &metadata, const QString &key)
{
    QStringList values;
    const QVariant value = metadata.value(key);
    if (!value.isValid()) {
        return values;
    }

    if (value.canConvert<QStringList>()) {
        const QStringList list = value.toStringList();
        for (const QString &entry : list) {
            const QString normalized = entry.trimmed();
            if (!normalized.isEmpty()) {
                values.push_back(normalized);
            }
        }
        return values;
    }

    if (value.typeId() == QMetaType::QVariantList) {
        const QVariantList list = value.toList();
        for (const QVariant &entry : list) {
            const QString normalized = entry.toString().trimmed();
            if (!normalized.isEmpty()) {
                values.push_back(normalized);
            }
        }
        return values;
    }

    const QString normalized = value.toString().trimmed();
    if (!normalized.isEmpty()) {
        values.push_back(normalized);
    }
    return values;
}

QString compiledPolicySummary(const SuggestionProposalRanker::Input &input)
{
    return metadataString(input.sourceMetadata, QStringLiteral("compiledContextPolicySummary")).toLower();
}

QStringList compiledPolicySummaryKeys(const SuggestionProposalRanker::Input &input)
{
    QStringList keys = metadataStringList(input.sourceMetadata,
                                          QStringLiteral("compiledContextPolicySummaryKeys"));
    for (QString &key : keys) {
        key = key.toLower();
    }
    return keys;
}

QString compiledLayeredSummary(const SuggestionProposalRanker::Input &input)
{
    return metadataString(input.sourceMetadata, QStringLiteral("compiledContextLayeredSummary")).toLower();
}

QString compiledEvolutionSummary(const SuggestionProposalRanker::Input &input)
{
    return metadataString(input.sourceMetadata, QStringLiteral("compiledContextEvolutionSummary")).toLower();
}

QString compiledTuningSummary(const SuggestionProposalRanker::Input &input)
{
    return metadataString(input.sourceMetadata, QStringLiteral("compiledContextTuningSummary")).toLower();
}

double metadataDouble(const QVariantMap &metadata, const QString &key, double fallback)
{
    bool ok = false;
    const double parsed = metadata.value(key).toDouble(&ok);
    return ok ? parsed : fallback;
}

QStringList compiledLayeredKeys(const SuggestionProposalRanker::Input &input)
{
    QStringList keys = metadataStringList(input.sourceMetadata,
                                          QStringLiteral("compiledContextLayeredKeys"));
    for (QString &key : keys) {
        key = key.toLower();
    }
    return keys;
}

QString evolutionCurrentMode(const QString &summary)
{
    if (summary.contains(QStringLiteral("current mode research_analysis"))) {
        return QStringLiteral("research_analysis");
    }
    if (summary.contains(QStringLiteral("current mode inbox_triage"))) {
        return QStringLiteral("inbox_triage");
    }
    if (summary.contains(QStringLiteral("current mode schedule_coordination"))) {
        return QStringLiteral("schedule_coordination");
    }
    if (summary.contains(QStringLiteral("current mode document_work"))) {
        return QStringLiteral("document_work");
    }
    return {};
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

bool summaryContainsAny(const QString &summary, std::initializer_list<const char *> patterns)
{
    for (const char *pattern : patterns) {
        if (summary.contains(QLatin1String(pattern))) {
            return true;
        }
    }
    return false;
}
}

namespace SuggestionProposalPolicyScoring {

double compiledPolicyFocusAdjustment(const SuggestionProposalRanker::Input &input,
                                     const ActionProposal &proposal,
                                     QString *reasonCode)
{
    const QString summary = compiledPolicySummary(input);
    const QStringList keys = compiledPolicySummaryKeys(input);
    if (!keys.contains(QStringLiteral("compiled_context_policy_focus")) || summary.isEmpty()) {
        return 0.0;
    }

    const QString capabilityId = proposal.capabilityId;
    if (summaryContainsAny(summary, {"message review", "reply preparation", "summarization"})) {
        if (capabilityId == QStringLiteral("inbox_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.policy_focus_inbox");
            return 0.10;
        }
        if (capabilityId == QStringLiteral("document_follow_up")
            || capabilityId == QStringLiteral("schedule_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.policy_focus_inbox_defocus");
            return -0.05;
        }
    }

    if (summaryContainsAny(summary, {"calendar", "meeting-aware", "deadline"})) {
        if (capabilityId == QStringLiteral("schedule_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.policy_focus_schedule");
            return 0.10;
        }
        if (capabilityId == QStringLiteral("document_follow_up")
            || capabilityId == QStringLiteral("source_review")) {
            *reasonCode = QStringLiteral("proposal_rank.policy_focus_schedule_defocus");
            return -0.05;
        }
    }

    if (summaryContainsAny(summary, {"source review", "evidence-grounded browsing", "synthesis"})) {
        if (capabilityId == QStringLiteral("source_review")
            || capabilityId == QStringLiteral("web_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.policy_focus_research");
            return 0.09;
        }
        if (capabilityId == QStringLiteral("inbox_follow_up")
            || capabilityId == QStringLiteral("schedule_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.policy_focus_research_defocus");
            return -0.05;
        }
    }

    if (summaryContainsAny(summary, {"document", "workspace", "file-grounded"})) {
        if (capabilityId == QStringLiteral("document_follow_up")
            || capabilityId == QStringLiteral("browser_follow_up")
            || capabilityId == QStringLiteral("source_review")) {
            *reasonCode = QStringLiteral("proposal_rank.policy_focus_document");
            return 0.09;
        }
        if (capabilityId == QStringLiteral("inbox_follow_up")
            || capabilityId == QStringLiteral("schedule_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.policy_focus_document_defocus");
            return -0.05;
        }
    }

    return 0.0;
}

double compiledPolicySourceAdjustment(const SuggestionProposalRanker::Input &input,
                                      const ActionProposal &proposal,
                                      QString *reasonCode)
{
    const QString summary = compiledPolicySummary(input);
    const QStringList keys = compiledPolicySummaryKeys(input);
    if (!keys.contains(QStringLiteral("compiled_context_policy_sources")) || summary.isEmpty()) {
        return 0.0;
    }

    const QString capabilityId = proposal.capabilityId;
    if (summary.contains(QStringLiteral("connector_inbox_maildrop"))
        && capabilityId == QStringLiteral("inbox_follow_up")) {
        *reasonCode = QStringLiteral("proposal_rank.policy_source_inbox");
        return 0.06;
    }
    if (summary.contains(QStringLiteral("connector_schedule_calendar"))
        && capabilityId == QStringLiteral("schedule_follow_up")) {
        *reasonCode = QStringLiteral("proposal_rank.policy_source_schedule");
        return 0.06;
    }
    if (summary.contains(QStringLiteral("connector_research_browser"))
        && (capabilityId == QStringLiteral("source_review")
            || capabilityId == QStringLiteral("web_follow_up"))) {
        *reasonCode = QStringLiteral("proposal_rank.policy_source_research");
        return 0.05;
    }
    if (summary.contains(QStringLiteral("document and workspace continuity"))
        && (capabilityId == QStringLiteral("document_follow_up")
            || capabilityId == QStringLiteral("browser_follow_up"))) {
        *reasonCode = QStringLiteral("proposal_rank.policy_source_document");
        return 0.05;
    }
    return 0.0;
}

double compiledLayeredAdjustment(const SuggestionProposalRanker::Input &input,
                                 const ActionProposal &proposal,
                                 QString *reasonCode)
{
    const QString summary = compiledLayeredSummary(input);
    const QStringList keys = compiledLayeredKeys(input);
    if (summary.isEmpty() || keys.isEmpty()) {
        return 0.0;
    }

    const QString capabilityId = proposal.capabilityId;
    if (keys.contains(QStringLiteral("compiled_context_layered_focus"))
        && summaryContainsAny(summary, {"research analysis remains active", "source review", "evidence-grounded"})) {
        if (capabilityId == QStringLiteral("source_review")
            || capabilityId == QStringLiteral("web_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.layered_focus_research");
            return 0.08;
        }
        if (capabilityId == QStringLiteral("inbox_follow_up")
            || capabilityId == QStringLiteral("schedule_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.layered_focus_research_defocus");
            return -0.07;
        }
    }

    if (keys.contains(QStringLiteral("compiled_context_layered_focus"))
        && summaryContainsAny(summary, {"inbox triage remains active", "message review", "reply preparation"})) {
        if (capabilityId == QStringLiteral("inbox_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.layered_focus_inbox");
            return 0.08;
        }
        if (capabilityId == QStringLiteral("document_follow_up")
            || capabilityId == QStringLiteral("schedule_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.layered_focus_inbox_defocus");
            return -0.07;
        }
    }

    if (keys.contains(QStringLiteral("compiled_context_layered_focus"))
        && summaryContainsAny(summary, {"schedule coordination remains active", "meeting-aware", "deadline"})) {
        if (capabilityId == QStringLiteral("schedule_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.layered_focus_schedule");
            return 0.08;
        }
        if (capabilityId == QStringLiteral("document_follow_up")
            || capabilityId == QStringLiteral("source_review")) {
            *reasonCode = QStringLiteral("proposal_rank.layered_focus_schedule_defocus");
            return -0.07;
        }
    }

    if (keys.contains(QStringLiteral("compiled_context_layered_focus"))
        && summaryContainsAny(summary, {"document-focused work", "workspace", "file-grounded"})) {
        if (capabilityId == QStringLiteral("document_follow_up")
            || capabilityId == QStringLiteral("browser_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.layered_focus_document");
            return 0.07;
        }
        if (capabilityId == QStringLiteral("inbox_follow_up")
            || capabilityId == QStringLiteral("schedule_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.layered_focus_document_defocus");
            return -0.06;
        }
    }

    if (keys.contains(QStringLiteral("compiled_context_layered_continuity"))
        && summary.contains(QStringLiteral("connector_inbox_maildrop"))
        && capabilityId == QStringLiteral("inbox_follow_up")) {
        *reasonCode = QStringLiteral("proposal_rank.layered_continuity_inbox");
        return 0.06;
    }
    if (keys.contains(QStringLiteral("compiled_context_layered_continuity"))
        && summary.contains(QStringLiteral("connector_schedule_calendar"))
        && capabilityId == QStringLiteral("schedule_follow_up")) {
        *reasonCode = QStringLiteral("proposal_rank.layered_continuity_schedule");
        return 0.06;
    }
    return 0.0;
}

double compiledEvolutionAdjustment(const SuggestionProposalRanker::Input &input,
                                   const ActionProposal &proposal,
                                   QString *reasonCode)
{
    const QString summary = compiledEvolutionSummary(input);
    if (summary.isEmpty()) {
        return 0.0;
    }

    const QString currentMode = evolutionCurrentMode(summary);
    const int observations = observedCount(summary);
    const int shifts = modeShiftCount(summary);
    const QString capabilityId = proposal.capabilityId;

    if (currentMode == QStringLiteral("research_analysis") && observations >= 2) {
        if (capabilityId == QStringLiteral("source_review")
            || capabilityId == QStringLiteral("web_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.evolution_sustained_research");
            return 0.09;
        }
        if (capabilityId == QStringLiteral("inbox_follow_up")
            || capabilityId == QStringLiteral("schedule_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.evolution_sustained_research_defocus");
            return -0.08;
        }
    }

    if (currentMode == QStringLiteral("inbox_triage") && observations >= 2) {
        if (capabilityId == QStringLiteral("inbox_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.evolution_sustained_inbox");
            return 0.09;
        }
        if (capabilityId == QStringLiteral("document_follow_up")
            || capabilityId == QStringLiteral("schedule_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.evolution_sustained_inbox_defocus");
            return -0.08;
        }
    }

    if (currentMode == QStringLiteral("schedule_coordination") && observations >= 2) {
        if (capabilityId == QStringLiteral("schedule_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.evolution_sustained_schedule");
            return 0.09;
        }
        if (capabilityId == QStringLiteral("document_follow_up")
            || capabilityId == QStringLiteral("source_review")) {
            *reasonCode = QStringLiteral("proposal_rank.evolution_sustained_schedule_defocus");
            return -0.08;
        }
    }

    if (currentMode == QStringLiteral("document_work") && observations >= 2) {
        if (capabilityId == QStringLiteral("document_follow_up")
            || capabilityId == QStringLiteral("browser_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.evolution_sustained_document");
            return 0.08;
        }
        if (capabilityId == QStringLiteral("inbox_follow_up")
            || capabilityId == QStringLiteral("schedule_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.evolution_sustained_document_defocus");
            return -0.07;
        }
    }

    if (shifts >= 2
        && proposal.priority.compare(QStringLiteral("medium"), Qt::CaseInsensitive) == 0
        && currentMode == QStringLiteral("research_analysis")
        && (capabilityId == QStringLiteral("inbox_follow_up")
            || capabilityId == QStringLiteral("schedule_follow_up"))) {
        *reasonCode = QStringLiteral("proposal_rank.evolution_transition_penalty");
        return -0.10;
    }

    return 0.0;
}

double compiledTuningAdjustment(const SuggestionProposalRanker::Input &input,
                                const ActionProposal &proposal,
                                QString *reasonCode)
{
    const QString summary = compiledTuningSummary(input);
    if (summary.isEmpty()) {
        return 0.0;
    }

    const QString capabilityId = proposal.capabilityId;
    const bool mediumPriority =
        proposal.priority.trimmed().compare(QStringLiteral("medium"), Qt::CaseInsensitive) == 0;
    const double alignmentBoost = metadataDouble(input.sourceMetadata,
                                                 QStringLiteral("tuningAlignmentBoost"),
                                                 0.08);
    const double defocusPenalty = metadataDouble(input.sourceMetadata,
                                                 QStringLiteral("tuningDefocusPenalty"),
                                                 0.07);
    const double volatilityPenalty = metadataDouble(input.sourceMetadata,
                                                    QStringLiteral("tuningVolatilityPenalty"),
                                                    0.05);

    if (summary.contains(QStringLiteral("policy stability bias: research_analysis"))) {
        if (capabilityId == QStringLiteral("source_review")
            || capabilityId == QStringLiteral("web_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.tuning_stability_research");
            return alignmentBoost;
        }
        if (mediumPriority
            && (capabilityId == QStringLiteral("inbox_follow_up")
                || capabilityId == QStringLiteral("schedule_follow_up"))) {
            *reasonCode = QStringLiteral("proposal_rank.tuning_stability_research_defocus");
            return -defocusPenalty;
        }
    }

    if (summary.contains(QStringLiteral("policy stability bias: inbox_triage"))) {
        if (capabilityId == QStringLiteral("inbox_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.tuning_stability_inbox");
            return alignmentBoost;
        }
        if (mediumPriority
            && (capabilityId == QStringLiteral("document_follow_up")
                || capabilityId == QStringLiteral("schedule_follow_up"))) {
            *reasonCode = QStringLiteral("proposal_rank.tuning_stability_inbox_defocus");
            return -defocusPenalty;
        }
    }

    if (summary.contains(QStringLiteral("policy stability bias: schedule_coordination"))) {
        if (capabilityId == QStringLiteral("schedule_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.tuning_stability_schedule");
            return alignmentBoost;
        }
        if (mediumPriority
            && (capabilityId == QStringLiteral("document_follow_up")
                || capabilityId == QStringLiteral("source_review"))) {
            *reasonCode = QStringLiteral("proposal_rank.tuning_stability_schedule_defocus");
            return -defocusPenalty;
        }
    }

    if (summary.contains(QStringLiteral("policy stability bias: document_work"))) {
        if (capabilityId == QStringLiteral("document_follow_up")
            || capabilityId == QStringLiteral("browser_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.tuning_stability_document");
            return alignmentBoost;
        }
        if (mediumPriority
            && (capabilityId == QStringLiteral("inbox_follow_up")
                || capabilityId == QStringLiteral("schedule_follow_up"))) {
            *reasonCode = QStringLiteral("proposal_rank.tuning_stability_document_defocus");
            return -defocusPenalty;
        }
    }

    if (mediumPriority && summary.contains(QStringLiteral("policy volatility: elevated"))) {
        if (capabilityId == QStringLiteral("inbox_follow_up")
            || capabilityId == QStringLiteral("schedule_follow_up")
            || capabilityId == QStringLiteral("document_follow_up")) {
            *reasonCode = QStringLiteral("proposal_rank.tuning_volatility_medium_penalty");
            return -volatilityPenalty;
        }
    }

    return 0.0;
}

} // namespace SuggestionProposalPolicyScoring

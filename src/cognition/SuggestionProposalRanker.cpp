#include "cognition/SuggestionProposalRanker.h"

#include <algorithm>

namespace {
QString desktopTaskId(const QVariantMap &desktopContext)
{
    return desktopContext.value(QStringLiteral("taskId")).toString().trimmed();
}

QString desktopThreadId(const QVariantMap &desktopContext)
{
    return desktopContext.value(QStringLiteral("threadId")).toString().trimmed();
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

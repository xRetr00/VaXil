#include "cognition/ProactiveSuggestionGate.h"

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

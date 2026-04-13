#include "cognition/CooldownEngine.h"

namespace {
bool isMeaningfulThreadShift(const CooldownState &state, const CompanionContextSnapshot &context)
{
    return !context.threadId.isEmpty() && state.threadId != context.threadId.value;
}

bool priorityAtLeastHigh(const QString &priority)
{
    const QString normalized = priority.trimmed().toLower();
    return normalized == QStringLiteral("high") || normalized == QStringLiteral("critical");
}
}

BehaviorDecision CooldownEngine::evaluate(const Input &input) const
{
    BehaviorDecision decision;
    decision.action = QStringLiteral("suppress");
    decision.reasonCode = QStringLiteral("cooldown.active");

    if (input.focusMode.enabled && input.priority.trimmed().toLower() != QStringLiteral("critical")) {
        decision.reasonCode = QStringLiteral("focus_mode.suppressed");
        return decision;
    }

    if (input.focusMode.enabled
        && input.priority.trimmed().toLower() == QStringLiteral("critical")
        && !input.focusMode.allowCriticalAlerts) {
        decision.reasonCode = QStringLiteral("focus_mode.critical_blocked");
        return decision;
    }

    if (isMeaningfulThreadShift(input.state, input.context)) {
        decision.allowed = true;
        decision.action = QStringLiteral("allow");
        decision.reasonCode = QStringLiteral("cooldown.thread_shift");
        decision.score = 1.0;
        return decision;
    }

    if (!input.state.isActive(input.nowMs)) {
        decision.allowed = input.confidence >= 0.55;
        decision.action = decision.allowed ? QStringLiteral("allow") : QStringLiteral("defer");
        decision.reasonCode = decision.allowed ? QStringLiteral("cooldown.clear") : QStringLiteral("confidence.low");
        decision.score = input.confidence;
        return decision;
    }

    if (priorityAtLeastHigh(input.priority) && input.confidence >= 0.75 && input.novelty >= 0.65) {
        decision.allowed = true;
        decision.action = QStringLiteral("break_cooldown");
        decision.reasonCode = QStringLiteral("cooldown.break_high_novelty");
        decision.score = (input.confidence + input.novelty) / 2.0;
        return decision;
    }

    decision.allowed = false;
    decision.reasonCode = input.novelty < 0.65
        ? QStringLiteral("cooldown.low_novelty")
        : QStringLiteral("cooldown.active");
    decision.score = (input.confidence + input.novelty) / 2.0;
    return decision;
}

CooldownState CooldownEngine::advanceState(const Input &input, const BehaviorDecision &decision) const
{
    CooldownState next = input.state;
    next.threadId = input.context.threadId.value;
    next.lastTopic = input.context.topic;
    next.lastReasonCode = decision.reasonCode;

    if (decision.allowed) {
        next.suppressedCount = 0;
        next.activeUntilEpochMs = input.nowMs + 120000;
        return next;
    }

    next.suppressedCount += 1;
    if (!next.isActive(input.nowMs)) {
        next.activeUntilEpochMs = input.nowMs + 60000;
    }
    return next;
}

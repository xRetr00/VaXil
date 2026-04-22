#include "core/tools/AgentToolLoopGuard.h"

#include <algorithm>

#include "core/tools/ToolResultEvidencePolicy.h"

namespace {
bool resultFailedOrLowSignal(const AgentToolResult &result)
{
    return !result.success || ToolResultEvidencePolicy::assess(result).lowSignal;
}

QString stopMessageForReason(const QString &reasonCode)
{
    if (reasonCode == QStringLiteral("tool_loop.same_family_repeated_low_signal")) {
        return QStringLiteral("The tools are not adding useful new evidence. Please narrow the request or point me at the exact source to inspect.");
    }
    if (reasonCode == QStringLiteral("tool_loop.max_tool_calls")) {
        return QStringLiteral("I’ve hit the technical tool-call guard for this request. Please narrow it down and try again.");
    }
    return QStringLiteral("The tool attempts are failing or returning too little evidence. Please clarify what source or action you want me to use.");
}
}

void AgentToolLoopGuard::reset(AgentToolLoopGuardState *state)
{
    if (state == nullptr) {
        return;
    }
    *state = AgentToolLoopGuardState{};
}

AgentToolLoopGuardDecision AgentToolLoopGuard::evaluateResults(
    const QList<AgentToolResult> &results,
    AgentToolLoopGuardState *state,
    const AgentToolLoopGuardConfig &config)
{
    AgentToolLoopGuardDecision decision;
    if (state == nullptr || results.isEmpty()) {
        return decision;
    }

    bool allLatestLowSignalOrFailed = true;
    int latestSameFamilyCount = 0;
    for (const AgentToolResult &result : results) {
        ++state->totalToolCalls;
        const QString family = toolFamily(result.toolName);
        latestSameFamilyCount = ++state->familyAttempts[family];
        state->sameFamilyAttemptCount = std::max(state->sameFamilyAttemptCount, latestSameFamilyCount);

        const ToolResultEvidenceAssessment assessment = ToolResultEvidencePolicy::assess(result);
        if (!result.success) {
            ++state->failedToolAttempts;
        }
        if (assessment.lowSignal) {
            ++state->lowSignalAttempts;
        }
        if (result.success && !assessment.lowSignal) {
            allLatestLowSignalOrFailed = false;
        }
    }

    decision.failedToolAttemptCount = state->failedToolAttempts;
    decision.sameFamilyAttemptCount = state->sameFamilyAttemptCount;

    if (state->totalToolCalls >= config.maxToolCallsPerTurn) {
        decision.stop = true;
        decision.reasonCode = QStringLiteral("tool_loop.max_tool_calls");
    } else if (state->failedToolAttempts >= config.maxFailedToolCallsPerTurn) {
        decision.stop = true;
        decision.reasonCode = QStringLiteral("tool_loop.failed_attempts");
    } else if (state->lowSignalAttempts >= config.maxLowSignalToolResultsPerTurn) {
        decision.stop = true;
        decision.reasonCode = QStringLiteral("tool_loop.low_signal_attempts");
    } else if (allLatestLowSignalOrFailed && latestSameFamilyCount >= config.maxSameFamilyAttemptsPerTurn) {
        decision.stop = true;
        decision.reasonCode = QStringLiteral("tool_loop.same_family_repeated_low_signal");
    }

    if (decision.stop) {
        decision.userMessage = stopMessageForReason(decision.reasonCode);
        decision.reasonCodes = {
            QStringLiteral("technical_guard.tool_loop_breaker"),
            decision.reasonCode
        };
    }
    return decision;
}

QString AgentToolLoopGuard::toolFamily(const QString &toolName)
{
    const QString normalized = toolName.trimmed().toLower();
    if (normalized.startsWith(QStringLiteral("web_"))) {
        return QStringLiteral("web");
    }
    if (normalized.startsWith(QStringLiteral("browser_"))
        || normalized == QStringLiteral("computer_open_url")) {
        return QStringLiteral("browser");
    }
    if (normalized.startsWith(QStringLiteral("file_"))
        || normalized == QStringLiteral("dir_list")
        || normalized.startsWith(QStringLiteral("log_"))
        || normalized == QStringLiteral("ai_log_read")) {
        return QStringLiteral("file");
    }
    if (normalized.startsWith(QStringLiteral("memory_"))) {
        return QStringLiteral("memory");
    }
    if (normalized.startsWith(QStringLiteral("computer_"))) {
        return QStringLiteral("computer");
    }
    const int separator = normalized.indexOf(QLatin1Char('_'));
    return separator > 0 ? normalized.left(separator) : normalized;
}

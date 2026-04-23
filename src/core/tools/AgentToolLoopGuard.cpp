#include "core/tools/AgentToolLoopGuard.h"

#include <algorithm>

#include "core/tools/ToolResultEvidencePolicy.h"

namespace {
QString stopMessageForReason(const QString &reasonCode)
{
    if (reasonCode == QStringLiteral("tool_loop.same_family_repeated_low_signal")) {
        return QStringLiteral("The tools are not adding useful new evidence. Please narrow the request or point me at the exact source to inspect.");
    }
    if (reasonCode == QStringLiteral("tool_loop.max_tool_calls")) {
        return QStringLiteral("I’ve hit the technical tool-call guard for this request. Please narrow it down and try again.");
    }
    if (reasonCode == QStringLiteral("tool_loop.evidence_sufficient")) {
        return QStringLiteral("I have enough evidence to answer, so I stopped additional tool calls.");
    }
    if (reasonCode == QStringLiteral("tool_loop.cross_family_drift")) {
        return QStringLiteral("The tools started drifting away from the request after useful evidence was found. Please clarify the next source if you want me to inspect more.");
    }
    return QStringLiteral("The tool attempts are failing or returning too little evidence. Please clarify what source or action you want me to use.");
}

QString usefulEvidenceSummary(const AgentToolResult &result)
{
    const QString summary = result.summary.trimmed();
    if (!summary.isEmpty()) {
        return summary.left(240);
    }
    const QString output = result.output.trimmed();
    if (!output.isEmpty()) {
        return output.left(240);
    }
    const QString detail = result.detail.trimmed();
    return detail.left(240);
}

bool canSatisfyEvidenceNeed(const QString &family, const QString &toolName)
{
    return family == QStringLiteral("web")
        || toolName == QStringLiteral("browser_fetch_text")
        || toolName == QStringLiteral("file_read")
        || toolName == QStringLiteral("file_search")
        || toolName == QStringLiteral("log_search")
        || toolName == QStringLiteral("log_tail")
        || toolName == QStringLiteral("ai_log_read")
        || family == QStringLiteral("memory");
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

    int latestSameFamilyCount = 0;
    for (const AgentToolResult &result : results) {
        ++state->totalToolCalls;
        const QString family = toolFamily(result.toolName);
        latestSameFamilyCount = ++state->familyAttempts[family];
        state->sameFamilyAttemptCount = std::max(state->sameFamilyAttemptCount, latestSameFamilyCount);

        const ToolResultEvidenceAssessment assessment = ToolResultEvidencePolicy::assess(result);
        const bool failedOrLowSignal = !result.success || assessment.lowSignal;
        const bool usefulEvidence = canSatisfyEvidenceNeed(family, result.toolName)
            && result.success
            && !assessment.lowSignal
            && (assessment.confidence == QStringLiteral("strong")
                || assessment.confidence == QStringLiteral("medium")
                || !usefulEvidenceSummary(result).isEmpty());
        if (state->evidenceSufficient
            && failedOrLowSignal
            && !state->lastUsefulToolFamily.isEmpty()
            && state->lastUsefulToolFamily != family
            && family != QStringLiteral("memory")) {
            state->toolDriftDetected = true;
        }
        if (!result.success) {
            ++state->failedToolAttempts;
        }
        if (assessment.lowSignal) {
            ++state->lowSignalAttempts;
        }
        if (failedOrLowSignal) {
            ++state->consecutiveFailureCount;
            if (state->lastFailureFamily == family) {
                ++state->consecutiveSameFamilyFailureCount;
            } else {
                state->consecutiveSameFamilyFailureCount = 1;
                state->lastFailureFamily = family;
            }
            state->lastToolSuccess = false;
        } else {
            state->consecutiveFailureCount = 0;
            state->consecutiveSameFamilyFailureCount = 0;
            state->lastFailureFamily.clear();
            state->lastToolSuccess = true;
            if (usefulEvidence) {
                state->evidenceSufficient = true;
                state->lastUsefulToolFamily = family;
                state->lastUsefulEvidenceSummary = usefulEvidenceSummary(result);
            }
        }
    }

    decision.failedToolAttemptCount = state->failedToolAttempts;
    decision.sameFamilyAttemptCount = state->sameFamilyAttemptCount;
    decision.consecutiveFailureCount = state->consecutiveFailureCount;
    decision.lastToolSuccess = state->lastToolSuccess;
    decision.evidenceSufficient = state->evidenceSufficient;
    decision.toolDriftDetected = state->toolDriftDetected;
    decision.lastUsefulToolFamily = state->lastUsefulToolFamily;
    decision.lastUsefulEvidenceSummary = state->lastUsefulEvidenceSummary;

    if (state->toolDriftDetected) {
        decision.stop = true;
        decision.reasonCode = QStringLiteral("tool_loop.cross_family_drift");
    } else if (state->evidenceSufficient && state->totalToolCalls > 1 && state->consecutiveFailureCount >= 2) {
        decision.stop = true;
        decision.reasonCode = QStringLiteral("tool_loop.evidence_sufficient");
    } else if (state->totalToolCalls >= config.maxToolCallsPerTurn) {
        decision.stop = true;
        decision.reasonCode = QStringLiteral("tool_loop.max_tool_calls");
    } else if (state->failedToolAttempts >= config.maxFailedToolCallsPerTurn) {
        decision.stop = true;
        decision.reasonCode = QStringLiteral("tool_loop.failed_attempts");
    } else if (state->lowSignalAttempts >= config.maxLowSignalToolResultsPerTurn) {
        decision.stop = true;
        decision.reasonCode = QStringLiteral("tool_loop.low_signal_attempts");
    } else if (state->consecutiveSameFamilyFailureCount >= config.maxSameFamilyAttemptsPerTurn) {
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

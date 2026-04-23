#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include "core/AssistantTypes.h"

struct AgentToolLoopGuardConfig
{
    int maxToolCallsPerTurn = 12;
    int maxFailedToolCallsPerTurn = 3;
    int maxLowSignalToolResultsPerTurn = 3;
    int maxSameFamilyAttemptsPerTurn = 2;
};

struct AgentToolLoopGuardState
{
    int totalToolCalls = 0;
    int failedToolAttempts = 0;
    int lowSignalAttempts = 0;
    int sameFamilyAttemptCount = 0;
    int consecutiveFailureCount = 0;
    int consecutiveSameFamilyFailureCount = 0;
    QString lastFailureFamily;
    bool lastToolSuccess = false;
    bool evidenceSufficient = false;
    bool toolDriftDetected = false;
    QString lastUsefulToolFamily;
    QString lastUsefulEvidenceSummary;
    QHash<QString, int> familyAttempts;
};

struct AgentToolLoopGuardDecision
{
    bool stop = false;
    QString reasonCode;
    QString userMessage;
    int failedToolAttemptCount = 0;
    int sameFamilyAttemptCount = 0;
    int consecutiveFailureCount = 0;
    bool lastToolSuccess = false;
    bool evidenceSufficient = false;
    bool toolDriftDetected = false;
    QString lastUsefulToolFamily;
    QString lastUsefulEvidenceSummary;
    QStringList reasonCodes;
};

class AgentToolLoopGuard
{
public:
    static void reset(AgentToolLoopGuardState *state);
    static AgentToolLoopGuardDecision evaluateResults(
        const QList<AgentToolResult> &results,
        AgentToolLoopGuardState *state,
        const AgentToolLoopGuardConfig &config = {});
    static QString toolFamily(const QString &toolName);
};

#pragma once

#include <optional>

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "companion/contracts/FocusModeState.h"
#include "core/AssistantTypes.h"

struct PromptBlock
{
    QString name;
    QString reasonCode;
    QString content;
    bool included = true;

    [[nodiscard]] int charCount() const
    {
        return content.size();
    }
};

struct PromptAssemblyReport
{
    QList<PromptBlock> includedBlocks;
    QList<PromptBlock> suppressedBlocks;
    QStringList selectedToolNames;
    int selectedMemoryCount = 0;
    int evidenceCount = 0;
    int totalPromptChars = 0;

    [[nodiscard]] QString toLogString() const
    {
        QStringList included;
        for (const PromptBlock &block : includedBlocks) {
            included.push_back(QStringLiteral("%1:%2:%3")
                                   .arg(block.name,
                                        block.reasonCode,
                                        QString::number(block.charCount())));
        }

        QStringList suppressed;
        for (const PromptBlock &block : suppressedBlocks) {
            suppressed.push_back(QStringLiteral("%1:%2").arg(block.name, block.reasonCode));
        }

        return QStringLiteral("prompt_assembly totalChars=%1 included=[%2] suppressed=[%3] tools=[%4] selectedMemory=%5 evidence=%6")
            .arg(QString::number(totalPromptChars),
                 included.join(QStringLiteral(",")),
                 suppressed.join(QStringLiteral(",")),
                 selectedToolNames.join(QStringLiteral(",")),
                 QString::number(selectedMemoryCount),
                 QString::number(evidenceCount));
    }

    [[nodiscard]] QVariantMap toVariantMap() const
    {
        QStringList included;
        for (const PromptBlock &block : includedBlocks) {
            included.push_back(QStringLiteral("%1:%2:%3")
                                   .arg(block.name,
                                        block.reasonCode,
                                        QString::number(block.charCount())));
        }

        QStringList suppressed;
        for (const PromptBlock &block : suppressedBlocks) {
            suppressed.push_back(QStringLiteral("%1:%2").arg(block.name, block.reasonCode));
        }

        return {
            {QStringLiteral("totalPromptChars"), totalPromptChars},
            {QStringLiteral("includedBlocks"), included},
            {QStringLiteral("suppressedBlocks"), suppressed},
            {QStringLiteral("selectedTools"), selectedToolNames},
            {QStringLiteral("selectedMemoryCount"), selectedMemoryCount},
            {QStringLiteral("evidenceCount"), evidenceCount}
        };
    }
};

struct PromptTurnContext
{
    QString userInput;
    AssistantIdentity identity;
    UserProfile userProfile;
    IntentType intent = IntentType::GENERAL_CHAT;
    ReasoningMode reasoningMode = ReasoningMode::Balanced;
    ResponseMode responseMode = ResponseMode::Chat;
    QString sessionGoal;
    QString nextStepHint;
    MemoryContext selectedMemory;
    QList<AgentToolSpec> allowedTools;
    QList<AgentToolResult> toolResults;
    QString workspaceRoot;
    QString desktopContext;
    QString visionContext;
    QString activeTaskState;
    QString verifiedEvidence;
    QString activeBehavioralConstraints;
    QString compactResponseContract;
    bool includeWorkspaceContext = false;
    bool includeLogContext = false;
    bool includeFewShotExamples = false;
};

struct TurnRuntimeInput
{
    QString rawUserInput;
    QString effectiveInput;
    InputRouteDecision routeDecision;
    IntentType intent = IntentType::GENERAL_CHAT;
    ActionSession actionSession;
    std::optional<ActionThread> currentActionThread;
    QString desktopContext;
    QVariantMap desktopContextSnapshot;
    MemoryContext selectedMemory;
    AssistantIdentity identity;
    UserProfile userProfile;
    QList<AgentToolSpec> availableTools;
    QList<AgentToolSpec> preselectedTools;
    QList<AgentToolResult> toolResults;
    QString workspaceRoot;
    QString visionContext;
    qint64 currentTimeMs = 0;
    FocusModeState focusMode;
    bool privateMode = false;
    ReasoningMode reasoningMode = ReasoningMode::Balanced;
    bool memoryAutoWrite = false;
};

struct TurnRuntimePlan
{
    bool continuesActionThread = false;
    ToolPlan toolPlan;
    TrustDecision trustDecision;
    QList<AgentToolSpec> selectedTools;
    MemoryContext selectedMemory;
    QString evidenceState = QStringLiteral("none");
    bool evidenceSufficient = false;
    bool toolDriftDetected = false;
    QString actionLoopState = QStringLiteral("inspect");
    PromptTurnContext promptContext;
    PromptAssemblyReport promptReport;
};

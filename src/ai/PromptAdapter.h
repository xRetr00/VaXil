#pragma once

#include <QObject>

#include "core/AssistantTypes.h"

class PromptAdapter : public QObject
{
    Q_OBJECT

public:
    explicit PromptAdapter(QObject *parent = nullptr);

    QList<AiMessage> buildConversationMessages(
        const QString &input,
        const QList<AiMessage> &history,
        const QList<MemoryRecord> &memory,
        const AssistantIdentity &identity,
        const UserProfile &userProfile,
        ReasoningMode mode) const;

    QList<AiMessage> buildCommandMessages(
        const QString &input,
        const AssistantIdentity &identity,
        const UserProfile &userProfile,
        ReasoningMode mode) const;

    QList<AiMessage> buildHybridAgentMessages(
        const QString &input,
        const QList<MemoryRecord> &memory,
        const AssistantIdentity &identity,
        const UserProfile &userProfile,
        const QString &workspaceRoot,
        IntentType intent,
        const QList<AgentToolSpec> &availableTools,
        ReasoningMode mode) const;

    QString buildAgentInstructions(
        const QList<MemoryRecord> &memory,
        const QList<SkillManifest> &skills,
        const QList<AgentToolSpec> &availableTools,
        const AssistantIdentity &identity,
        const UserProfile &userProfile,
        const QString &workspaceRoot,
        IntentType intent,
        bool memoryAutoWrite) const;

    QList<AgentToolSpec> getRelevantTools(IntentType intent, const QList<AgentToolSpec> &availableTools) const;
    QString buildAgentWorldContext(
        IntentType intent,
        const QList<AgentToolSpec> &availableTools,
        const QList<MemoryRecord> &memory,
        const QString &workspaceRoot) const;
    QString applyReasoningMode(const QString &input, ReasoningMode mode) const;

private:
    QString buildToolSchemaContext(const QList<AgentToolSpec> &tools) const;
    QString buildWorkspaceContext(const QString &workspaceRoot) const;
    QString buildLogsContext(const QString &workspaceRoot) const;
    QString buildCapabilityRulesContext(IntentType intent) const;
    QString buildFewShotExamples(IntentType intent) const;
    QString buildMemorySummary(const QList<MemoryRecord> &memory) const;
};

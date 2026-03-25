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
        ReasoningMode mode) const;

    QString buildAgentInstructions(
        const QList<MemoryRecord> &memory,
        const QList<SkillManifest> &skills,
        const AssistantIdentity &identity,
        const UserProfile &userProfile,
        bool memoryAutoWrite) const;

    QString applyReasoningMode(const QString &input, ReasoningMode mode) const;
};

#pragma once

#include <QObject>

#include "core/AssistantTypes.h"

struct IntentResult
{
    IntentType type = IntentType::GENERAL_CHAT;
    float confidence = 0.0f;
    QString spokenMessage;
    QList<AgentTask> tasks;
};

class IntentDetector : public QObject
{
    Q_OBJECT

public:
    explicit IntentDetector(QObject *parent = nullptr);

    IntentResult detect(const QString &input, const QString &workspaceRoot) const;
};

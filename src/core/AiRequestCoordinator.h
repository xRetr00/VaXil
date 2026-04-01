#pragma once

#include "core/AssistantTypes.h"

class AppSettings;
class ReasoningRouter;

enum class AgentTransportMode {
    Responses,
    ChatAdapter,
    CapabilityError
};

class AiRequestCoordinator
{
public:
    AiRequestCoordinator(AppSettings *settings, ReasoningRouter *reasoningRouter);

    QString resolveModelId(const QStringList &availableModelIds) const;
    ReasoningMode chooseReasoningMode(const QString &input) const;
    AgentTransportMode resolveAgentTransport(const AgentCapabilitySet &capabilities, const QString &modelId) const;
    QString capabilityErrorText(const AgentCapabilitySet &capabilities, const QString &modelId) const;
    QString errorGroupFor(const QString &errorText) const;

private:
    AppSettings *m_settings = nullptr;
    ReasoningRouter *m_reasoningRouter = nullptr;
};

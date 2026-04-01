#include "core/AiRequestCoordinator.h"

#include "ai/ReasoningRouter.h"
#include "settings/AppSettings.h"

AiRequestCoordinator::AiRequestCoordinator(AppSettings *settings, ReasoningRouter *reasoningRouter)
    : m_settings(settings)
    , m_reasoningRouter(reasoningRouter)
{
}

QString AiRequestCoordinator::resolveModelId(const QStringList &availableModelIds) const
{
    if (!m_settings) {
        return availableModelIds.isEmpty() ? QString{} : availableModelIds.first();
    }
    return m_settings->chatBackendModel().isEmpty() && !availableModelIds.isEmpty()
        ? availableModelIds.first()
        : m_settings->chatBackendModel();
}

ReasoningMode AiRequestCoordinator::chooseReasoningMode(const QString &input) const
{
    if (!m_settings || !m_reasoningRouter) {
        return ReasoningMode::Balanced;
    }
    return m_reasoningRouter->chooseMode(input, m_settings->autoRoutingEnabled(), m_settings->defaultReasoningMode());
}

AgentTransportMode AiRequestCoordinator::resolveAgentTransport(const AgentCapabilitySet &capabilities, const QString &) const
{
    const QString mode = m_settings ? m_settings->agentProviderMode().trimmed().toLower() : QStringLiteral("auto");
    if (mode == QStringLiteral("responses")) {
        return (capabilities.responsesApi && capabilities.selectedModelToolCapable)
            ? AgentTransportMode::Responses
            : AgentTransportMode::CapabilityError;
    }
    if (mode == QStringLiteral("chat_adapter")) {
        return AgentTransportMode::ChatAdapter;
    }
    return (capabilities.responsesApi && capabilities.selectedModelToolCapable)
        ? AgentTransportMode::Responses
        : AgentTransportMode::ChatAdapter;
}

QString AiRequestCoordinator::capabilityErrorText(const AgentCapabilitySet &capabilities, const QString &modelId) const
{
    if (!capabilities.responsesApi) {
        return QStringLiteral("The selected provider does not support the Responses API required by responses mode.");
    }
    return QStringLiteral("The selected model (%1) is not marked tool-capable for responses mode.").arg(modelId);
}

QString AiRequestCoordinator::errorGroupFor(const QString &errorText) const
{
    const QString lowered = errorText.toLower();
    if (lowered.contains(QStringLiteral("timed out"))) {
        return QStringLiteral("error_timeout");
    }
    if (lowered.contains(QStringLiteral("unauthorized")) || lowered.contains(QStringLiteral("forbidden"))) {
        return QStringLiteral("error_auth");
    }
    if (lowered.contains(QStringLiteral("responses api")) || lowered.contains(QStringLiteral("tool-capable"))) {
        return QStringLiteral("error_capability");
    }
    if (lowered.contains(QStringLiteral("invalid"))) {
        return QStringLiteral("error_invalid");
    }
    if (lowered.contains(QStringLiteral("network"))
        || lowered.contains(QStringLiteral("connection"))
        || lowered.contains(QStringLiteral("host"))
        || lowered.contains(QStringLiteral("http"))) {
        return QStringLiteral("error_transport");
    }
    return QStringLiteral("ai_offline");
}

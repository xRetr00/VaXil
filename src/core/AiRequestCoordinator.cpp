#include "core/AiRequestCoordinator.h"

#include "ai/AiBackendClient.h"
#include "ai/PromptAdapter.h"
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

quint64 AiRequestCoordinator::startConversationRequest(AiBackendClient *backendClient,
                                                       PromptAdapter *promptAdapter,
                                                       const ConversationRequestContext &context,
                                                       ReasoningMode mode) const
{
    if (backendClient == nullptr || promptAdapter == nullptr) {
        return 0;
    }

    const auto messages = promptAdapter->buildConversationMessages(
        context.input,
        context.history,
        context.memory,
        context.identity,
        context.userProfile,
        mode,
        context.visionContext);

    return backendClient->sendChatRequest(messages,
                                          context.modelId,
                                          {.mode = mode,
                                           .kind = RequestKind::Conversation,
                                           .stream = context.streaming,
                                           .temperature = context.sampling.conversationTemperature,
                                           .topP = context.sampling.conversationTopP,
                                           .providerTopK = context.sampling.providerTopK,
                                           .maxTokens = context.sampling.maxOutputTokens,
                                           .timeout = std::chrono::milliseconds(context.timeoutMs)});
}

AgentStartRequestResult AiRequestCoordinator::startAgentRequest(AiBackendClient *backendClient,
                                                                PromptAdapter *promptAdapter,
                                                                const AgentCapabilitySet &capabilities,
                                                                const AgentRequestContext &context) const
{
    AgentStartRequestResult result;
    result.transportMode = resolveAgentTransport(capabilities, context.modelId);
    if (backendClient == nullptr || promptAdapter == nullptr || result.transportMode == AgentTransportMode::CapabilityError) {
        return result;
    }

    if (result.transportMode == AgentTransportMode::Responses) {
        const AgentRequest request{
            .model = context.modelId,
            .instructions = promptAdapter->buildAgentInstructions(
                context.memory,
                context.skills,
                context.tools,
                context.identity,
                context.userProfile,
                context.workspaceRoot,
                context.intent,
                context.memoryAutoWrite,
                context.visionContext),
            .inputText = context.input,
            .previousResponseId = {},
            .tools = context.tools,
            .toolResults = {},
            .sampling = context.sampling,
            .mode = context.mode,
            .timeout = std::chrono::milliseconds(context.timeoutMs)
        };
        result.requestId = backendClient->sendAgentRequest(request);
        return result;
    }

    const auto messages = promptAdapter->buildHybridAgentMessages(
        context.input,
        context.memory,
        context.identity,
        context.userProfile,
        context.workspaceRoot,
        context.intent,
        context.tools,
        context.mode,
        context.visionContext);

    result.requestId = backendClient->sendChatRequest(messages,
                                                      context.modelId,
                                                      {.mode = context.mode,
                                                       .kind = RequestKind::AgentConversation,
                                                       .stream = false,
                                                       .temperature = context.sampling.conversationTemperature,
                                                       .topP = context.sampling.conversationTopP,
                                                       .providerTopK = context.sampling.providerTopK,
                                                       .maxTokens = context.sampling.maxOutputTokens,
                                                       .timeout = std::chrono::milliseconds(context.timeoutMs)});
    return result;
}

quint64 AiRequestCoordinator::continueAgentRequest(AiBackendClient *backendClient,
                                                   PromptAdapter *promptAdapter,
                                                   bool useResponses,
                                                   const AgentRequestContext &context) const
{
    if (backendClient == nullptr || promptAdapter == nullptr) {
        return 0;
    }

    if (useResponses) {
        const AgentRequest request{
            .model = context.modelId,
            .instructions = promptAdapter->buildAgentInstructions(
                context.memory,
                context.skills,
                context.tools,
                context.identity,
                context.userProfile,
                context.workspaceRoot,
                context.intent,
                context.memoryAutoWrite,
                context.visionContext),
            .inputText = {},
            .previousResponseId = context.previousResponseId,
            .tools = context.tools,
            .toolResults = context.toolResults,
            .sampling = context.sampling,
            .mode = context.mode,
            .timeout = std::chrono::milliseconds(context.timeoutMs)
        };
        return backendClient->sendAgentRequest(request);
    }

    const auto messages = promptAdapter->buildHybridAgentContinuationMessages(
        context.input,
        context.toolResults,
        context.memory,
        context.identity,
        context.userProfile,
        context.workspaceRoot,
        context.intent,
        context.tools,
        context.mode,
        context.visionContext);

    return backendClient->sendChatRequest(messages,
                                          context.modelId,
                                          {.mode = context.mode,
                                           .kind = RequestKind::AgentConversation,
                                           .stream = false,
                                           .temperature = context.sampling.conversationTemperature,
                                           .topP = context.sampling.conversationTopP,
                                           .providerTopK = context.sampling.providerTopK,
                                           .maxTokens = context.sampling.maxOutputTokens,
                                           .timeout = std::chrono::milliseconds(context.timeoutMs)});
}

quint64 AiRequestCoordinator::startCommandRequest(AiBackendClient *backendClient,
                                                  PromptAdapter *promptAdapter,
                                                  const CommandRequestContext &context) const
{
    if (backendClient == nullptr || promptAdapter == nullptr) {
        return 0;
    }

    return backendClient->sendChatRequest(
        promptAdapter->buildCommandMessages(
            context.input,
            context.identity,
            context.userProfile,
            ReasoningMode::Fast),
        context.modelId,
        {.mode = ReasoningMode::Fast,
         .kind = RequestKind::CommandExtraction,
         .stream = false,
         .temperature = context.temperature,
         .topP = context.topP,
         .providerTopK = context.providerTopK,
         .maxTokens = context.maxTokens,
         .timeout = std::chrono::milliseconds(context.timeoutMs)});
}

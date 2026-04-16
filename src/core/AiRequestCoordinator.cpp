#include "core/AiRequestCoordinator.h"

#include "ai/AiBackendClient.h"
#include "ai/PromptAdapter.h"
#include "ai/ReasoningRouter.h"
#include "logging/LoggingService.h"
#include "settings/AppSettings.h"

#include <QStringList>

namespace {
QString clipAuditText(QString text, int maxChars = 200000)
{
    if (text.size() > maxChars) {
        text = text.left(maxChars) + QStringLiteral("\n...[truncated]");
    }
    return text;
}

QString reasoningModeName(ReasoningMode mode)
{
    switch (mode) {
    case ReasoningMode::Fast:
        return QStringLiteral("fast");
    case ReasoningMode::Deep:
        return QStringLiteral("deep");
    case ReasoningMode::Balanced:
    default:
        return QStringLiteral("balanced");
    }
}

QString transportModeName(AgentTransportMode mode)
{
    switch (mode) {
    case AgentTransportMode::Responses:
        return QStringLiteral("responses");
    case AgentTransportMode::ChatAdapter:
        return QStringLiteral("chat_adapter");
    case AgentTransportMode::CapabilityError:
    default:
        return QStringLiteral("capability_error");
    }
}

QString formatMemoryLane(const QString &name, const QList<MemoryRecord> &records)
{
    QStringList lines;
    lines << QStringLiteral("[%1] count=%2").arg(name).arg(records.size());
    for (const MemoryRecord &record : records) {
        lines << QStringLiteral("- type=%1 key=%2 source=%3 updatedAt=%4 value=%5")
                     .arg(record.type,
                          record.key,
                          record.source,
                          record.updatedAt,
                          record.value.simplified());
    }
    return lines.join(QStringLiteral("\n"));
}

QString formatMemoryContext(const MemoryContext &memory)
{
    QStringList sections;
    sections << QStringLiteral("memory_context_summary profile=%1 active=%2 episodic=%3")
                    .arg(memory.profile.size())
                    .arg(memory.activeCommitments.size())
                    .arg(memory.episodic.size());
    sections << formatMemoryLane(QStringLiteral("profile"), memory.profile);
    sections << formatMemoryLane(QStringLiteral("active_commitments"), memory.activeCommitments);
    sections << formatMemoryLane(QStringLiteral("episodic"), memory.episodic);
    return sections.join(QStringLiteral("\n\n"));
}

QString formatMessages(const QList<AiMessage> &messages)
{
    QStringList lines;
    lines << QStringLiteral("messages=%1").arg(messages.size());
    for (int i = 0; i < messages.size(); ++i) {
        const AiMessage &message = messages.at(i);
        lines << QStringLiteral("--- message[%1] role=%2 ---").arg(i).arg(message.role);
        lines << message.content;
    }
    return lines.join(QStringLiteral("\n"));
}

QString formatToolSpecs(const QList<AgentToolSpec> &tools)
{
    QStringList lines;
    lines << QStringLiteral("tools=%1").arg(tools.size());
    for (const AgentToolSpec &tool : tools) {
        lines << QStringLiteral("--- tool name=%1 ---").arg(tool.name);
        lines << QStringLiteral("description=%1").arg(tool.description.simplified());
        lines << QStringLiteral("parameters=%1").arg(QString::fromStdString(tool.parameters.dump()));
    }
    return lines.join(QStringLiteral("\n"));
}

QString formatToolResults(const QList<AgentToolResult> &results)
{
    QStringList lines;
    lines << QStringLiteral("tool_results=%1").arg(results.size());
    for (const AgentToolResult &result : results) {
        lines << QStringLiteral("--- tool_result id=%1 name=%2 success=%3 errorKind=%4 ---")
                     .arg(result.callId,
                          result.toolName,
                          result.success ? QStringLiteral("true") : QStringLiteral("false"),
                          QString::number(static_cast<int>(result.errorKind)));
        lines << QStringLiteral("detail=%1").arg(result.detail);
        lines << QStringLiteral("output=%1").arg(result.output);
    }
    return lines.join(QStringLiteral("\n"));
}
}

AiRequestCoordinator::AiRequestCoordinator(AppSettings *settings,
                                           ReasoningRouter *reasoningRouter,
                                           LoggingService *loggingService)
    : m_settings(settings)
    , m_reasoningRouter(reasoningRouter)
    , m_loggingService(loggingService)
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
        context.responseMode,
        context.sessionGoal,
        context.nextStepHint,
        mode,
        context.visionContext);

    if (m_loggingService) {
        m_loggingService->infoFor(
            QStringLiteral("ai_prompt"),
            QStringLiteral("[conversation_request] model=%1 mode=%2 stream=%3 timeoutMs=%4 historyCount=%5 visionContextChars=%6 sessionGoal=%7 nextStepHint=%8")
                .arg(context.modelId,
                     reasoningModeName(mode),
                     context.streaming ? QStringLiteral("true") : QStringLiteral("false"),
                     QString::number(context.timeoutMs),
                     QString::number(context.history.size()),
                     QString::number(context.visionContext.size()),
                     context.sessionGoal.simplified(),
                     context.nextStepHint.simplified()));
        m_loggingService->infoFor(QStringLiteral("memory_audit"), clipAuditText(formatMemoryContext(context.memory)));
        m_loggingService->infoFor(QStringLiteral("ai_prompt"), clipAuditText(formatMessages(messages)));
    }

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
        if (m_loggingService && result.transportMode == AgentTransportMode::CapabilityError) {
            m_loggingService->warnFor(
                QStringLiteral("route_audit"),
                QStringLiteral("[agent_request] blocked by capability mode=%1 model=%2")
                    .arg(transportModeName(result.transportMode), context.modelId));
        }
        return result;
    }

    if (m_loggingService) {
        m_loggingService->infoFor(
            QStringLiteral("route_audit"),
            QStringLiteral("[agent_request] transport=%1 model=%2 intent=%3 memoryAutoWrite=%4 toolCount=%5 toolResultCount=%6")
                .arg(transportModeName(result.transportMode),
                     context.modelId,
                     QString::number(static_cast<int>(context.intent)),
                     context.memoryAutoWrite ? QStringLiteral("true") : QStringLiteral("false"),
                     QString::number(context.tools.size()),
                     QString::number(context.toolResults.size())));
        m_loggingService->infoFor(QStringLiteral("memory_audit"), clipAuditText(formatMemoryContext(context.memory)));
        m_loggingService->infoFor(QStringLiteral("tool_audit"), clipAuditText(formatToolSpecs(context.tools)));
    }

    if (result.transportMode == AgentTransportMode::Responses) {
        const QString instructions = promptAdapter->buildAgentInstructions(
            context.memory,
            context.skills,
            context.tools,
            context.identity,
            context.userProfile,
            context.workspaceRoot,
            context.intent,
            context.memoryAutoWrite,
            context.responseMode,
            context.sessionGoal,
            context.nextStepHint,
            context.visionContext);
        const AgentRequest request{
            .model = context.modelId,
            .instructions = instructions,
            .inputText = context.input,
            .previousResponseId = {},
            .tools = context.tools,
            .toolResults = {},
            .sampling = context.sampling,
            .mode = context.mode,
            .timeout = std::chrono::milliseconds(context.timeoutMs)
        };
        if (m_loggingService) {
            m_loggingService->infoFor(
                QStringLiteral("ai_prompt"),
                QStringLiteral("[agent_request.responses] model=%1 mode=%2 timeoutMs=%3 workspaceRoot=%4")
                    .arg(context.modelId,
                         reasoningModeName(context.mode),
                         QString::number(context.timeoutMs),
                         context.workspaceRoot));
            m_loggingService->infoFor(QStringLiteral("ai_prompt"), clipAuditText(QStringLiteral("--- instructions ---\n%1").arg(instructions)));
            m_loggingService->infoFor(QStringLiteral("ai_prompt"), clipAuditText(QStringLiteral("--- input_text ---\n%1").arg(context.input)));
        }
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
        context.responseMode,
        context.sessionGoal,
        context.nextStepHint,
        context.mode,
        context.visionContext);

    if (m_loggingService) {
        m_loggingService->infoFor(
            QStringLiteral("ai_prompt"),
            QStringLiteral("[agent_request.chat_adapter] model=%1 mode=%2 timeoutMs=%3")
                .arg(context.modelId,
                     reasoningModeName(context.mode),
                     QString::number(context.timeoutMs)));
        m_loggingService->infoFor(QStringLiteral("ai_prompt"), clipAuditText(formatMessages(messages)));
    }

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
        const QString instructions = promptAdapter->buildAgentInstructions(
            context.memory,
            context.skills,
            context.tools,
            context.identity,
            context.userProfile,
            context.workspaceRoot,
            context.intent,
            context.memoryAutoWrite,
            context.responseMode,
            context.sessionGoal,
            context.nextStepHint,
            context.visionContext);
        const AgentRequest request{
            .model = context.modelId,
            .instructions = instructions,
            .inputText = {},
            .previousResponseId = context.previousResponseId,
            .tools = context.tools,
            .toolResults = context.toolResults,
            .sampling = context.sampling,
            .mode = context.mode,
            .timeout = std::chrono::milliseconds(context.timeoutMs)
        };
        if (m_loggingService) {
            m_loggingService->infoFor(
                QStringLiteral("route_audit"),
                QStringLiteral("[agent_continue.responses] model=%1 previousResponseId=%2 toolResultCount=%3")
                    .arg(context.modelId, context.previousResponseId, QString::number(context.toolResults.size())));
            m_loggingService->infoFor(QStringLiteral("memory_audit"), clipAuditText(formatMemoryContext(context.memory)));
            m_loggingService->infoFor(QStringLiteral("tool_audit"), clipAuditText(formatToolResults(context.toolResults)));
            m_loggingService->infoFor(QStringLiteral("ai_prompt"), clipAuditText(QStringLiteral("--- continuation instructions ---\n%1").arg(instructions)));
        }
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
        context.responseMode,
        context.sessionGoal,
        context.nextStepHint,
        context.mode,
        context.visionContext);

    if (m_loggingService) {
        m_loggingService->infoFor(
            QStringLiteral("route_audit"),
            QStringLiteral("[agent_continue.chat_adapter] model=%1 toolResultCount=%2")
                .arg(context.modelId, QString::number(context.toolResults.size())));
        m_loggingService->infoFor(QStringLiteral("memory_audit"), clipAuditText(formatMemoryContext(context.memory)));
        m_loggingService->infoFor(QStringLiteral("tool_audit"), clipAuditText(formatToolResults(context.toolResults)));
        m_loggingService->infoFor(QStringLiteral("ai_prompt"), clipAuditText(formatMessages(messages)));
    }

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

    const QList<AiMessage> messages = promptAdapter->buildCommandMessages(
        context.input,
        context.identity,
        context.userProfile,
        context.responseMode,
        context.sessionGoal,
        ReasoningMode::Fast);

    if (m_loggingService) {
        m_loggingService->infoFor(
            QStringLiteral("ai_prompt"),
            QStringLiteral("[command_request] model=%1 timeoutMs=%2 temperature=%3")
                .arg(context.modelId)
                .arg(context.timeoutMs)
                .arg(context.temperature, 0, 'f', 3));
        m_loggingService->infoFor(QStringLiteral("ai_prompt"), clipAuditText(formatMessages(messages)));
    }

    return backendClient->sendChatRequest(
        messages,
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

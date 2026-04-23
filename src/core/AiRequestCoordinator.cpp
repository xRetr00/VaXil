#include "core/AiRequestCoordinator.h"

#include "ai/AiBackendClient.h"
#include "ai/PromptAdapter.h"
#include "ai/ReasoningRouter.h"
#include "core/AssistantRequestLifecyclePolicy.h"
#include "companion/contracts/BehaviorTraceEvent.h"
#include "logging/LoggingService.h"
#include "settings/AppSettings.h"

#include <QCryptographicHash>
#include <QProcessEnvironment>
#include <QStringList>

namespace {
QString clipAuditText(QString text, int maxChars = 200000)
{
    if (text.size() > maxChars) {
        text = text.left(maxChars) + QStringLiteral("\n...[truncated]");
    }
    return text;
}

bool debugPromptDumpEnabled()
{
    const QString value = QProcessEnvironment::systemEnvironment()
                              .value(QStringLiteral("VAXIL_DEBUG_PROMPT_DUMP"))
                              .trimmed()
                              .toLower();
    if (value.isEmpty()) {
        return true;
    }
    if (value == QStringLiteral("0")
        || value == QStringLiteral("false")
        || value == QStringLiteral("no")
        || value == QStringLiteral("off")) {
        return false;
    }
    return value == QStringLiteral("1")
        || value == QStringLiteral("true")
        || value == QStringLiteral("yes")
        || value == QStringLiteral("on");
}

QString providerKindForAudit(const AppSettings *settings)
{
    const QString kind = settings ? settings->chatBackendKind().trimmed().toLower() : QString{};
    return kind.isEmpty() ? QStringLiteral("openai_compatible_local") : kind;
}

QString providerEndpointForAudit(const AppSettings *settings)
{
    return settings ? settings->chatBackendEndpoint().trimmed() : QString{};
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

bool modelLooksToolCapable(const QString &modelId)
{
    const QString lowered = modelId.toLower();
    return lowered.contains(QStringLiteral("qwen"))
        || lowered.contains(QStringLiteral("granite"))
        || lowered.contains(QStringLiteral("llama"))
        || lowered.contains(QStringLiteral("gpt"))
        || lowered.contains(QStringLiteral("claude"))
        || lowered.contains(QStringLiteral("gemini"))
        || lowered.contains(QStringLiteral("mistral"))
        || lowered.contains(QStringLiteral("deepseek"))
        || lowered.contains(QStringLiteral("gpt-oss"))
        || lowered.contains(QStringLiteral("tool"));
}

bool isSideEffectingProviderTool(const QString &toolName)
{
    return toolName == QStringLiteral("browser_open")
        || toolName == QStringLiteral("computer_open_url")
        || toolName == QStringLiteral("computer_open_app")
        || toolName == QStringLiteral("computer_write_file")
        || toolName == QStringLiteral("computer_set_timer")
        || toolName == QStringLiteral("file_write")
        || toolName == QStringLiteral("file_patch")
        || toolName == QStringLiteral("memory_write")
        || toolName == QStringLiteral("memory_delete");
}

struct ProviderToolFilterResult
{
    QList<AgentToolSpec> tools;
    QString reasonCode = QStringLiteral("provider_tools.unchanged");
    QString compatibilityMode = QStringLiteral("full_tools");
    QStringList removedTools;
};

ProviderToolFilterResult filterToolsForProvider(const QString &providerKind,
                                                const QString &modelId,
                                                const QList<AgentToolSpec> &tools,
                                                AgentTransportMode transportMode)
{
    ProviderToolFilterResult result;
    result.tools = tools;
    if (tools.isEmpty()) {
        result.reasonCode = QStringLiteral("provider_tools.none_requested");
        result.compatibilityMode = QStringLiteral("no_tools");
        return result;
    }
    if (transportMode != AgentTransportMode::Responses) {
        result.reasonCode = QStringLiteral("provider_tools.chat_adapter_prompt_tools");
        result.compatibilityMode = QStringLiteral("chat_adapter");
        return result;
    }

    const bool openRouter = providerKind == QStringLiteral("openrouter");
    if (!openRouter) {
        return result;
    }

    if (!modelLooksToolCapable(modelId)) {
        result.tools.clear();
        for (const AgentToolSpec &tool : tools) {
            result.removedTools.push_back(tool.name);
        }
        result.reasonCode = QStringLiteral("provider_tools.model_not_tool_capable");
        result.compatibilityMode = QStringLiteral("no_tools");
        return result;
    }

    QList<AgentToolSpec> kept;
    for (const AgentToolSpec &tool : tools) {
        if (isSideEffectingProviderTool(tool.name)) {
            result.removedTools.push_back(tool.name);
            continue;
        }
        kept.push_back(tool);
    }
    if (!result.removedTools.isEmpty()) {
        result.tools = kept;
        result.reasonCode = QStringLiteral("provider_tools.openrouter_side_effecting_filtered");
        result.compatibilityMode = kept.isEmpty() ? QStringLiteral("no_tools") : QStringLiteral("safe_tools_only");
    }
    return result;
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

int promptContextSize(const std::optional<PromptTurnContext> &context)
{
    if (!context.has_value()) {
        return 0;
    }
    return context->desktopContext.size()
        + context->visionContext.size()
        + context->activeTaskState.size()
        + context->verifiedEvidence.size()
        + context->activeBehavioralConstraints.size();
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

QVariantMap promptSummaryPayload(const PromptAssemblyReport &report)
{
    QStringList blocksUsed;
    QStringList blocksSuppressed;
    QStringList suppressionReasons;
    QString hashSeed;
    for (const PromptBlock &block : report.includedBlocks) {
        blocksUsed.push_back(block.name);
        hashSeed += QStringLiteral("i:%1:%2:%3|")
                        .arg(block.name,
                             block.reasonCode,
                             QString::number(block.charCount()));
    }
    for (const PromptBlock &block : report.suppressedBlocks) {
        blocksSuppressed.push_back(block.name);
        suppressionReasons.push_back(block.reasonCode);
        hashSeed += QStringLiteral("s:%1:%2|").arg(block.name, block.reasonCode);
    }
    suppressionReasons.removeDuplicates();
    const QString promptHash = QString::fromLatin1(
        QCryptographicHash::hash(hashSeed.toUtf8(), QCryptographicHash::Sha256).toHex());

    QVariantMap payload;
    payload.insert(QStringLiteral("blocks_used"), blocksUsed);
    payload.insert(QStringLiteral("blocks_suppressed"), blocksSuppressed);
    payload.insert(QStringLiteral("prompt_size"), report.totalPromptChars);
    payload.insert(QStringLiteral("hash"), promptHash);
    payload.insert(QStringLiteral("suppression_reasons"), suppressionReasons);
    payload.insert(QStringLiteral("selected_tools_count"), report.selectedToolNames.size());
    payload.insert(QStringLiteral("selected_memory_count"), report.selectedMemoryCount);
    payload.insert(QStringLiteral("evidence_count"), report.evidenceCount);
    return payload;
}

void logPromptAssembly(LoggingService *loggingService,
                       const PromptAdapter *promptAdapter,
                       const PromptTurnContext &context,
                       const QString &turnId)
{
    if (loggingService == nullptr || promptAdapter == nullptr) {
        return;
    }

    const PromptAssemblyReport report = promptAdapter->buildPromptAssemblyReport(context);
    loggingService->infoFor(QStringLiteral("ai_prompt"),
                            QStringLiteral("[prompt_summary] %1").arg(report.toLogString()));
    loggingService->logTurnTrace(
        turnId,
        QStringLiteral("prompt_assembled"),
        QStringLiteral("prompt.summary_ready"),
        promptSummaryPayload(report));
}

void logProviderRequestStarted(LoggingService *loggingService,
                               const QString &turnId,
                               quint64 requestId,
                               const QString &providerKind,
                               const QString &providerEndpoint,
                               const QString &modelId,
                               const QString &requestKind,
                               const QString &transportMode = QString())
{
    if (loggingService == nullptr) {
        return;
    }

    QVariantMap payload{
        {QStringLiteral("provider"), providerKind},
        {QStringLiteral("endpoint"), providerEndpoint},
        {QStringLiteral("model"), modelId},
        {QStringLiteral("request_kind"), requestKind},
        {QStringLiteral("retry_count"), 0},
        {QStringLiteral("backoff_ms"), 0}
    };
    if (!transportMode.trimmed().isEmpty()) {
        payload.insert(QStringLiteral("transport_mode"), transportMode);
    }
    loggingService->logTurnTrace(
        turnId,
        QStringLiteral("provider_request_started"),
        QStringLiteral("provider.request_started"),
        payload,
        QStringLiteral("system"),
        QString::number(requestId));
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

AgentTransportMode AiRequestCoordinator::resolveAgentTransport(const AgentCapabilitySet &capabilities, const QString &modelId) const
{
    const QString mode = m_settings ? m_settings->agentProviderMode().trimmed().toLower() : QStringLiteral("auto");
    const QString providerKind = providerKindForAudit(m_settings);
    const bool providerModelLikelyToolCapable = providerKind != QStringLiteral("openrouter")
        || modelLooksToolCapable(modelId);
    if (mode == QStringLiteral("responses")) {
        return (capabilities.responsesApi && capabilities.selectedModelToolCapable && providerModelLikelyToolCapable)
            ? AgentTransportMode::Responses
            : AgentTransportMode::CapabilityError;
    }
    if (mode == QStringLiteral("chat_adapter")) {
        return AgentTransportMode::ChatAdapter;
    }
    return (capabilities.responsesApi && capabilities.selectedModelToolCapable && providerModelLikelyToolCapable)
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
    if (AssistantRequestLifecyclePolicy::isProviderRateLimitError(errorText)) {
        return QStringLiteral("error_rate_limit");
    }
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

    const auto messages = context.promptContext.has_value()
        ? promptAdapter->buildConversationMessages(*context.promptContext, context.history)
        : promptAdapter->buildConversationMessages(
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
        if (context.promptContext.has_value()) {
            logPromptAssembly(m_loggingService, promptAdapter, *context.promptContext, context.turnId);
        }
        const QString providerKind = providerKindForAudit(m_settings);
        const QString providerEndpoint = providerEndpointForAudit(m_settings);
        m_loggingService->infoFor(
            QStringLiteral("ai_prompt"),
            QStringLiteral("[conversation_request] provider=%1 endpoint=%2 model=%3 mode=%4 stream=%5 timeoutMs=%6 historyCount=%7 visionContextChars=%8 sessionGoal=%9 nextStepHint=%10 prompt_size=%11 context_size=%12 tools_count=%13")
                .arg(providerKind,
                     providerEndpoint,
                     context.modelId,
                     reasoningModeName(mode),
                     context.streaming ? QStringLiteral("true") : QStringLiteral("false"),
                     QString::number(context.timeoutMs),
                     QString::number(context.history.size()),
                     QString::number(context.visionContext.size()),
                     context.sessionGoal.simplified(),
                     context.nextStepHint.simplified(),
                     QString::number(formatMessages(messages).size()),
                     QString::number(promptContextSize(context.promptContext)),
                     QString::number(context.promptContext.has_value() ? context.promptContext->allowedTools.size() : 0)));
        m_loggingService->infoFor(QStringLiteral("memory_audit"), clipAuditText(formatMemoryContext(context.memory)));
        if (debugPromptDumpEnabled()) {
            const QString formattedMessages = formatMessages(messages);
            m_loggingService->infoFor(QStringLiteral("ai_prompt"), clipAuditText(formattedMessages));
            m_loggingService->logAiExchange(formattedMessages, QString(), QStringLiteral("conversation_prompt"), QStringLiteral("Prompt sent"));
        }
    }

    const quint64 requestId = backendClient->sendChatRequest(messages,
                                                             context.modelId,
                                                             {.mode = mode,
                                                              .kind = RequestKind::Conversation,
                                                              .stream = context.streaming,
                                                              .temperature = context.sampling.conversationTemperature,
                                                              .topP = context.sampling.conversationTopP,
                                                              .providerTopK = context.sampling.providerTopK,
                                                              .maxTokens = context.sampling.maxOutputTokens,
                                                              .timeout = std::chrono::milliseconds(context.timeoutMs)});
    if (m_loggingService) {
        logProviderRequestStarted(
            m_loggingService,
            context.turnId,
            requestId,
            providerKindForAudit(m_settings),
            providerEndpointForAudit(m_settings),
            context.modelId,
            QStringLiteral("conversation"));
    }
    return requestId;
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
            m_loggingService->logTurnTrace(
                context.turnId,
                QStringLiteral("provider_request_failed"),
                QStringLiteral("provider.capability_error"),
                {
                    {QStringLiteral("provider"), providerKindForAudit(m_settings)},
                    {QStringLiteral("endpoint"), providerEndpointForAudit(m_settings)},
                    {QStringLiteral("model"), context.modelId},
                    {QStringLiteral("request_kind"), QStringLiteral("agent")},
                    {QStringLiteral("transport_mode"), transportModeName(result.transportMode)},
                    {QStringLiteral("retry_count"), 0},
                    {QStringLiteral("backoff_ms"), 0},
                    {QStringLiteral("error_class"), QStringLiteral("capability")}
                });
        }
        return result;
    }

    AgentRequestContext compatibleContext = context;
    const ProviderToolFilterResult providerToolFilter = filterToolsForProvider(
        providerKindForAudit(m_settings),
        context.modelId,
        context.tools,
        result.transportMode);
    result.providerToolFilterReason = providerToolFilter.reasonCode;
    result.providerToolCompatibilityMode = providerToolFilter.compatibilityMode;
    result.toolsRemovedForProvider = providerToolFilter.removedTools;
    compatibleContext.tools = providerToolFilter.tools;
    result.effectiveTools = compatibleContext.tools;
    if (compatibleContext.promptContext.has_value()) {
        compatibleContext.promptContext->allowedTools = compatibleContext.tools;
    }

    if (m_loggingService) {
        const QString providerKind = providerKindForAudit(m_settings);
        const QString providerEndpoint = providerEndpointForAudit(m_settings);
        m_loggingService->infoFor(
            QStringLiteral("route_audit"),
            QStringLiteral("[agent_request] provider=%1 endpoint=%2 transport=%3 model=%4 intent=%5 memoryAutoWrite=%6 toolCount=%7 toolResultCount=%8 provider_tool_filter_reason=%9 provider_tool_compatibility_mode=%10 tools_removed_for_provider=%11")
                .arg(providerKind,
                     providerEndpoint,
                     transportModeName(result.transportMode),
                     compatibleContext.modelId,
                     QString::number(static_cast<int>(compatibleContext.intent)),
                     compatibleContext.memoryAutoWrite ? QStringLiteral("true") : QStringLiteral("false"),
                     QString::number(compatibleContext.tools.size()),
                     QString::number(compatibleContext.toolResults.size()),
                     providerToolFilter.reasonCode,
                     providerToolFilter.compatibilityMode,
                     providerToolFilter.removedTools.join(QStringLiteral(","))));
        m_loggingService->infoFor(QStringLiteral("memory_audit"), clipAuditText(formatMemoryContext(compatibleContext.memory)));
        m_loggingService->infoFor(QStringLiteral("tool_audit"), clipAuditText(formatToolSpecs(compatibleContext.tools)));
    }

    if (result.transportMode == AgentTransportMode::Responses) {
        const QString instructions = compatibleContext.promptContext.has_value()
            ? promptAdapter->buildAgentInstructions(*compatibleContext.promptContext, compatibleContext.skills, compatibleContext.memoryAutoWrite)
            : promptAdapter->buildAgentInstructions(
                compatibleContext.memory,
                compatibleContext.skills,
                compatibleContext.tools,
                compatibleContext.identity,
                compatibleContext.userProfile,
                compatibleContext.workspaceRoot,
                compatibleContext.intent,
                compatibleContext.memoryAutoWrite,
                compatibleContext.responseMode,
                compatibleContext.sessionGoal,
                compatibleContext.nextStepHint,
                compatibleContext.visionContext);
        const AgentRequest request{
            .model = compatibleContext.modelId,
            .instructions = instructions,
            .inputText = compatibleContext.input,
            .previousResponseId = {},
            .tools = compatibleContext.tools,
            .toolResults = {},
            .sampling = compatibleContext.sampling,
            .mode = compatibleContext.mode,
            .timeout = std::chrono::milliseconds(compatibleContext.timeoutMs)
        };
        if (m_loggingService) {
            if (compatibleContext.promptContext.has_value()) {
                logPromptAssembly(m_loggingService, promptAdapter, *compatibleContext.promptContext, compatibleContext.turnId);
            }
            const QString providerKind = providerKindForAudit(m_settings);
            const QString providerEndpoint = providerEndpointForAudit(m_settings);
            m_loggingService->infoFor(
                QStringLiteral("ai_prompt"),
                QStringLiteral("[agent_request.responses] provider=%1 endpoint=%2 model=%3 mode=%4 timeoutMs=%5 workspaceRoot=%6")
                    .arg(providerKind,
                         providerEndpoint,
                         compatibleContext.modelId,
                         reasoningModeName(compatibleContext.mode),
                         QString::number(compatibleContext.timeoutMs),
                         compatibleContext.workspaceRoot));
            if (debugPromptDumpEnabled()) {
                m_loggingService->infoFor(QStringLiteral("ai_prompt"), clipAuditText(QStringLiteral("--- instructions ---\n%1").arg(instructions)));
                m_loggingService->infoFor(QStringLiteral("ai_prompt"), clipAuditText(QStringLiteral("--- input_text ---\n%1").arg(compatibleContext.input)));
            }
        }
        result.requestId = backendClient->sendAgentRequest(request);
        if (m_loggingService) {
            logProviderRequestStarted(
                m_loggingService,
                compatibleContext.turnId,
                result.requestId,
                providerKindForAudit(m_settings),
                providerEndpointForAudit(m_settings),
                compatibleContext.modelId,
                QStringLiteral("agent"),
                QStringLiteral("responses"));
        }
        return result;
    }

    const auto messages = compatibleContext.promptContext.has_value()
        ? promptAdapter->buildHybridAgentMessages(*compatibleContext.promptContext)
        : promptAdapter->buildHybridAgentMessages(
            compatibleContext.input,
            compatibleContext.memory,
            compatibleContext.identity,
            compatibleContext.userProfile,
            compatibleContext.workspaceRoot,
            compatibleContext.intent,
            compatibleContext.tools,
            compatibleContext.responseMode,
            compatibleContext.sessionGoal,
            compatibleContext.nextStepHint,
            compatibleContext.mode,
            compatibleContext.visionContext);

    if (m_loggingService) {
        if (compatibleContext.promptContext.has_value()) {
            logPromptAssembly(m_loggingService, promptAdapter, *compatibleContext.promptContext, compatibleContext.turnId);
        }
        const QString providerKind = providerKindForAudit(m_settings);
        const QString providerEndpoint = providerEndpointForAudit(m_settings);
        m_loggingService->infoFor(
            QStringLiteral("ai_prompt"),
            QStringLiteral("[agent_request.chat_adapter] provider=%1 endpoint=%2 model=%3 mode=%4 timeoutMs=%5")
                .arg(providerKind,
                     providerEndpoint,
                     compatibleContext.modelId,
                     reasoningModeName(compatibleContext.mode),
                     QString::number(compatibleContext.timeoutMs)));
        if (debugPromptDumpEnabled()) {
            m_loggingService->infoFor(QStringLiteral("ai_prompt"), clipAuditText(formatMessages(messages)));
        }
    }

    result.requestId = backendClient->sendChatRequest(messages,
                                                      compatibleContext.modelId,
                                                      {.mode = compatibleContext.mode,
                                                       .kind = RequestKind::AgentConversation,
                                                       .stream = false,
                                                       .temperature = compatibleContext.sampling.conversationTemperature,
                                                       .topP = compatibleContext.sampling.conversationTopP,
                                                       .providerTopK = compatibleContext.sampling.providerTopK,
                                                       .maxTokens = compatibleContext.sampling.maxOutputTokens,
                                                       .timeout = std::chrono::milliseconds(compatibleContext.timeoutMs)});
    if (m_loggingService) {
        logProviderRequestStarted(
            m_loggingService,
            compatibleContext.turnId,
            result.requestId,
            providerKindForAudit(m_settings),
            providerEndpointForAudit(m_settings),
            compatibleContext.modelId,
            QStringLiteral("agent"),
            QStringLiteral("chat_adapter"));
    }
    return result;
}

AgentStartRequestResult AiRequestCoordinator::continueAgentRequest(AiBackendClient *backendClient,
                                                                   PromptAdapter *promptAdapter,
                                                                   bool useResponses,
                                                                   const AgentRequestContext &context) const
{
    AgentStartRequestResult result;
    result.transportMode = useResponses ? AgentTransportMode::Responses : AgentTransportMode::ChatAdapter;
    if (backendClient == nullptr || promptAdapter == nullptr) {
        return result;
    }

    AgentRequestContext compatibleContext = context;
    const AgentTransportMode transportMode = result.transportMode;
    const ProviderToolFilterResult providerToolFilter = filterToolsForProvider(
        providerKindForAudit(m_settings),
        context.modelId,
        context.tools,
        transportMode);
    result.providerToolFilterReason = providerToolFilter.reasonCode;
    result.providerToolCompatibilityMode = providerToolFilter.compatibilityMode;
    result.toolsRemovedForProvider = providerToolFilter.removedTools;
    compatibleContext.tools = providerToolFilter.tools;
    result.effectiveTools = compatibleContext.tools;
    if (compatibleContext.promptContext.has_value()) {
        compatibleContext.promptContext->allowedTools = compatibleContext.tools;
    }

    if (useResponses) {
        const QString instructions = compatibleContext.promptContext.has_value()
            ? promptAdapter->buildAgentInstructions(*compatibleContext.promptContext, compatibleContext.skills, compatibleContext.memoryAutoWrite)
            : promptAdapter->buildAgentInstructions(
                compatibleContext.memory,
                compatibleContext.skills,
                compatibleContext.tools,
                compatibleContext.identity,
                compatibleContext.userProfile,
                compatibleContext.workspaceRoot,
                compatibleContext.intent,
                compatibleContext.memoryAutoWrite,
                compatibleContext.responseMode,
                compatibleContext.sessionGoal,
                compatibleContext.nextStepHint,
                compatibleContext.visionContext);
        const AgentRequest request{
            .model = compatibleContext.modelId,
            .instructions = instructions,
            .inputText = {},
            .previousResponseId = compatibleContext.previousResponseId,
            .tools = compatibleContext.tools,
            .toolResults = compatibleContext.toolResults,
            .sampling = compatibleContext.sampling,
            .mode = compatibleContext.mode,
            .timeout = std::chrono::milliseconds(compatibleContext.timeoutMs)
        };
        if (m_loggingService) {
            if (compatibleContext.promptContext.has_value()) {
                logPromptAssembly(m_loggingService, promptAdapter, *compatibleContext.promptContext, compatibleContext.turnId);
            }
            const QString providerKind = providerKindForAudit(m_settings);
            const QString providerEndpoint = providerEndpointForAudit(m_settings);
            m_loggingService->infoFor(
                QStringLiteral("route_audit"),
                QStringLiteral("[agent_continue.responses] provider=%1 endpoint=%2 model=%3 previousResponseId=%4 toolResultCount=%5 provider_tool_filter_reason=%6 provider_tool_compatibility_mode=%7 tools_removed_for_provider=%8")
                    .arg(providerKind,
                         providerEndpoint,
                         compatibleContext.modelId,
                         compatibleContext.previousResponseId,
                         QString::number(compatibleContext.toolResults.size()),
                         providerToolFilter.reasonCode,
                         providerToolFilter.compatibilityMode,
                         providerToolFilter.removedTools.join(QStringLiteral(","))));
            m_loggingService->infoFor(QStringLiteral("memory_audit"), clipAuditText(formatMemoryContext(compatibleContext.memory)));
            m_loggingService->infoFor(QStringLiteral("tool_audit"), clipAuditText(formatToolResults(compatibleContext.toolResults)));
            if (debugPromptDumpEnabled()) {
                m_loggingService->infoFor(QStringLiteral("ai_prompt"), clipAuditText(QStringLiteral("--- continuation instructions ---\n%1").arg(instructions)));
            }
        }
        const quint64 requestId = backendClient->sendAgentRequest(request);
        if (m_loggingService) {
            logProviderRequestStarted(
                m_loggingService,
                compatibleContext.turnId,
                requestId,
                providerKindForAudit(m_settings),
                providerEndpointForAudit(m_settings),
                compatibleContext.modelId,
                QStringLiteral("agent_continue"),
                QStringLiteral("responses"));
        }
        result.requestId = requestId;
        return result;
    }

    const auto messages = compatibleContext.promptContext.has_value()
        ? promptAdapter->buildHybridAgentContinuationMessages(*compatibleContext.promptContext)
        : promptAdapter->buildHybridAgentContinuationMessages(
            compatibleContext.input,
            compatibleContext.toolResults,
            compatibleContext.memory,
            compatibleContext.identity,
            compatibleContext.userProfile,
            compatibleContext.workspaceRoot,
            compatibleContext.intent,
            compatibleContext.tools,
            compatibleContext.responseMode,
            compatibleContext.sessionGoal,
            compatibleContext.nextStepHint,
            compatibleContext.mode,
            compatibleContext.visionContext);

    if (m_loggingService) {
        if (compatibleContext.promptContext.has_value()) {
            logPromptAssembly(m_loggingService, promptAdapter, *compatibleContext.promptContext, compatibleContext.turnId);
        }
        const QString providerKind = providerKindForAudit(m_settings);
        const QString providerEndpoint = providerEndpointForAudit(m_settings);
        m_loggingService->infoFor(
            QStringLiteral("route_audit"),
            QStringLiteral("[agent_continue.chat_adapter] provider=%1 endpoint=%2 model=%3 toolResultCount=%4")
                .arg(providerKind,
                     providerEndpoint,
                     compatibleContext.modelId,
                     QString::number(compatibleContext.toolResults.size())));
        m_loggingService->infoFor(QStringLiteral("memory_audit"), clipAuditText(formatMemoryContext(compatibleContext.memory)));
        m_loggingService->infoFor(QStringLiteral("tool_audit"), clipAuditText(formatToolResults(compatibleContext.toolResults)));
        if (debugPromptDumpEnabled()) {
            m_loggingService->infoFor(QStringLiteral("ai_prompt"), clipAuditText(formatMessages(messages)));
        }
    }

    const quint64 requestId = backendClient->sendChatRequest(messages,
                                                             compatibleContext.modelId,
                                                             {.mode = compatibleContext.mode,
                                                              .kind = RequestKind::AgentConversation,
                                                              .stream = false,
                                                              .temperature = compatibleContext.sampling.conversationTemperature,
                                                              .topP = compatibleContext.sampling.conversationTopP,
                                                              .providerTopK = compatibleContext.sampling.providerTopK,
                                                              .maxTokens = compatibleContext.sampling.maxOutputTokens,
                                                              .timeout = std::chrono::milliseconds(compatibleContext.timeoutMs)});
    if (m_loggingService) {
        logProviderRequestStarted(
            m_loggingService,
            compatibleContext.turnId,
            requestId,
            providerKindForAudit(m_settings),
            providerEndpointForAudit(m_settings),
                compatibleContext.modelId,
                QStringLiteral("agent_continue"),
                QStringLiteral("chat_adapter"));
    }
    result.requestId = requestId;
    return result;
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
        const QString providerKind = providerKindForAudit(m_settings);
        const QString providerEndpoint = providerEndpointForAudit(m_settings);
        m_loggingService->infoFor(
            QStringLiteral("ai_prompt"),
            QStringLiteral("[command_request] provider=%1 endpoint=%2 model=%3 timeoutMs=%4 temperature=%5")
                .arg(providerKind)
                .arg(providerEndpoint)
                .arg(context.modelId)
                .arg(context.timeoutMs)
                .arg(context.temperature, 0, 'f', 3));
        if (debugPromptDumpEnabled()) {
            m_loggingService->infoFor(QStringLiteral("ai_prompt"), clipAuditText(formatMessages(messages)));
        }
    }

    const quint64 requestId = backendClient->sendChatRequest(
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
    if (m_loggingService) {
        logProviderRequestStarted(
            m_loggingService,
            context.turnId,
            requestId,
            providerKindForAudit(m_settings),
            providerEndpointForAudit(m_settings),
            context.modelId,
            QStringLiteral("command_extraction"));
    }
    return requestId;
}

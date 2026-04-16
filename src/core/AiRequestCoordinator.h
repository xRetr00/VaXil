#pragma once

#include "core/AssistantTypes.h"

class AppSettings;
class AiBackendClient;
class LoggingService;
class PromptAdapter;
class ReasoningRouter;

enum class AgentTransportMode {
    Responses,
    ChatAdapter,
    CapabilityError
};

struct ConversationRequestContext {
    QString modelId;
    QString input;
    QList<AiMessage> history;
    MemoryContext memory;
    AssistantIdentity identity;
    UserProfile userProfile;
    QString visionContext;
    ResponseMode responseMode = ResponseMode::Chat;
    QString sessionGoal;
    QString nextStepHint;
    SamplingProfile sampling;
    bool streaming = true;
    int timeoutMs = 12000;
};

struct AgentRequestContext {
    QString modelId;
    QString input;
    QString previousResponseId;
    IntentType intent = IntentType::GENERAL_CHAT;
    MemoryContext memory;
    QList<SkillManifest> skills;
    QList<AgentToolSpec> tools;
    QList<AgentToolResult> toolResults;
    AssistantIdentity identity;
    UserProfile userProfile;
    QString workspaceRoot;
    QString visionContext;
    ResponseMode responseMode = ResponseMode::Chat;
    QString sessionGoal;
    QString nextStepHint;
    SamplingProfile sampling;
    ReasoningMode mode = ReasoningMode::Balanced;
    bool memoryAutoWrite = false;
    int timeoutMs = 12000;
};

struct CommandRequestContext {
    QString modelId;
    QString input;
    AssistantIdentity identity;
    UserProfile userProfile;
    ResponseMode responseMode = ResponseMode::Act;
    QString sessionGoal;
    int timeoutMs = 12000;
    double temperature = 0.2;
    std::optional<double> topP;
    std::optional<int> providerTopK;
    std::optional<int> maxTokens;
};

struct AgentStartRequestResult {
    quint64 requestId = 0;
    AgentTransportMode transportMode = AgentTransportMode::ChatAdapter;
};

class AiRequestCoordinator
{
public:
    AiRequestCoordinator(AppSettings *settings,
                         ReasoningRouter *reasoningRouter,
                         LoggingService *loggingService);

    QString resolveModelId(const QStringList &availableModelIds) const;
    ReasoningMode chooseReasoningMode(const QString &input) const;
    AgentTransportMode resolveAgentTransport(const AgentCapabilitySet &capabilities, const QString &modelId) const;
    QString capabilityErrorText(const AgentCapabilitySet &capabilities, const QString &modelId) const;
    QString errorGroupFor(const QString &errorText) const;
    quint64 startConversationRequest(AiBackendClient *backendClient,
                                     PromptAdapter *promptAdapter,
                                     const ConversationRequestContext &context,
                                     ReasoningMode mode) const;
    AgentStartRequestResult startAgentRequest(AiBackendClient *backendClient,
                                              PromptAdapter *promptAdapter,
                                              const AgentCapabilitySet &capabilities,
                                              const AgentRequestContext &context) const;
    quint64 continueAgentRequest(AiBackendClient *backendClient,
                                 PromptAdapter *promptAdapter,
                                 bool useResponses,
                                 const AgentRequestContext &context) const;
    quint64 startCommandRequest(AiBackendClient *backendClient,
                                PromptAdapter *promptAdapter,
                                const CommandRequestContext &context) const;

private:
    AppSettings *m_settings = nullptr;
    ReasoningRouter *m_reasoningRouter = nullptr;
    LoggingService *m_loggingService = nullptr;
};

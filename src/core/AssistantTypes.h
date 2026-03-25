#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <QMetaType>
#include <QJsonObject>
#include <QStringList>
#include <QString>

#include <nlohmann/json.hpp>

enum class AssistantState {
    Idle,
    Listening,
    Processing,
    Speaking
};

enum class ReasoningMode {
    Fast,
    Balanced,
    Deep
};

enum class RequestKind {
    CommandExtraction,
    Conversation,
    AgentConversation
};

enum class IntentType {
    LIST_FILES,
    READ_FILE,
    WRITE_FILE,
    MEMORY_WRITE,
    GENERAL_CHAT
};

enum class TaskState {
    Pending,
    Running,
    Finished,
    Canceled,
    Expired
};

enum class LocalIntent {
    Greeting,
    SmallTalk,
    Command,
    ComplexQuery,
    Unknown
};

struct AiRequestOptions {
    ReasoningMode mode = ReasoningMode::Balanced;
    RequestKind kind = RequestKind::Conversation;
    bool stream = true;
    double temperature = 0.7;
    std::optional<double> topP;
    std::optional<int> providerTopK;
    std::optional<int> maxTokens;
    std::optional<int> seed;
    std::chrono::milliseconds timeout{12000};
};

struct ModelInfo {
    QString id;
    QString ownedBy;
};

struct AiAvailability {
    bool online = false;
    bool modelAvailable = false;
    QString status = QStringLiteral("AI unavailable");
};

struct AgentCapabilitySet {
    bool responsesApi = false;
    bool previousResponseId = false;
    bool toolCalling = false;
    bool remoteMcp = false;
    bool selectedModelToolCapable = false;
    bool agentEnabled = false;
    QString providerMode = QStringLiteral("chat_completions");
    QString status = QStringLiteral("Agent unavailable");
};

struct SamplingProfile {
    double conversationTemperature = 0.7;
    std::optional<double> conversationTopP = 0.9;
    double toolUseTemperature = 0.2;
    std::optional<int> providerTopK;
    int maxOutputTokens = 1024;
};

struct AudioLevel {
    float rms = 0.0f;
    float peak = 0.0f;
};

struct AiMessage {
    QString role;
    QString content;
};

struct AgentToolSpec {
    QString name;
    QString description;
    nlohmann::json parameters = nlohmann::json::object();
};

struct AgentTask {
    int id = 0;
    QString type;
    QJsonObject args;
    TaskState state = TaskState::Pending;
    qint64 createdAtMs = 0;
    int priority = 0;
    QString taskKey;
    int retryCount = 0;
};

struct BackgroundTaskResult {
    int taskId = 0;
    QString type;
    bool success = false;
    TaskState state = TaskState::Finished;
    QString title;
    QString summary;
    QString detail;
    QJsonObject payload;
    QString finishedAt;
    QString taskKey;
};

struct AgentToolCall {
    QString id;
    QString name;
    QString argumentsJson;
};

struct AgentToolResult {
    QString callId;
    QString toolName;
    QString output;
    bool success = false;
};

struct AgentResponse {
    QString responseId;
    QString outputText;
    QList<AgentToolCall> toolCalls;
    QString rawJson;
};

struct AgentTraceEntry {
    QString timestamp;
    QString kind;
    QString title;
    QString detail;
    bool success = true;
};

struct MemoryEntry {
    QString id;
    QString kind;
    QString title;
    QString content;
    QStringList tags;
    float confidence = 0.0f;
    bool secret = false;
    QString source;
    QString updatedAt;
};

struct SkillManifest {
    QString id;
    QString name;
    QString version;
    QString description;
    QString promptTemplatePath;
};

struct AgentRequest {
    QString model;
    QString instructions;
    QString inputText;
    QString previousResponseId;
    QList<AgentToolSpec> tools;
    QList<AgentToolResult> toolResults;
    SamplingProfile sampling;
    ReasoningMode mode = ReasoningMode::Balanced;
    std::chrono::milliseconds timeout{12000};
};

struct TranscriptionResult {
    QString text;
    float confidence = 0.0f;
};

struct CommandEnvelope {
    QString intent;
    QString target;
    QString action;
    nlohmann::json args = nlohmann::json::object();
    float confidence = 0.0f;
    bool valid = false;
};

struct MemoryRecord {
    QString type;
    QString key;
    QString value;
    float confidence = 0.0f;
    QString source;
    QString updatedAt;
};

struct AssistantIdentity {
    QString assistantName;
    QString personality;
    QString tone;
    QString addressingStyle;
};

struct UserProfile {
    QString displayName;
    QString spokenName;
    QString userName;
    nlohmann::json preferences = nlohmann::json::object();
};

struct AssistantResponse {
    QString text;
    bool isDeviceAction = false;
    std::optional<CommandEnvelope> command;
};

struct LocalResponseContext {
    QString assistantName;
    QString userName;
    QString timeOfDay;
    QString systemState;
    QString tone;
    QString addressingStyle;
    QString currentTime;
    QString currentDate;
    QString wakeWord;
};

Q_DECLARE_METATYPE(AiMessage)
Q_DECLARE_METATYPE(QList<AiMessage>)
Q_DECLARE_METATYPE(ModelInfo)
Q_DECLARE_METATYPE(QList<ModelInfo>)
Q_DECLARE_METATYPE(AiAvailability)
Q_DECLARE_METATYPE(AgentCapabilitySet)
Q_DECLARE_METATYPE(SamplingProfile)
Q_DECLARE_METATYPE(AgentToolSpec)
Q_DECLARE_METATYPE(QList<AgentToolSpec>)
Q_DECLARE_METATYPE(IntentType)
Q_DECLARE_METATYPE(TaskState)
Q_DECLARE_METATYPE(AgentTask)
Q_DECLARE_METATYPE(QList<AgentTask>)
Q_DECLARE_METATYPE(BackgroundTaskResult)
Q_DECLARE_METATYPE(QList<BackgroundTaskResult>)
Q_DECLARE_METATYPE(AgentToolCall)
Q_DECLARE_METATYPE(QList<AgentToolCall>)
Q_DECLARE_METATYPE(AgentToolResult)
Q_DECLARE_METATYPE(QList<AgentToolResult>)
Q_DECLARE_METATYPE(AgentResponse)
Q_DECLARE_METATYPE(AgentTraceEntry)
Q_DECLARE_METATYPE(QList<AgentTraceEntry>)
Q_DECLARE_METATYPE(MemoryEntry)
Q_DECLARE_METATYPE(QList<MemoryEntry>)
Q_DECLARE_METATYPE(SkillManifest)
Q_DECLARE_METATYPE(QList<SkillManifest>)
Q_DECLARE_METATYPE(AgentRequest)
Q_DECLARE_METATYPE(TranscriptionResult)
Q_DECLARE_METATYPE(AiRequestOptions)
Q_DECLARE_METATYPE(AudioLevel)

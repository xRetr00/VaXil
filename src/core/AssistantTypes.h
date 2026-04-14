#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <QMetaType>
#include <QDateTime>
#include <QJsonObject>
#include <QStringList>
#include <QString>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include "companion/contracts/ConnectorEvent.h"

enum class AssistantState {
    Idle,
    Listening,
    Processing,
    Speaking
};

enum class AssistantSurfaceState {
    Ready,
    Listening,
    Thinking,
    Speaking,
    ToolRunning,
    Error
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

enum class ToolErrorKind {
    None,
    Transport,
    Auth,
    Capability,
    Invalid,
    Timeout,
    Unknown
};

enum class MemoryType {
    Preference,
    Fact,
    Context
};

enum class LocalIntent {
    Greeting,
    SmallTalk,
    Command,
    ComplexQuery,
    Unknown
};

enum class ResponseMode {
    Chat,
    Clarify,
    Act,
    ActWithProgress,
    Recover,
    Summarize,
    Confirm
};

enum class ActionThreadState {
    None,
    Running,
    Completed,
    Failed,
    Canceled
};

enum class MemoryLane {
    Profile,
    Episodic,
    ActiveCommitment
};

enum class GestureLifecycleState {
    Idle,
    Detecting,
    Active,
    Cooldown
};

enum class GestureEventType {
    Start,
    Hold,
    End
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

struct VisionObjectDetection {
    QString className;
    double confidence = 0.0;
};

struct VisionGestureDetection {
    QString name;
    double confidence = 0.0;
};

struct VisionSnapshot {
    QString type = QStringLiteral("vision.snapshot");
    QString schemaVersion = QStringLiteral("1.0");
    QDateTime timestamp;
    QString nodeId;
    QString traceId;
    QList<VisionObjectDetection> objects;
    QList<VisionGestureDetection> gestures;
    int fingerCount = -1;
    QString summary;
};

struct GestureObservation {
    QString actionName;
    QString sourceGesture;
    double confidence = 0.0;
};

struct GestureEvent {
    GestureEventType type = GestureEventType::Start;
    GestureLifecycleState lifecycleState = GestureLifecycleState::Idle;
    QString actionName;
    QString sourceGesture;
    double confidence = 0.0;
    qint64 timestampMs = 0;
    int stableForMs = 0;
    int stableFrameCount = 0;
    QString traceId;
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

enum class InputRouteKind {
    None,
    LocalResponse,
    DeterministicTasks,
    BackgroundTasks,
    Conversation,
    AgentConversation,
    CommandExtraction,
    AgentCapabilityError
};

struct InputRouteDecision {
    InputRouteKind kind = InputRouteKind::None;
    IntentType intent = IntentType::GENERAL_CHAT;
    ReasoningMode reasoningMode = ReasoningMode::Balanced;
    bool useVisionContext = false;
    bool speak = true;
    QString message;
    QString status;
    QList<AgentTask> tasks;
};

struct BackgroundTaskResult {
    int taskId = 0;
    QString type;
    bool success = false;
    TaskState state = TaskState::Finished;
    ToolErrorKind errorKind = ToolErrorKind::Unknown;
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
    ToolErrorKind errorKind = ToolErrorKind::Unknown;
    QString summary;
    QString detail;
    QJsonObject payload;
    QString rawProviderError;
};

struct ToolExecutionRequest {
    QString toolName;
    QString callId;
    QJsonObject args;
};

struct ToolExecutionResult {
    QString toolName;
    QString callId;
    bool success = false;
    ToolErrorKind errorKind = ToolErrorKind::Unknown;
    QString summary;
    QString detail;
    QJsonObject payload;
    QString rawProviderError;
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
    MemoryType type = MemoryType::Fact;
    QString key;
    QString value;
    QDateTime createdAt;
    QDateTime expiresAt;
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

struct MemoryContext {
    QList<MemoryRecord> profile;
    QList<MemoryRecord> episodic;
    QList<MemoryRecord> activeCommitments;

    [[nodiscard]] bool isEmpty() const
    {
        return profile.isEmpty() && episodic.isEmpty() && activeCommitments.isEmpty();
    }

    [[nodiscard]] QList<MemoryRecord> promptRecords() const
    {
        QList<MemoryRecord> records = activeCommitments;
        records.append(profile);
        records.append(episodic);
        return records;
    }
};

struct ToolPlanStep {
    QString toolName;
    int affordanceScore = 0;
    int riskScore = 0;
    bool requiresGrounding = false;
    bool sideEffecting = false;
    QString reason;
};

struct ToolPlan {
    QString goal;
    QList<ToolPlanStep> candidates;
    QStringList orderedToolNames;
    bool requiresGrounding = false;
    bool sideEffecting = false;
    QString rationale;
};

struct TrustDecision {
    bool highRisk = false;
    bool requiresConfirmation = false;
    QString reason;
    QString userMessage;
};

struct ActionSession {
    QString id;
    QString userRequest;
    QString goal;
    ResponseMode responseMode = ResponseMode::Chat;
    QString preamble;
    QString progress;
    QString successSummary;
    QString failureSummary;
    QString nextStepHint;
    QStringList selectedTools;
    bool shouldAnnounceProgress = false;
    TrustDecision trust;
    ToolPlan toolPlan;
};

struct ActionThread {
    QString id;
    QString taskType;
    QString userGoal;
    QString resultSummary;
    QString artifactText;
    QJsonObject payload;
    QStringList sourceUrls;
    QString nextStepHint;
    ActionThreadState state = ActionThreadState::None;
    bool success = false;
    bool valid = false;
    qint64 updatedAtMs = 0;
    qint64 expiresAtMs = 0;

    [[nodiscard]] bool isUsable(qint64 nowMs) const
    {
        return valid && state != ActionThreadState::None && expiresAtMs > nowMs;
    }

    [[nodiscard]] bool hasArtifacts() const
    {
        return !artifactText.trimmed().isEmpty() || !payload.isEmpty() || !sourceUrls.isEmpty();
    }
};

struct AssistantIdentity {
    QString assistantName;
    QString personality;
    QString tone;
    QString addressingStyle;
};

struct UserProfile {
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
Q_DECLARE_METATYPE(VisionObjectDetection)
Q_DECLARE_METATYPE(QList<VisionObjectDetection>)
Q_DECLARE_METATYPE(VisionGestureDetection)
Q_DECLARE_METATYPE(QList<VisionGestureDetection>)
Q_DECLARE_METATYPE(VisionSnapshot)
Q_DECLARE_METATYPE(GestureLifecycleState)
Q_DECLARE_METATYPE(GestureEventType)
Q_DECLARE_METATYPE(GestureObservation)
Q_DECLARE_METATYPE(QList<GestureObservation>)
Q_DECLARE_METATYPE(GestureEvent)
Q_DECLARE_METATYPE(ModelInfo)
Q_DECLARE_METATYPE(QList<ModelInfo>)
Q_DECLARE_METATYPE(AiAvailability)
Q_DECLARE_METATYPE(AgentCapabilitySet)
Q_DECLARE_METATYPE(SamplingProfile)
Q_DECLARE_METATYPE(AgentToolSpec)
Q_DECLARE_METATYPE(QList<AgentToolSpec>)
Q_DECLARE_METATYPE(IntentType)
Q_DECLARE_METATYPE(TaskState)
Q_DECLARE_METATYPE(ToolErrorKind)
Q_DECLARE_METATYPE(MemoryType)
Q_DECLARE_METATYPE(InputRouteKind)
Q_DECLARE_METATYPE(InputRouteDecision)
Q_DECLARE_METATYPE(AgentTask)
Q_DECLARE_METATYPE(QList<AgentTask>)
Q_DECLARE_METATYPE(BackgroundTaskResult)
Q_DECLARE_METATYPE(QList<BackgroundTaskResult>)
Q_DECLARE_METATYPE(AgentToolCall)
Q_DECLARE_METATYPE(QList<AgentToolCall>)
Q_DECLARE_METATYPE(AgentToolResult)
Q_DECLARE_METATYPE(QList<AgentToolResult>)
Q_DECLARE_METATYPE(ToolExecutionRequest)
Q_DECLARE_METATYPE(ToolExecutionResult)
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

#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

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
    Conversation
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

struct AudioLevel {
    float rms = 0.0f;
    float peak = 0.0f;
};

struct AiMessage {
    QString role;
    QString content;
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
};

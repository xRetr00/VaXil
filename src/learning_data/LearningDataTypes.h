#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace LearningData {

inline const QString kSchemaVersion = QStringLiteral("1.0.0");

struct ToolCandidateScore
{
    QString toolName;
    double score = 0.0;
};

enum class WakeWordClipRole {
    Positive,
    Negative,
    HardNegative,
    Ambiguous,
    FalseAccept,
    FalseReject,
    PreRoll,
    PostRoll
};

enum class WakeWordLabelStatus {
    AutoLabeled,
    UserConfirmed,
    Assumed,
    Rejected
};

struct SessionEvent
{
    QString schemaVersion = kSchemaVersion;
    QString sessionId;
    QString startedAt;
    QString endedAt;
    QString appVersion;
    QJsonObject deviceInfo;
    bool collectionEnabled = false;
    bool audioCollectionEnabled = false;
    bool transcriptCollectionEnabled = false;
    bool toolLoggingEnabled = false;
    bool behaviorLoggingEnabled = false;
    bool memoryLoggingEnabled = false;
};

struct AudioCaptureEvent
{
    QString schemaVersion = kSchemaVersion;
    QString sessionId;
    QString turnId;
    QString eventId;
    QString timestamp;
    QString audioRole;
    QString filePath;
    qint64 durationMs = 0;
    int sampleRate = 16000;
    int channels = 1;
    QString sampleFormat = QStringLiteral("pcm_s16le");
    QString captureSource;
    bool voiceActivityDetected = false;
    bool wakeWordDetected = false;
    QString collectionReason;
    QString labelStatus = QStringLiteral("unlabeled");
    QString notes;
    QString fileHashSha256;
};

struct WakeWordEvent
{
    QString schemaVersion = kSchemaVersion;
    QString eventId;
    QString sessionId;
    QString turnId;
    QString timestamp;
    QString filePath;
    WakeWordClipRole clipRole = WakeWordClipRole::Negative;
    WakeWordLabelStatus labelStatus = WakeWordLabelStatus::Assumed;
    QString wakeEngine;
    QString keywordText;
    bool detected = false;
    double detectionScore = 0.0;
    bool detectionScoreAvailable = false;
    double threshold = 0.0;
    bool thresholdAvailable = false;
    qint64 durationMs = 0;
    int sampleRate = 16000;
    int channels = 1;
    QString captureSource;
    QString collectionReason;
    bool wasUsedToStartSession = false;
    bool cameFromFalseTrigger = false;
    bool cameFromMissedTriggerRecovery = false;
    QString notes;
    QString fileHashSha256;
};

struct AsrEvent
{
    QString schemaVersion = kSchemaVersion;
    QString sessionId;
    QString turnId;
    QString eventId;
    QString timestamp;
    QString sttEngine;
    QString sourceAudioEventId;
    QString rawTranscript;
    QString normalizedTranscript;
    QString finalTranscript;
    QString transcriptSource = QStringLiteral("raw_asr");
    QString language;
    double confidence = 0.0;
    bool wasUserEdited = false;
    bool transcriptChanged = false;
};

struct ToolDecisionEvent
{
    QString schemaVersion = kSchemaVersion;
    QString sessionId;
    QString turnId;
    QString eventId;
    QString timestamp;
    QString userInputText;
    QString inputMode = QStringLiteral("text");
    QStringList availableTools;
    QString selectedTool;
    QList<ToolCandidateScore> candidateToolsWithScores;
    QString decisionSource;
    QString expectedConfirmationLevel = QStringLiteral("none");
    bool noToolOptionConsidered = false;
    QString notes;
};

struct ToolExecutionEvent
{
    QString schemaVersion = kSchemaVersion;
    QString sessionId;
    QString turnId;
    QString eventId;
    QString timestamp;
    QString selectedTool;
    QJsonObject toolArgsRedacted;
    QString executionStartedAt;
    QString executionFinishedAt;
    qint64 latencyMs = 0;
    bool succeeded = false;
    QString failureType;
    bool retried = false;
    int retryCount = 0;
    bool userCorrectedToolChoice = false;
    QString finalOutcomeLabel = QStringLiteral("unknown");
};

struct BehaviorDecisionEvent
{
    QString schemaVersion = kSchemaVersion;
    QString sessionId;
    QString turnId;
    QString eventId;
    QString timestamp;
    QString responseMode;
    QString whySelected;
    bool interruptedUser = false;
    bool followUpAttempted = false;
    QString followUpHelpfulLabel = QStringLiteral("unknown");
    QString verbosityLevel;
    qint64 speakingDurationMs = 0;
};

struct MemoryDecisionEvent
{
    QString schemaVersion = kSchemaVersion;
    QString sessionId;
    QString turnId;
    QString eventId;
    QString timestamp;
    bool memoryCandidatePresent = false;
    QString memoryAction = QStringLiteral("none");
    QString memoryType = QStringLiteral("none");
    double confidence = 0.0;
    QString privacyRiskLevel = QStringLiteral("low");
    bool wasUserConfirmed = false;
    QString outcomeLabel = QStringLiteral("unknown");
};

struct UserFeedbackEvent
{
    QString schemaVersion = kSchemaVersion;
    QString sessionId;
    QString turnId;
    QString eventId;
    QString timestamp;
    QString feedbackType;
    QStringList linkedEventIds;
    QString freeformText;
    QString severity = QStringLiteral("normal");
};

struct LearningDataDiagnostics
{
    QString schemaVersion = kSchemaVersion;
    qint64 sessions = 0;
    qint64 audioClips = 0;
    qint64 wakeWordClips = 0;
    qint64 toolDecisions = 0;
    qint64 toolExecutions = 0;
    qint64 behaviorDecisions = 0;
    qint64 memoryDecisions = 0;
    qint64 feedbackEvents = 0;
    qint64 approximateDiskUsageBytes = 0;
};

namespace detail {

inline QString wakeWordClipRoleToString(WakeWordClipRole role)
{
    switch (role) {
    case WakeWordClipRole::Positive:
        return QStringLiteral("positive");
    case WakeWordClipRole::Negative:
        return QStringLiteral("negative");
    case WakeWordClipRole::HardNegative:
        return QStringLiteral("hard_negative");
    case WakeWordClipRole::Ambiguous:
        return QStringLiteral("ambiguous");
    case WakeWordClipRole::FalseAccept:
        return QStringLiteral("false_accept");
    case WakeWordClipRole::FalseReject:
        return QStringLiteral("false_reject");
    case WakeWordClipRole::PreRoll:
        return QStringLiteral("pre_roll");
    case WakeWordClipRole::PostRoll:
        return QStringLiteral("post_roll");
    }
    return QStringLiteral("negative");
}

inline WakeWordClipRole wakeWordClipRoleFromString(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("positive")) {
        return WakeWordClipRole::Positive;
    }
    if (normalized == QStringLiteral("hard_negative")) {
        return WakeWordClipRole::HardNegative;
    }
    if (normalized == QStringLiteral("ambiguous")) {
        return WakeWordClipRole::Ambiguous;
    }
    if (normalized == QStringLiteral("false_accept")) {
        return WakeWordClipRole::FalseAccept;
    }
    if (normalized == QStringLiteral("false_reject")) {
        return WakeWordClipRole::FalseReject;
    }
    if (normalized == QStringLiteral("pre_roll")) {
        return WakeWordClipRole::PreRoll;
    }
    if (normalized == QStringLiteral("post_roll")) {
        return WakeWordClipRole::PostRoll;
    }
    return WakeWordClipRole::Negative;
}

inline QString wakeWordLabelStatusToString(WakeWordLabelStatus status)
{
    switch (status) {
    case WakeWordLabelStatus::AutoLabeled:
        return QStringLiteral("auto_labeled");
    case WakeWordLabelStatus::UserConfirmed:
        return QStringLiteral("user_confirmed");
    case WakeWordLabelStatus::Assumed:
        return QStringLiteral("assumed");
    case WakeWordLabelStatus::Rejected:
        return QStringLiteral("rejected");
    }
    return QStringLiteral("assumed");
}

inline WakeWordLabelStatus wakeWordLabelStatusFromString(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("auto_labeled")) {
        return WakeWordLabelStatus::AutoLabeled;
    }
    if (normalized == QStringLiteral("user_confirmed")) {
        return WakeWordLabelStatus::UserConfirmed;
    }
    if (normalized == QStringLiteral("rejected")) {
        return WakeWordLabelStatus::Rejected;
    }
    return WakeWordLabelStatus::Assumed;
}

inline QString objectString(const QJsonObject &obj, const QString &key, const QString &fallback = QString())
{
    const QJsonValue value = obj.value(key);
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble());
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    return fallback;
}

inline bool objectBool(const QJsonObject &obj, const QString &key, bool fallback = false)
{
    const QJsonValue value = obj.value(key);
    return value.isBool() ? value.toBool() : fallback;
}

inline int objectInt(const QJsonObject &obj, const QString &key, int fallback = 0)
{
    const QJsonValue value = obj.value(key);
    return value.isDouble() ? value.toInt() : fallback;
}

inline qint64 objectInt64(const QJsonObject &obj, const QString &key, qint64 fallback = 0)
{
    const QJsonValue value = obj.value(key);
    return value.isDouble() ? static_cast<qint64>(value.toDouble()) : fallback;
}

inline double objectDouble(const QJsonObject &obj, const QString &key, double fallback = 0.0)
{
    const QJsonValue value = obj.value(key);
    return value.isDouble() ? value.toDouble() : fallback;
}

inline QStringList objectStringList(const QJsonObject &obj, const QString &key)
{
    QStringList out;
    const QJsonValue value = obj.value(key);
    if (!value.isArray()) {
        return out;
    }
    for (const QJsonValue &entry : value.toArray()) {
        if (entry.isString()) {
            out.push_back(entry.toString());
        }
    }
    return out;
}

inline QJsonArray toJsonArray(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values) {
        array.push_back(value);
    }
    return array;
}

inline QJsonObject toJson(const ToolCandidateScore &value)
{
    return {
        {QStringLiteral("tool_name"), value.toolName},
        {QStringLiteral("score"), value.score}
    };
}

inline ToolCandidateScore toolCandidateScoreFromJson(const QJsonObject &obj)
{
    ToolCandidateScore value;
    value.toolName = objectString(obj, QStringLiteral("tool_name"));
    value.score = objectDouble(obj, QStringLiteral("score"), 0.0);
    return value;
}

inline QJsonArray toJsonArray(const QList<ToolCandidateScore> &values)
{
    QJsonArray array;
    for (const ToolCandidateScore &value : values) {
        array.push_back(toJson(value));
    }
    return array;
}

inline QList<ToolCandidateScore> toolCandidateScoresFromJson(const QJsonValue &value)
{
    QList<ToolCandidateScore> scores;
    if (!value.isArray()) {
        return scores;
    }
    for (const QJsonValue &entry : value.toArray()) {
        if (entry.isObject()) {
            scores.push_back(toolCandidateScoreFromJson(entry.toObject()));
        }
    }
    return scores;
}

inline QString normalizedSchemaVersion(const QJsonObject &obj)
{
    const QString version = objectString(obj, QStringLiteral("schema_version"), kSchemaVersion);
    return version.trimmed().isEmpty() ? kSchemaVersion : version.trimmed();
}

} // namespace detail

inline QJsonObject toJson(const SessionEvent &event)
{
    return {
        {QStringLiteral("schema_version"), event.schemaVersion},
        {QStringLiteral("event_kind"), QStringLiteral("session")},
        {QStringLiteral("session_id"), event.sessionId},
        {QStringLiteral("started_at"), event.startedAt},
        {QStringLiteral("ended_at"), event.endedAt},
        {QStringLiteral("app_version"), event.appVersion},
        {QStringLiteral("device_info"), event.deviceInfo},
        {QStringLiteral("collection_enabled"), event.collectionEnabled},
        {QStringLiteral("audio_collection_enabled"), event.audioCollectionEnabled},
        {QStringLiteral("transcript_collection_enabled"), event.transcriptCollectionEnabled},
        {QStringLiteral("tool_logging_enabled"), event.toolLoggingEnabled},
        {QStringLiteral("behavior_logging_enabled"), event.behaviorLoggingEnabled},
        {QStringLiteral("memory_logging_enabled"), event.memoryLoggingEnabled}
    };
}

inline SessionEvent sessionEventFromJson(const QJsonObject &obj)
{
    SessionEvent event;
    event.schemaVersion = detail::normalizedSchemaVersion(obj);
    event.sessionId = detail::objectString(obj, QStringLiteral("session_id"));
    event.startedAt = detail::objectString(obj, QStringLiteral("started_at"));
    event.endedAt = detail::objectString(obj, QStringLiteral("ended_at"));
    event.appVersion = detail::objectString(obj, QStringLiteral("app_version"));
    event.deviceInfo = obj.value(QStringLiteral("device_info")).toObject();
    event.collectionEnabled = detail::objectBool(obj, QStringLiteral("collection_enabled"), false);
    event.audioCollectionEnabled = detail::objectBool(obj, QStringLiteral("audio_collection_enabled"), false);
    event.transcriptCollectionEnabled = detail::objectBool(obj, QStringLiteral("transcript_collection_enabled"), false);
    event.toolLoggingEnabled = detail::objectBool(obj, QStringLiteral("tool_logging_enabled"), false);
    event.behaviorLoggingEnabled = detail::objectBool(obj, QStringLiteral("behavior_logging_enabled"), false);
    event.memoryLoggingEnabled = detail::objectBool(obj, QStringLiteral("memory_logging_enabled"), false);
    return event;
}

inline QJsonObject toJson(const AudioCaptureEvent &event)
{
    return {
        {QStringLiteral("schema_version"), event.schemaVersion},
        {QStringLiteral("event_kind"), QStringLiteral("audio_capture")},
        {QStringLiteral("session_id"), event.sessionId},
        {QStringLiteral("turn_id"), event.turnId},
        {QStringLiteral("event_id"), event.eventId},
        {QStringLiteral("timestamp"), event.timestamp},
        {QStringLiteral("audio_role"), event.audioRole},
        {QStringLiteral("file_path"), event.filePath},
        {QStringLiteral("duration_ms"), event.durationMs},
        {QStringLiteral("sample_rate"), event.sampleRate},
        {QStringLiteral("channels"), event.channels},
        {QStringLiteral("sample_format"), event.sampleFormat},
        {QStringLiteral("capture_source"), event.captureSource},
        {QStringLiteral("voice_activity_detected"), event.voiceActivityDetected},
        {QStringLiteral("wake_word_detected"), event.wakeWordDetected},
        {QStringLiteral("collection_reason"), event.collectionReason},
        {QStringLiteral("label_status"), event.labelStatus},
        {QStringLiteral("notes"), event.notes},
        {QStringLiteral("file_hash_sha256"), event.fileHashSha256}
    };
}

inline AudioCaptureEvent audioCaptureEventFromJson(const QJsonObject &obj)
{
    AudioCaptureEvent event;
    event.schemaVersion = detail::normalizedSchemaVersion(obj);
    event.sessionId = detail::objectString(obj, QStringLiteral("session_id"));
    event.turnId = detail::objectString(obj, QStringLiteral("turn_id"));
    event.eventId = detail::objectString(obj, QStringLiteral("event_id"));
    event.timestamp = detail::objectString(obj, QStringLiteral("timestamp"));
    event.audioRole = detail::objectString(obj, QStringLiteral("audio_role"));
    event.filePath = detail::objectString(obj, QStringLiteral("file_path"));
    event.durationMs = detail::objectInt64(obj, QStringLiteral("duration_ms"), 0);
    event.sampleRate = detail::objectInt(obj, QStringLiteral("sample_rate"), 16000);
    event.channels = detail::objectInt(obj, QStringLiteral("channels"), 1);
    event.sampleFormat = detail::objectString(obj, QStringLiteral("sample_format"), QStringLiteral("pcm_s16le"));
    event.captureSource = detail::objectString(obj, QStringLiteral("capture_source"));
    event.voiceActivityDetected = detail::objectBool(obj, QStringLiteral("voice_activity_detected"), false);
    event.wakeWordDetected = detail::objectBool(obj, QStringLiteral("wake_word_detected"), false);
    event.collectionReason = detail::objectString(obj, QStringLiteral("collection_reason"));
    event.labelStatus = detail::objectString(obj, QStringLiteral("label_status"), QStringLiteral("unlabeled"));
    event.notes = detail::objectString(obj, QStringLiteral("notes"));
    event.fileHashSha256 = detail::objectString(obj, QStringLiteral("file_hash_sha256"));
    return event;
}

inline QJsonObject toJson(const WakeWordEvent &event)
{
    return {
        {QStringLiteral("schema_version"), event.schemaVersion},
        {QStringLiteral("event_kind"), QStringLiteral("wakeword")},
        {QStringLiteral("event_id"), event.eventId},
        {QStringLiteral("session_id"), event.sessionId},
        {QStringLiteral("turn_id"), event.turnId},
        {QStringLiteral("timestamp"), event.timestamp},
        {QStringLiteral("file_path"), event.filePath},
        {QStringLiteral("clip_role"), detail::wakeWordClipRoleToString(event.clipRole)},
        {QStringLiteral("label_status"), detail::wakeWordLabelStatusToString(event.labelStatus)},
        {QStringLiteral("wake_engine"), event.wakeEngine},
        {QStringLiteral("keyword_text"), event.keywordText},
        {QStringLiteral("detected"), event.detected},
        {QStringLiteral("detection_score"), event.detectionScoreAvailable ? QJsonValue(event.detectionScore) : QJsonValue()},
        {QStringLiteral("threshold"), event.thresholdAvailable ? QJsonValue(event.threshold) : QJsonValue()},
        {QStringLiteral("duration_ms"), event.durationMs},
        {QStringLiteral("sample_rate"), event.sampleRate},
        {QStringLiteral("channels"), event.channels},
        {QStringLiteral("capture_source"), event.captureSource},
        {QStringLiteral("collection_reason"), event.collectionReason},
        {QStringLiteral("was_used_to_start_session"), event.wasUsedToStartSession},
        {QStringLiteral("came_from_false_trigger"), event.cameFromFalseTrigger},
        {QStringLiteral("came_from_missed_trigger_recovery"), event.cameFromMissedTriggerRecovery},
        {QStringLiteral("notes"), event.notes},
        {QStringLiteral("file_hash_sha256"), event.fileHashSha256}
    };
}

inline WakeWordEvent wakeWordEventFromJson(const QJsonObject &obj)
{
    WakeWordEvent event;
    event.schemaVersion = detail::normalizedSchemaVersion(obj);
    event.eventId = detail::objectString(obj, QStringLiteral("event_id"));
    event.sessionId = detail::objectString(obj, QStringLiteral("session_id"));
    event.turnId = detail::objectString(obj, QStringLiteral("turn_id"));
    event.timestamp = detail::objectString(obj, QStringLiteral("timestamp"));
    event.filePath = detail::objectString(obj, QStringLiteral("file_path"));
    event.clipRole = detail::wakeWordClipRoleFromString(detail::objectString(obj, QStringLiteral("clip_role"), QStringLiteral("negative")));
    event.labelStatus = detail::wakeWordLabelStatusFromString(detail::objectString(obj, QStringLiteral("label_status"), QStringLiteral("assumed")));
    event.wakeEngine = detail::objectString(obj, QStringLiteral("wake_engine"));
    event.keywordText = detail::objectString(obj, QStringLiteral("keyword_text"));
    event.detected = detail::objectBool(obj, QStringLiteral("detected"), false);
    event.detectionScoreAvailable = obj.value(QStringLiteral("detection_score")).isDouble();
    event.detectionScore = detail::objectDouble(obj, QStringLiteral("detection_score"), 0.0);
    event.thresholdAvailable = obj.value(QStringLiteral("threshold")).isDouble();
    event.threshold = detail::objectDouble(obj, QStringLiteral("threshold"), 0.0);
    event.durationMs = detail::objectInt64(obj, QStringLiteral("duration_ms"), 0);
    event.sampleRate = detail::objectInt(obj, QStringLiteral("sample_rate"), 16000);
    event.channels = detail::objectInt(obj, QStringLiteral("channels"), 1);
    event.captureSource = detail::objectString(obj, QStringLiteral("capture_source"));
    event.collectionReason = detail::objectString(obj, QStringLiteral("collection_reason"));
    event.wasUsedToStartSession = detail::objectBool(obj, QStringLiteral("was_used_to_start_session"), false);
    event.cameFromFalseTrigger = detail::objectBool(obj, QStringLiteral("came_from_false_trigger"), false);
    event.cameFromMissedTriggerRecovery = detail::objectBool(obj, QStringLiteral("came_from_missed_trigger_recovery"), false);
    event.notes = detail::objectString(obj, QStringLiteral("notes"));
    event.fileHashSha256 = detail::objectString(obj, QStringLiteral("file_hash_sha256"));
    return event;
}

inline QJsonObject toJson(const AsrEvent &event)
{
    return {
        {QStringLiteral("schema_version"), event.schemaVersion},
        {QStringLiteral("event_kind"), QStringLiteral("asr")},
        {QStringLiteral("session_id"), event.sessionId},
        {QStringLiteral("turn_id"), event.turnId},
        {QStringLiteral("event_id"), event.eventId},
        {QStringLiteral("timestamp"), event.timestamp},
        {QStringLiteral("stt_engine"), event.sttEngine},
        {QStringLiteral("source_audio_event_id"), event.sourceAudioEventId},
        {QStringLiteral("raw_transcript"), event.rawTranscript},
        {QStringLiteral("normalized_transcript"), event.normalizedTranscript},
        {QStringLiteral("final_transcript"), event.finalTranscript},
        {QStringLiteral("transcript_source"), event.transcriptSource},
        {QStringLiteral("language"), event.language},
        {QStringLiteral("confidence"), event.confidence},
        {QStringLiteral("was_user_edited"), event.wasUserEdited},
        {QStringLiteral("transcript_changed"), event.transcriptChanged}
    };
}

inline AsrEvent asrEventFromJson(const QJsonObject &obj)
{
    AsrEvent event;
    event.schemaVersion = detail::normalizedSchemaVersion(obj);
    event.sessionId = detail::objectString(obj, QStringLiteral("session_id"));
    event.turnId = detail::objectString(obj, QStringLiteral("turn_id"));
    event.eventId = detail::objectString(obj, QStringLiteral("event_id"));
    event.timestamp = detail::objectString(obj, QStringLiteral("timestamp"));
    event.sttEngine = detail::objectString(obj, QStringLiteral("stt_engine"));
    event.sourceAudioEventId = detail::objectString(obj, QStringLiteral("source_audio_event_id"));
    event.rawTranscript = detail::objectString(obj, QStringLiteral("raw_transcript"));
    event.normalizedTranscript = detail::objectString(obj, QStringLiteral("normalized_transcript"));
    event.finalTranscript = detail::objectString(obj, QStringLiteral("final_transcript"));
    event.transcriptSource = detail::objectString(obj, QStringLiteral("transcript_source"), QStringLiteral("raw_asr"));
    event.language = detail::objectString(obj, QStringLiteral("language"));
    event.confidence = detail::objectDouble(obj, QStringLiteral("confidence"), 0.0);
    event.wasUserEdited = detail::objectBool(obj, QStringLiteral("was_user_edited"), false);
    event.transcriptChanged = detail::objectBool(obj, QStringLiteral("transcript_changed"), false);
    return event;
}

inline QJsonObject toJson(const ToolDecisionEvent &event)
{
    return {
        {QStringLiteral("schema_version"), event.schemaVersion},
        {QStringLiteral("event_kind"), QStringLiteral("tool_decision")},
        {QStringLiteral("session_id"), event.sessionId},
        {QStringLiteral("turn_id"), event.turnId},
        {QStringLiteral("event_id"), event.eventId},
        {QStringLiteral("timestamp"), event.timestamp},
        {QStringLiteral("user_input_text"), event.userInputText},
        {QStringLiteral("input_mode"), event.inputMode},
        {QStringLiteral("available_tools"), detail::toJsonArray(event.availableTools)},
        {QStringLiteral("selected_tool"), event.selectedTool},
        {QStringLiteral("candidate_tools_with_scores"), detail::toJsonArray(event.candidateToolsWithScores)},
        {QStringLiteral("decision_source"), event.decisionSource},
        {QStringLiteral("expected_confirmation_level"), event.expectedConfirmationLevel},
        {QStringLiteral("no_tool_option_considered"), event.noToolOptionConsidered},
        {QStringLiteral("notes"), event.notes}
    };
}

inline ToolDecisionEvent toolDecisionEventFromJson(const QJsonObject &obj)
{
    ToolDecisionEvent event;
    event.schemaVersion = detail::normalizedSchemaVersion(obj);
    event.sessionId = detail::objectString(obj, QStringLiteral("session_id"));
    event.turnId = detail::objectString(obj, QStringLiteral("turn_id"));
    event.eventId = detail::objectString(obj, QStringLiteral("event_id"));
    event.timestamp = detail::objectString(obj, QStringLiteral("timestamp"));
    event.userInputText = detail::objectString(obj, QStringLiteral("user_input_text"));
    event.inputMode = detail::objectString(obj, QStringLiteral("input_mode"), QStringLiteral("text"));
    event.availableTools = detail::objectStringList(obj, QStringLiteral("available_tools"));
    event.selectedTool = detail::objectString(obj, QStringLiteral("selected_tool"));
    event.candidateToolsWithScores = detail::toolCandidateScoresFromJson(obj.value(QStringLiteral("candidate_tools_with_scores")));
    event.decisionSource = detail::objectString(obj, QStringLiteral("decision_source"));
    event.expectedConfirmationLevel = detail::objectString(obj, QStringLiteral("expected_confirmation_level"), QStringLiteral("none"));
    event.noToolOptionConsidered = detail::objectBool(obj, QStringLiteral("no_tool_option_considered"), false);
    event.notes = detail::objectString(obj, QStringLiteral("notes"));
    return event;
}

inline QJsonObject toJson(const ToolExecutionEvent &event)
{
    return {
        {QStringLiteral("schema_version"), event.schemaVersion},
        {QStringLiteral("event_kind"), QStringLiteral("tool_execution")},
        {QStringLiteral("session_id"), event.sessionId},
        {QStringLiteral("turn_id"), event.turnId},
        {QStringLiteral("event_id"), event.eventId},
        {QStringLiteral("timestamp"), event.timestamp},
        {QStringLiteral("selected_tool"), event.selectedTool},
        {QStringLiteral("tool_args_redacted"), event.toolArgsRedacted},
        {QStringLiteral("execution_started_at"), event.executionStartedAt},
        {QStringLiteral("execution_finished_at"), event.executionFinishedAt},
        {QStringLiteral("latency_ms"), event.latencyMs},
        {QStringLiteral("succeeded"), event.succeeded},
        {QStringLiteral("failure_type"), event.failureType},
        {QStringLiteral("retried"), event.retried},
        {QStringLiteral("retry_count"), event.retryCount},
        {QStringLiteral("user_corrected_tool_choice"), event.userCorrectedToolChoice},
        {QStringLiteral("final_outcome_label"), event.finalOutcomeLabel}
    };
}

inline ToolExecutionEvent toolExecutionEventFromJson(const QJsonObject &obj)
{
    ToolExecutionEvent event;
    event.schemaVersion = detail::normalizedSchemaVersion(obj);
    event.sessionId = detail::objectString(obj, QStringLiteral("session_id"));
    event.turnId = detail::objectString(obj, QStringLiteral("turn_id"));
    event.eventId = detail::objectString(obj, QStringLiteral("event_id"));
    event.timestamp = detail::objectString(obj, QStringLiteral("timestamp"));
    event.selectedTool = detail::objectString(obj, QStringLiteral("selected_tool"));
    event.toolArgsRedacted = obj.value(QStringLiteral("tool_args_redacted")).toObject();
    event.executionStartedAt = detail::objectString(obj, QStringLiteral("execution_started_at"));
    event.executionFinishedAt = detail::objectString(obj, QStringLiteral("execution_finished_at"));
    event.latencyMs = detail::objectInt64(obj, QStringLiteral("latency_ms"), 0);
    event.succeeded = detail::objectBool(obj, QStringLiteral("succeeded"), false);
    event.failureType = detail::objectString(obj, QStringLiteral("failure_type"));
    event.retried = detail::objectBool(obj, QStringLiteral("retried"), false);
    event.retryCount = detail::objectInt(obj, QStringLiteral("retry_count"), 0);
    event.userCorrectedToolChoice = detail::objectBool(obj, QStringLiteral("user_corrected_tool_choice"), false);
    event.finalOutcomeLabel = detail::objectString(obj, QStringLiteral("final_outcome_label"), QStringLiteral("unknown"));
    return event;
}

inline QJsonObject toJson(const BehaviorDecisionEvent &event)
{
    return {
        {QStringLiteral("schema_version"), event.schemaVersion},
        {QStringLiteral("event_kind"), QStringLiteral("behavior_decision")},
        {QStringLiteral("session_id"), event.sessionId},
        {QStringLiteral("turn_id"), event.turnId},
        {QStringLiteral("event_id"), event.eventId},
        {QStringLiteral("timestamp"), event.timestamp},
        {QStringLiteral("response_mode"), event.responseMode},
        {QStringLiteral("why_selected"), event.whySelected},
        {QStringLiteral("interrupted_user"), event.interruptedUser},
        {QStringLiteral("follow_up_attempted"), event.followUpAttempted},
        {QStringLiteral("follow_up_helpful_label"), event.followUpHelpfulLabel},
        {QStringLiteral("verbosity_level"), event.verbosityLevel},
        {QStringLiteral("speaking_duration_ms"), event.speakingDurationMs}
    };
}

inline BehaviorDecisionEvent behaviorDecisionEventFromJson(const QJsonObject &obj)
{
    BehaviorDecisionEvent event;
    event.schemaVersion = detail::normalizedSchemaVersion(obj);
    event.sessionId = detail::objectString(obj, QStringLiteral("session_id"));
    event.turnId = detail::objectString(obj, QStringLiteral("turn_id"));
    event.eventId = detail::objectString(obj, QStringLiteral("event_id"));
    event.timestamp = detail::objectString(obj, QStringLiteral("timestamp"));
    event.responseMode = detail::objectString(obj, QStringLiteral("response_mode"));
    event.whySelected = detail::objectString(obj, QStringLiteral("why_selected"));
    event.interruptedUser = detail::objectBool(obj, QStringLiteral("interrupted_user"), false);
    event.followUpAttempted = detail::objectBool(obj, QStringLiteral("follow_up_attempted"), false);
    event.followUpHelpfulLabel = detail::objectString(obj, QStringLiteral("follow_up_helpful_label"), QStringLiteral("unknown"));
    event.verbosityLevel = detail::objectString(obj, QStringLiteral("verbosity_level"));
    event.speakingDurationMs = detail::objectInt64(obj, QStringLiteral("speaking_duration_ms"), 0);
    return event;
}

inline QJsonObject toJson(const MemoryDecisionEvent &event)
{
    return {
        {QStringLiteral("schema_version"), event.schemaVersion},
        {QStringLiteral("event_kind"), QStringLiteral("memory_decision")},
        {QStringLiteral("session_id"), event.sessionId},
        {QStringLiteral("turn_id"), event.turnId},
        {QStringLiteral("event_id"), event.eventId},
        {QStringLiteral("timestamp"), event.timestamp},
        {QStringLiteral("memory_candidate_present"), event.memoryCandidatePresent},
        {QStringLiteral("memory_action"), event.memoryAction},
        {QStringLiteral("memory_type"), event.memoryType},
        {QStringLiteral("confidence"), event.confidence},
        {QStringLiteral("privacy_risk_level"), event.privacyRiskLevel},
        {QStringLiteral("was_user_confirmed"), event.wasUserConfirmed},
        {QStringLiteral("outcome_label"), event.outcomeLabel}
    };
}

inline MemoryDecisionEvent memoryDecisionEventFromJson(const QJsonObject &obj)
{
    MemoryDecisionEvent event;
    event.schemaVersion = detail::normalizedSchemaVersion(obj);
    event.sessionId = detail::objectString(obj, QStringLiteral("session_id"));
    event.turnId = detail::objectString(obj, QStringLiteral("turn_id"));
    event.eventId = detail::objectString(obj, QStringLiteral("event_id"));
    event.timestamp = detail::objectString(obj, QStringLiteral("timestamp"));
    event.memoryCandidatePresent = detail::objectBool(obj, QStringLiteral("memory_candidate_present"), false);
    event.memoryAction = detail::objectString(obj, QStringLiteral("memory_action"), QStringLiteral("none"));
    event.memoryType = detail::objectString(obj, QStringLiteral("memory_type"), QStringLiteral("none"));
    event.confidence = detail::objectDouble(obj, QStringLiteral("confidence"), 0.0);
    event.privacyRiskLevel = detail::objectString(obj, QStringLiteral("privacy_risk_level"), QStringLiteral("low"));
    event.wasUserConfirmed = detail::objectBool(obj, QStringLiteral("was_user_confirmed"), false);
    event.outcomeLabel = detail::objectString(obj, QStringLiteral("outcome_label"), QStringLiteral("unknown"));
    return event;
}

inline QJsonObject toJson(const UserFeedbackEvent &event)
{
    return {
        {QStringLiteral("schema_version"), event.schemaVersion},
        {QStringLiteral("event_kind"), QStringLiteral("user_feedback")},
        {QStringLiteral("session_id"), event.sessionId},
        {QStringLiteral("turn_id"), event.turnId},
        {QStringLiteral("event_id"), event.eventId},
        {QStringLiteral("timestamp"), event.timestamp},
        {QStringLiteral("feedback_type"), event.feedbackType},
        {QStringLiteral("linked_event_ids"), detail::toJsonArray(event.linkedEventIds)},
        {QStringLiteral("freeform_text"), event.freeformText},
        {QStringLiteral("severity"), event.severity}
    };
}

inline UserFeedbackEvent userFeedbackEventFromJson(const QJsonObject &obj)
{
    UserFeedbackEvent event;
    event.schemaVersion = detail::normalizedSchemaVersion(obj);
    event.sessionId = detail::objectString(obj, QStringLiteral("session_id"));
    event.turnId = detail::objectString(obj, QStringLiteral("turn_id"));
    event.eventId = detail::objectString(obj, QStringLiteral("event_id"));
    event.timestamp = detail::objectString(obj, QStringLiteral("timestamp"));
    event.feedbackType = detail::objectString(obj, QStringLiteral("feedback_type"));
    event.linkedEventIds = detail::objectStringList(obj, QStringLiteral("linked_event_ids"));
    event.freeformText = detail::objectString(obj, QStringLiteral("freeform_text"));
    event.severity = detail::objectString(obj, QStringLiteral("severity"), QStringLiteral("normal"));
    return event;
}

inline QJsonObject toJson(const LearningDataDiagnostics &diag)
{
    return {
        {QStringLiteral("schema_version"), diag.schemaVersion},
        {QStringLiteral("sessions"), diag.sessions},
        {QStringLiteral("audio_clips"), diag.audioClips},
        {QStringLiteral("wakeword_clips"), diag.wakeWordClips},
        {QStringLiteral("tool_decisions"), diag.toolDecisions},
        {QStringLiteral("tool_executions"), diag.toolExecutions},
        {QStringLiteral("behavior_decisions"), diag.behaviorDecisions},
        {QStringLiteral("memory_decisions"), diag.memoryDecisions},
        {QStringLiteral("feedback_events"), diag.feedbackEvents},
        {QStringLiteral("approximate_disk_usage_bytes"), diag.approximateDiskUsageBytes}
    };
}

inline QByteArray toJsonLine(const QJsonObject &obj)
{
    QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    payload.append('\n');
    return payload;
}

inline QString toIsoUtcNow()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

} // namespace LearningData

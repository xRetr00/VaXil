#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace LearningData {

inline constexpr auto kSchemaVersion = "1.0.0";

inline QString defaultSchemaVersion()
{
    return QString::fromLatin1(kSchemaVersion);
}

struct ToolCandidateScore {
    QString toolName;
    double score = 0.0;

    [[nodiscard]] QJsonObject toJson() const
    {
        return {
            {QStringLiteral("tool_name"), toolName},
            {QStringLiteral("score"), score}
        };
    }

    [[nodiscard]] static ToolCandidateScore fromJson(const QJsonObject &json)
    {
        ToolCandidateScore value;
        value.toolName = json.value(QStringLiteral("tool_name")).toString();
        value.score = json.value(QStringLiteral("score")).toDouble();
        return value;
    }
};

struct SessionEvent {
    QString schemaVersion = defaultSchemaVersion();
    QString sessionId;
    QString startedAt;
    QString endedAt;
    QString appVersion;
    QVariantMap deviceInfo;
    bool collectionEnabled = false;
    bool audioCollectionEnabled = false;
    bool transcriptCollectionEnabled = false;
    bool toolLoggingEnabled = false;
    bool behaviorLoggingEnabled = false;
    bool memoryLoggingEnabled = false;

    [[nodiscard]] QJsonObject toJson() const
    {
        return {
            {QStringLiteral("schema_version"), schemaVersion},
            {QStringLiteral("session_id"), sessionId},
            {QStringLiteral("started_at"), startedAt},
            {QStringLiteral("ended_at"), endedAt},
            {QStringLiteral("app_version"), appVersion},
            {QStringLiteral("device_info"), QJsonObject::fromVariantMap(deviceInfo)},
            {QStringLiteral("collection_enabled"), collectionEnabled},
            {QStringLiteral("audio_collection_enabled"), audioCollectionEnabled},
            {QStringLiteral("transcript_collection_enabled"), transcriptCollectionEnabled},
            {QStringLiteral("tool_logging_enabled"), toolLoggingEnabled},
            {QStringLiteral("behavior_logging_enabled"), behaviorLoggingEnabled},
            {QStringLiteral("memory_logging_enabled"), memoryLoggingEnabled}
        };
    }

    [[nodiscard]] static SessionEvent fromJson(const QJsonObject &json)
    {
        SessionEvent value;
        value.schemaVersion = json.value(QStringLiteral("schema_version")).toString(defaultSchemaVersion());
        value.sessionId = json.value(QStringLiteral("session_id")).toString();
        value.startedAt = json.value(QStringLiteral("started_at")).toString();
        value.endedAt = json.value(QStringLiteral("ended_at")).toString();
        value.appVersion = json.value(QStringLiteral("app_version")).toString();
        value.deviceInfo = json.value(QStringLiteral("device_info")).toObject().toVariantMap();
        value.collectionEnabled = json.value(QStringLiteral("collection_enabled")).toBool(false);
        value.audioCollectionEnabled = json.value(QStringLiteral("audio_collection_enabled")).toBool(false);
        value.transcriptCollectionEnabled = json.value(QStringLiteral("transcript_collection_enabled")).toBool(false);
        value.toolLoggingEnabled = json.value(QStringLiteral("tool_logging_enabled")).toBool(false);
        value.behaviorLoggingEnabled = json.value(QStringLiteral("behavior_logging_enabled")).toBool(false);
        value.memoryLoggingEnabled = json.value(QStringLiteral("memory_logging_enabled")).toBool(false);
        return value;
    }
};

struct AudioCaptureEvent {
    QString schemaVersion = defaultSchemaVersion();
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
    QString sha256;

    [[nodiscard]] QJsonObject toJson() const
    {
        return {
            {QStringLiteral("schema_version"), schemaVersion},
            {QStringLiteral("session_id"), sessionId},
            {QStringLiteral("turn_id"), turnId},
            {QStringLiteral("event_id"), eventId},
            {QStringLiteral("timestamp"), timestamp},
            {QStringLiteral("audio_role"), audioRole},
            {QStringLiteral("file_path"), filePath},
            {QStringLiteral("duration_ms"), durationMs},
            {QStringLiteral("sample_rate"), sampleRate},
            {QStringLiteral("channels"), channels},
            {QStringLiteral("sample_format"), sampleFormat},
            {QStringLiteral("capture_source"), captureSource},
            {QStringLiteral("voice_activity_detected"), voiceActivityDetected},
            {QStringLiteral("wake_word_detected"), wakeWordDetected},
            {QStringLiteral("collection_reason"), collectionReason},
            {QStringLiteral("label_status"), labelStatus},
            {QStringLiteral("notes"), notes},
            {QStringLiteral("sha256"), sha256}
        };
    }

    [[nodiscard]] static AudioCaptureEvent fromJson(const QJsonObject &json)
    {
        AudioCaptureEvent value;
        value.schemaVersion = json.value(QStringLiteral("schema_version")).toString(defaultSchemaVersion());
        value.sessionId = json.value(QStringLiteral("session_id")).toString();
        value.turnId = json.value(QStringLiteral("turn_id")).toString();
        value.eventId = json.value(QStringLiteral("event_id")).toString();
        value.timestamp = json.value(QStringLiteral("timestamp")).toString();
        value.audioRole = json.value(QStringLiteral("audio_role")).toString();
        value.filePath = json.value(QStringLiteral("file_path")).toString();
        value.durationMs = static_cast<qint64>(json.value(QStringLiteral("duration_ms")).toDouble());
        value.sampleRate = json.value(QStringLiteral("sample_rate")).toInt(16000);
        value.channels = json.value(QStringLiteral("channels")).toInt(1);
        value.sampleFormat = json.value(QStringLiteral("sample_format")).toString(QStringLiteral("pcm_s16le"));
        value.captureSource = json.value(QStringLiteral("capture_source")).toString();
        value.voiceActivityDetected = json.value(QStringLiteral("voice_activity_detected")).toBool(false);
        value.wakeWordDetected = json.value(QStringLiteral("wake_word_detected")).toBool(false);
        value.collectionReason = json.value(QStringLiteral("collection_reason")).toString();
        value.labelStatus = json.value(QStringLiteral("label_status")).toString(QStringLiteral("unlabeled"));
        value.notes = json.value(QStringLiteral("notes")).toString();
        value.sha256 = json.value(QStringLiteral("sha256")).toString();
        return value;
    }
};

struct AsrEvent {
    QString schemaVersion = defaultSchemaVersion();
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

    [[nodiscard]] QJsonObject toJson() const
    {
        return {
            {QStringLiteral("schema_version"), schemaVersion},
            {QStringLiteral("session_id"), sessionId},
            {QStringLiteral("turn_id"), turnId},
            {QStringLiteral("event_id"), eventId},
            {QStringLiteral("timestamp"), timestamp},
            {QStringLiteral("stt_engine"), sttEngine},
            {QStringLiteral("source_audio_event_id"), sourceAudioEventId},
            {QStringLiteral("raw_transcript"), rawTranscript},
            {QStringLiteral("normalized_transcript"), normalizedTranscript},
            {QStringLiteral("final_transcript"), finalTranscript},
            {QStringLiteral("transcript_source"), transcriptSource},
            {QStringLiteral("language"), language},
            {QStringLiteral("confidence"), confidence},
            {QStringLiteral("was_user_edited"), wasUserEdited},
            {QStringLiteral("transcript_changed"), transcriptChanged}
        };
    }

    [[nodiscard]] static AsrEvent fromJson(const QJsonObject &json)
    {
        AsrEvent value;
        value.schemaVersion = json.value(QStringLiteral("schema_version")).toString(defaultSchemaVersion());
        value.sessionId = json.value(QStringLiteral("session_id")).toString();
        value.turnId = json.value(QStringLiteral("turn_id")).toString();
        value.eventId = json.value(QStringLiteral("event_id")).toString();
        value.timestamp = json.value(QStringLiteral("timestamp")).toString();
        value.sttEngine = json.value(QStringLiteral("stt_engine")).toString();
        value.sourceAudioEventId = json.value(QStringLiteral("source_audio_event_id")).toString();
        value.rawTranscript = json.value(QStringLiteral("raw_transcript")).toString();
        value.normalizedTranscript = json.value(QStringLiteral("normalized_transcript")).toString();
        value.finalTranscript = json.value(QStringLiteral("final_transcript")).toString();
        value.transcriptSource = json.value(QStringLiteral("transcript_source")).toString(QStringLiteral("raw_asr"));
        value.language = json.value(QStringLiteral("language")).toString();
        value.confidence = json.value(QStringLiteral("confidence")).toDouble(0.0);
        value.wasUserEdited = json.value(QStringLiteral("was_user_edited")).toBool(false);
        value.transcriptChanged = json.value(QStringLiteral("transcript_changed")).toBool(false);
        return value;
    }
};

struct ToolDecisionEvent {
    QString schemaVersion = defaultSchemaVersion();
    QString sessionId;
    QString turnId;
    QString eventId;
    QString timestamp;
    QString userInputText;
    QString inputMode;
    QStringList availableTools;
    QString selectedTool;
    QList<ToolCandidateScore> candidateToolsWithScores;
    QString decisionSource;
    QString expectedConfirmationLevel;
    bool noToolOptionConsidered = false;
    QString notes;

    [[nodiscard]] QJsonObject toJson() const
    {
        QJsonArray candidates;
        for (const ToolCandidateScore &candidate : candidateToolsWithScores) {
            candidates.push_back(candidate.toJson());
        }

        return {
            {QStringLiteral("schema_version"), schemaVersion},
            {QStringLiteral("session_id"), sessionId},
            {QStringLiteral("turn_id"), turnId},
            {QStringLiteral("event_id"), eventId},
            {QStringLiteral("timestamp"), timestamp},
            {QStringLiteral("user_input_text"), userInputText},
            {QStringLiteral("input_mode"), inputMode},
            {QStringLiteral("available_tools"), QJsonArray::fromStringList(availableTools)},
            {QStringLiteral("selected_tool"), selectedTool},
            {QStringLiteral("candidate_tools_with_scores"), candidates},
            {QStringLiteral("decision_source"), decisionSource},
            {QStringLiteral("expected_confirmation_level"), expectedConfirmationLevel},
            {QStringLiteral("no_tool_option_considered"), noToolOptionConsidered},
            {QStringLiteral("notes"), notes}
        };
    }

    [[nodiscard]] static ToolDecisionEvent fromJson(const QJsonObject &json)
    {
        ToolDecisionEvent value;
        value.schemaVersion = json.value(QStringLiteral("schema_version")).toString(defaultSchemaVersion());
        value.sessionId = json.value(QStringLiteral("session_id")).toString();
        value.turnId = json.value(QStringLiteral("turn_id")).toString();
        value.eventId = json.value(QStringLiteral("event_id")).toString();
        value.timestamp = json.value(QStringLiteral("timestamp")).toString();
        value.userInputText = json.value(QStringLiteral("user_input_text")).toString();
        value.inputMode = json.value(QStringLiteral("input_mode")).toString();
        for (const QJsonValue &tool : json.value(QStringLiteral("available_tools")).toArray()) {
            value.availableTools.push_back(tool.toString());
        }
        value.selectedTool = json.value(QStringLiteral("selected_tool")).toString();
        for (const QJsonValue &candidate : json.value(QStringLiteral("candidate_tools_with_scores")).toArray()) {
            value.candidateToolsWithScores.push_back(ToolCandidateScore::fromJson(candidate.toObject()));
        }
        value.decisionSource = json.value(QStringLiteral("decision_source")).toString();
        value.expectedConfirmationLevel = json.value(QStringLiteral("expected_confirmation_level")).toString();
        value.noToolOptionConsidered = json.value(QStringLiteral("no_tool_option_considered")).toBool(false);
        value.notes = json.value(QStringLiteral("notes")).toString();
        return value;
    }
};

struct ToolExecutionEvent {
    QString schemaVersion = defaultSchemaVersion();
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

    [[nodiscard]] QJsonObject toJson() const
    {
        return {
            {QStringLiteral("schema_version"), schemaVersion},
            {QStringLiteral("session_id"), sessionId},
            {QStringLiteral("turn_id"), turnId},
            {QStringLiteral("event_id"), eventId},
            {QStringLiteral("timestamp"), timestamp},
            {QStringLiteral("selected_tool"), selectedTool},
            {QStringLiteral("tool_args_redacted"), toolArgsRedacted},
            {QStringLiteral("execution_started_at"), executionStartedAt},
            {QStringLiteral("execution_finished_at"), executionFinishedAt},
            {QStringLiteral("latency_ms"), latencyMs},
            {QStringLiteral("succeeded"), succeeded},
            {QStringLiteral("failure_type"), failureType},
            {QStringLiteral("retried"), retried},
            {QStringLiteral("retry_count"), retryCount},
            {QStringLiteral("user_corrected_tool_choice"), userCorrectedToolChoice},
            {QStringLiteral("final_outcome_label"), finalOutcomeLabel}
        };
    }

    [[nodiscard]] static ToolExecutionEvent fromJson(const QJsonObject &json)
    {
        ToolExecutionEvent value;
        value.schemaVersion = json.value(QStringLiteral("schema_version")).toString(defaultSchemaVersion());
        value.sessionId = json.value(QStringLiteral("session_id")).toString();
        value.turnId = json.value(QStringLiteral("turn_id")).toString();
        value.eventId = json.value(QStringLiteral("event_id")).toString();
        value.timestamp = json.value(QStringLiteral("timestamp")).toString();
        value.selectedTool = json.value(QStringLiteral("selected_tool")).toString();
        value.toolArgsRedacted = json.value(QStringLiteral("tool_args_redacted")).toObject();
        value.executionStartedAt = json.value(QStringLiteral("execution_started_at")).toString();
        value.executionFinishedAt = json.value(QStringLiteral("execution_finished_at")).toString();
        value.latencyMs = static_cast<qint64>(json.value(QStringLiteral("latency_ms")).toDouble());
        value.succeeded = json.value(QStringLiteral("succeeded")).toBool(false);
        value.failureType = json.value(QStringLiteral("failure_type")).toString();
        value.retried = json.value(QStringLiteral("retried")).toBool(false);
        value.retryCount = json.value(QStringLiteral("retry_count")).toInt(0);
        value.userCorrectedToolChoice = json.value(QStringLiteral("user_corrected_tool_choice")).toBool(false);
        value.finalOutcomeLabel = json.value(QStringLiteral("final_outcome_label")).toString(QStringLiteral("unknown"));
        return value;
    }
};

struct BehaviorDecisionEvent {
    QString schemaVersion = defaultSchemaVersion();
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

    [[nodiscard]] QJsonObject toJson() const
    {
        return {
            {QStringLiteral("schema_version"), schemaVersion},
            {QStringLiteral("session_id"), sessionId},
            {QStringLiteral("turn_id"), turnId},
            {QStringLiteral("event_id"), eventId},
            {QStringLiteral("timestamp"), timestamp},
            {QStringLiteral("response_mode"), responseMode},
            {QStringLiteral("why_selected"), whySelected},
            {QStringLiteral("interrupted_user"), interruptedUser},
            {QStringLiteral("follow_up_attempted"), followUpAttempted},
            {QStringLiteral("follow_up_helpful_label"), followUpHelpfulLabel},
            {QStringLiteral("verbosity_level"), verbosityLevel},
            {QStringLiteral("speaking_duration_ms"), speakingDurationMs}
        };
    }

    [[nodiscard]] static BehaviorDecisionEvent fromJson(const QJsonObject &json)
    {
        BehaviorDecisionEvent value;
        value.schemaVersion = json.value(QStringLiteral("schema_version")).toString(defaultSchemaVersion());
        value.sessionId = json.value(QStringLiteral("session_id")).toString();
        value.turnId = json.value(QStringLiteral("turn_id")).toString();
        value.eventId = json.value(QStringLiteral("event_id")).toString();
        value.timestamp = json.value(QStringLiteral("timestamp")).toString();
        value.responseMode = json.value(QStringLiteral("response_mode")).toString();
        value.whySelected = json.value(QStringLiteral("why_selected")).toString();
        value.interruptedUser = json.value(QStringLiteral("interrupted_user")).toBool(false);
        value.followUpAttempted = json.value(QStringLiteral("follow_up_attempted")).toBool(false);
        value.followUpHelpfulLabel = json.value(QStringLiteral("follow_up_helpful_label")).toString(QStringLiteral("unknown"));
        value.verbosityLevel = json.value(QStringLiteral("verbosity_level")).toString();
        value.speakingDurationMs = static_cast<qint64>(json.value(QStringLiteral("speaking_duration_ms")).toDouble(0));
        return value;
    }
};

struct MemoryDecisionEvent {
    QString schemaVersion = defaultSchemaVersion();
    QString sessionId;
    QString turnId;
    QString eventId;
    QString timestamp;
    bool memoryCandidatePresent = false;
    QString memoryAction = QStringLiteral("none");
    QString memoryType;
    double confidence = 0.0;
    QString privacyRiskLevel = QStringLiteral("low");
    bool wasUserConfirmed = false;
    QString outcomeLabel = QStringLiteral("unknown");

    [[nodiscard]] QJsonObject toJson() const
    {
        return {
            {QStringLiteral("schema_version"), schemaVersion},
            {QStringLiteral("session_id"), sessionId},
            {QStringLiteral("turn_id"), turnId},
            {QStringLiteral("event_id"), eventId},
            {QStringLiteral("timestamp"), timestamp},
            {QStringLiteral("memory_candidate_present"), memoryCandidatePresent},
            {QStringLiteral("memory_action"), memoryAction},
            {QStringLiteral("memory_type"), memoryType},
            {QStringLiteral("confidence"), confidence},
            {QStringLiteral("privacy_risk_level"), privacyRiskLevel},
            {QStringLiteral("was_user_confirmed"), wasUserConfirmed},
            {QStringLiteral("outcome_label"), outcomeLabel}
        };
    }

    [[nodiscard]] static MemoryDecisionEvent fromJson(const QJsonObject &json)
    {
        MemoryDecisionEvent value;
        value.schemaVersion = json.value(QStringLiteral("schema_version")).toString(defaultSchemaVersion());
        value.sessionId = json.value(QStringLiteral("session_id")).toString();
        value.turnId = json.value(QStringLiteral("turn_id")).toString();
        value.eventId = json.value(QStringLiteral("event_id")).toString();
        value.timestamp = json.value(QStringLiteral("timestamp")).toString();
        value.memoryCandidatePresent = json.value(QStringLiteral("memory_candidate_present")).toBool(false);
        value.memoryAction = json.value(QStringLiteral("memory_action")).toString(QStringLiteral("none"));
        value.memoryType = json.value(QStringLiteral("memory_type")).toString();
        value.confidence = json.value(QStringLiteral("confidence")).toDouble(0.0);
        value.privacyRiskLevel = json.value(QStringLiteral("privacy_risk_level")).toString(QStringLiteral("low"));
        value.wasUserConfirmed = json.value(QStringLiteral("was_user_confirmed")).toBool(false);
        value.outcomeLabel = json.value(QStringLiteral("outcome_label")).toString(QStringLiteral("unknown"));
        return value;
    }
};

struct UserFeedbackEvent {
    QString schemaVersion = defaultSchemaVersion();
    QString sessionId;
    QString turnId;
    QString eventId;
    QString timestamp;
    QString feedbackType;
    QStringList linkedEventIds;
    QString freeformText;
    QString severity = QStringLiteral("normal");

    [[nodiscard]] QJsonObject toJson() const
    {
        return {
            {QStringLiteral("schema_version"), schemaVersion},
            {QStringLiteral("session_id"), sessionId},
            {QStringLiteral("turn_id"), turnId},
            {QStringLiteral("event_id"), eventId},
            {QStringLiteral("timestamp"), timestamp},
            {QStringLiteral("feedback_type"), feedbackType},
            {QStringLiteral("linked_event_ids"), QJsonArray::fromStringList(linkedEventIds)},
            {QStringLiteral("freeform_text"), freeformText},
            {QStringLiteral("severity"), severity}
        };
    }

    [[nodiscard]] static UserFeedbackEvent fromJson(const QJsonObject &json)
    {
        UserFeedbackEvent value;
        value.schemaVersion = json.value(QStringLiteral("schema_version")).toString(defaultSchemaVersion());
        value.sessionId = json.value(QStringLiteral("session_id")).toString();
        value.turnId = json.value(QStringLiteral("turn_id")).toString();
        value.eventId = json.value(QStringLiteral("event_id")).toString();
        value.timestamp = json.value(QStringLiteral("timestamp")).toString();
        value.feedbackType = json.value(QStringLiteral("feedback_type")).toString();
        for (const QJsonValue &eventId : json.value(QStringLiteral("linked_event_ids")).toArray()) {
            value.linkedEventIds.push_back(eventId.toString());
        }
        value.freeformText = json.value(QStringLiteral("freeform_text")).toString();
        value.severity = json.value(QStringLiteral("severity")).toString(QStringLiteral("normal"));
        return value;
    }
};

struct DiagnosticsSnapshot {
    qint64 totalSessions = 0;
    qint64 totalAudioClips = 0;
    qint64 approximateDiskUsageBytes = 0;
    qint64 toolDecisionsLogged = 0;
    qint64 behaviorDecisionsLogged = 0;
    qint64 memoryDecisionsLogged = 0;
    qint64 explicitFeedbackCorrections = 0;

    [[nodiscard]] QJsonObject toJson() const
    {
        return {
            {QStringLiteral("total_sessions"), totalSessions},
            {QStringLiteral("total_audio_clips"), totalAudioClips},
            {QStringLiteral("approximate_disk_usage_bytes"), approximateDiskUsageBytes},
            {QStringLiteral("tool_decisions_logged"), toolDecisionsLogged},
            {QStringLiteral("behavior_decisions_logged"), behaviorDecisionsLogged},
            {QStringLiteral("memory_decisions_logged"), memoryDecisionsLogged},
            {QStringLiteral("explicit_feedback_corrections"), explicitFeedbackCorrections}
        };
    }
};

} // namespace LearningData

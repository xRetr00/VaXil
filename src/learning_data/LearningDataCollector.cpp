#include "learning_data/LearningDataCollector.h"

#include <QDateTime>
#include <QJsonObject>
#include <QUuid>
#include <QtConcurrent>

#include "logging/LoggingService.h"
#include "settings/AppSettings.h"

namespace LearningData {

namespace {

QString ensureTimestamp(QString timestamp)
{
    timestamp = timestamp.trimmed();
    if (!timestamp.isEmpty()) {
        return timestamp;
    }
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString ensureSchemaVersion(QString schemaVersion)
{
    schemaVersion = schemaVersion.trimmed();
    if (!schemaVersion.isEmpty()) {
        return schemaVersion;
    }
    return defaultSchemaVersion();
}

} // namespace

LearningDataCollector::LearningDataCollector(AppSettings *settings,
                                             LoggingService *loggingService,
                                             QString rootPath)
    : m_settings(settings)
    , m_loggingService(loggingService)
    , m_storage(std::move(rootPath), loggingService)
{
    m_ioPool.setMaxThreadCount(1);
}

LearningDataCollector::~LearningDataCollector()
{
    waitForIdle();
}

bool LearningDataCollector::initialize()
{
    const SettingsSnapshot settingsSnapshot = currentSettings();
    return ensureInitialized(settingsSnapshot);
}

QString LearningDataCollector::rootPath() const
{
    return m_storage.rootPath();
}

SettingsSnapshot LearningDataCollector::currentSettings() const
{
    return SettingsSnapshot::fromAppSettings(m_settings);
}

QString LearningDataCollector::createEventId(const QString &prefix)
{
    const QString safePrefix = prefix.trimmed().isEmpty() ? QStringLiteral("evt") : prefix.trimmed();
    return QStringLiteral("%1_%2_%3")
        .arg(safePrefix,
             QString::number(QDateTime::currentMSecsSinceEpoch()),
             QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
}

void LearningDataCollector::recordSessionEvent(SessionEvent event)
{
    SettingsSnapshot settingsSnapshot = currentSettings();
    if (!collectionEnabled(settingsSnapshot)) {
        return;
    }
    if (!ensureInitialized(settingsSnapshot)) {
        return;
    }

    event.schemaVersion = ensureSchemaVersion(event.schemaVersion);
    event.startedAt = ensureTimestamp(event.startedAt);
    if (!event.endedAt.trimmed().isEmpty()) {
        event.endedAt = ensureTimestamp(event.endedAt);
    }

    enqueue([this, event]() {
        m_storage.appendIndexEvent(QStringLiteral("sessions"), event.startedAt, event.toJson());
    });
}

void LearningDataCollector::recordAudioCaptureEvent(AudioCaptureEvent event, const QByteArray &pcmData)
{
    SettingsSnapshot settingsSnapshot = currentSettings();
    if (!collectionEnabled(settingsSnapshot) || !settingsSnapshot.audioCollectionEnabled) {
        return;
    }
    if (!ensureInitialized(settingsSnapshot)) {
        return;
    }

    event.schemaVersion = ensureSchemaVersion(event.schemaVersion);
    event.timestamp = ensureTimestamp(event.timestamp);
    if (event.eventId.trimmed().isEmpty()) {
        event.eventId = createEventId(QStringLiteral("audio"));
    }

    enqueue([this, settingsSnapshot, event, pcmData]() mutable {
        QString sha256;
        qint64 durationMs = event.durationMs;
        const QString relativePath = m_storage.writeAudioClip(
            event.sessionId,
            event.turnId,
            event.eventId,
            event.audioRole,
            pcmData,
            event.sampleRate,
            event.channels,
            &sha256,
            &durationMs);
        if (relativePath.isEmpty()) {
            return;
        }

        event.filePath = relativePath;
        if (event.sha256.trimmed().isEmpty()) {
            event.sha256 = sha256;
        }
        if (event.durationMs <= 0) {
            event.durationMs = durationMs;
        }

        m_storage.appendIndexEvent(QStringLiteral("audio_events"), event.timestamp, event.toJson());

        if (settingsSnapshot.allowExportPreparedDatasets) {
            m_storage.appendExportRecord(
                QStringLiteral("export_audio_manifest"),
                {
                    {QStringLiteral("schema_version"), event.schemaVersion},
                    {QStringLiteral("session_id"), event.sessionId},
                    {QStringLiteral("turn_id"), event.turnId},
                    {QStringLiteral("audio_event_id"), event.eventId},
                    {QStringLiteral("timestamp"), event.timestamp},
                    {QStringLiteral("audio_role"), event.audioRole},
                    {QStringLiteral("file_path"), event.filePath},
                    {QStringLiteral("duration_ms"), event.durationMs},
                    {QStringLiteral("sample_rate"), event.sampleRate},
                    {QStringLiteral("channels"), event.channels},
                    {QStringLiteral("label_status"), event.labelStatus},
                    {QStringLiteral("sha256"), event.sha256}
                });
        }
    });
}

void LearningDataCollector::recordAsrEvent(AsrEvent event)
{
    SettingsSnapshot settingsSnapshot = currentSettings();
    if (!collectionEnabled(settingsSnapshot) || !settingsSnapshot.transcriptCollectionEnabled) {
        return;
    }
    if (!ensureInitialized(settingsSnapshot)) {
        return;
    }

    event.schemaVersion = ensureSchemaVersion(event.schemaVersion);
    event.timestamp = ensureTimestamp(event.timestamp);
    if (event.eventId.trimmed().isEmpty()) {
        event.eventId = createEventId(QStringLiteral("asr"));
    }

    enqueue([this, settingsSnapshot, event]() {
        m_storage.appendIndexEvent(QStringLiteral("asr_events"), event.timestamp, event.toJson());
        if (settingsSnapshot.allowExportPreparedDatasets) {
            m_storage.appendExportRecord(
                QStringLiteral("export_audio_manifest"),
                {
                    {QStringLiteral("schema_version"), event.schemaVersion},
                    {QStringLiteral("session_id"), event.sessionId},
                    {QStringLiteral("turn_id"), event.turnId},
                    {QStringLiteral("asr_event_id"), event.eventId},
                    {QStringLiteral("timestamp"), event.timestamp},
                    {QStringLiteral("source_audio_event_id"), event.sourceAudioEventId},
                    {QStringLiteral("stt_engine"), event.sttEngine},
                    {QStringLiteral("raw_transcript"), event.rawTranscript},
                    {QStringLiteral("normalized_transcript"), event.normalizedTranscript},
                    {QStringLiteral("final_transcript"), event.finalTranscript},
                    {QStringLiteral("transcript_source"), event.transcriptSource},
                    {QStringLiteral("confidence"), event.confidence},
                    {QStringLiteral("was_user_edited"), event.wasUserEdited},
                    {QStringLiteral("transcript_changed"), event.transcriptChanged}
                });
        }
    });
}

void LearningDataCollector::recordToolDecisionEvent(ToolDecisionEvent event)
{
    SettingsSnapshot settingsSnapshot = currentSettings();
    if (!collectionEnabled(settingsSnapshot) || !settingsSnapshot.toolLoggingEnabled) {
        return;
    }
    if (!ensureInitialized(settingsSnapshot)) {
        return;
    }

    event.schemaVersion = ensureSchemaVersion(event.schemaVersion);
    event.timestamp = ensureTimestamp(event.timestamp);
    if (event.eventId.trimmed().isEmpty()) {
        event.eventId = createEventId(QStringLiteral("tool_decision"));
    }

    enqueue([this, settingsSnapshot, event]() {
        m_storage.appendIndexEvent(QStringLiteral("tool_decisions"), event.timestamp, event.toJson());
        if (settingsSnapshot.allowExportPreparedDatasets) {
            QJsonArray candidates;
            for (const ToolCandidateScore &candidate : event.candidateToolsWithScores) {
                candidates.push_back(candidate.toJson());
            }
            m_storage.appendExportRecord(
                QStringLiteral("export_tool_policy_manifest"),
                {
                    {QStringLiteral("schema_version"), event.schemaVersion},
                    {QStringLiteral("record_type"), QStringLiteral("tool_decision")},
                    {QStringLiteral("event_id"), event.eventId},
                    {QStringLiteral("session_id"), event.sessionId},
                    {QStringLiteral("turn_id"), event.turnId},
                    {QStringLiteral("timestamp"), event.timestamp},
                    {QStringLiteral("user_input_text"), event.userInputText},
                    {QStringLiteral("selected_tool"), event.selectedTool},
                    {QStringLiteral("available_tools"), QJsonArray::fromStringList(event.availableTools)},
                    {QStringLiteral("candidate_tools_with_scores"), candidates},
                    {QStringLiteral("decision_source"), event.decisionSource},
                    {QStringLiteral("expected_confirmation_level"), event.expectedConfirmationLevel},
                    {QStringLiteral("no_tool_option_considered"), event.noToolOptionConsidered}
                });
        }
    });
}

void LearningDataCollector::recordToolExecutionEvent(ToolExecutionEvent event)
{
    SettingsSnapshot settingsSnapshot = currentSettings();
    if (!collectionEnabled(settingsSnapshot) || !settingsSnapshot.toolLoggingEnabled) {
        return;
    }
    if (!ensureInitialized(settingsSnapshot)) {
        return;
    }

    event.schemaVersion = ensureSchemaVersion(event.schemaVersion);
    event.timestamp = ensureTimestamp(event.timestamp);
    event.executionStartedAt = ensureTimestamp(event.executionStartedAt);
    event.executionFinishedAt = ensureTimestamp(event.executionFinishedAt);
    if (event.eventId.trimmed().isEmpty()) {
        event.eventId = createEventId(QStringLiteral("tool_exec"));
    }

    enqueue([this, settingsSnapshot, event]() {
        m_storage.appendIndexEvent(QStringLiteral("tool_executions"), event.timestamp, event.toJson());
        if (settingsSnapshot.allowExportPreparedDatasets) {
            m_storage.appendExportRecord(
                QStringLiteral("export_tool_policy_manifest"),
                {
                    {QStringLiteral("schema_version"), event.schemaVersion},
                    {QStringLiteral("record_type"), QStringLiteral("tool_execution")},
                    {QStringLiteral("event_id"), event.eventId},
                    {QStringLiteral("session_id"), event.sessionId},
                    {QStringLiteral("turn_id"), event.turnId},
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
                    {QStringLiteral("final_outcome_label"), event.finalOutcomeLabel}
                });
        }
    });
}

void LearningDataCollector::recordBehaviorDecisionEvent(BehaviorDecisionEvent event)
{
    SettingsSnapshot settingsSnapshot = currentSettings();
    if (!collectionEnabled(settingsSnapshot) || !settingsSnapshot.behaviorLoggingEnabled) {
        return;
    }
    if (!ensureInitialized(settingsSnapshot)) {
        return;
    }

    event.schemaVersion = ensureSchemaVersion(event.schemaVersion);
    event.timestamp = ensureTimestamp(event.timestamp);
    if (event.eventId.trimmed().isEmpty()) {
        event.eventId = createEventId(QStringLiteral("behavior"));
    }

    enqueue([this, settingsSnapshot, event]() {
        m_storage.appendIndexEvent(QStringLiteral("behavior_events"), event.timestamp, event.toJson());
        if (settingsSnapshot.allowExportPreparedDatasets) {
            m_storage.appendExportRecord(
                QStringLiteral("export_behavior_policy_manifest"),
                {
                    {QStringLiteral("schema_version"), event.schemaVersion},
                    {QStringLiteral("record_type"), QStringLiteral("behavior_decision")},
                    {QStringLiteral("event_id"), event.eventId},
                    {QStringLiteral("session_id"), event.sessionId},
                    {QStringLiteral("turn_id"), event.turnId},
                    {QStringLiteral("timestamp"), event.timestamp},
                    {QStringLiteral("response_mode"), event.responseMode},
                    {QStringLiteral("why_selected"), event.whySelected},
                    {QStringLiteral("interrupted_user"), event.interruptedUser},
                    {QStringLiteral("follow_up_attempted"), event.followUpAttempted},
                    {QStringLiteral("follow_up_helpful_label"), event.followUpHelpfulLabel},
                    {QStringLiteral("verbosity_level"), event.verbosityLevel},
                    {QStringLiteral("speaking_duration_ms"), event.speakingDurationMs}
                });
        }
    });
}

void LearningDataCollector::recordMemoryDecisionEvent(MemoryDecisionEvent event)
{
    SettingsSnapshot settingsSnapshot = currentSettings();
    if (!collectionEnabled(settingsSnapshot) || !settingsSnapshot.memoryLoggingEnabled) {
        return;
    }
    if (!ensureInitialized(settingsSnapshot)) {
        return;
    }

    event.schemaVersion = ensureSchemaVersion(event.schemaVersion);
    event.timestamp = ensureTimestamp(event.timestamp);
    if (event.eventId.trimmed().isEmpty()) {
        event.eventId = createEventId(QStringLiteral("memory"));
    }

    enqueue([this, settingsSnapshot, event]() {
        m_storage.appendIndexEvent(QStringLiteral("memory_events"), event.timestamp, event.toJson());
        if (settingsSnapshot.allowExportPreparedDatasets) {
            m_storage.appendExportRecord(
                QStringLiteral("export_memory_policy_manifest"),
                {
                    {QStringLiteral("schema_version"), event.schemaVersion},
                    {QStringLiteral("record_type"), QStringLiteral("memory_decision")},
                    {QStringLiteral("event_id"), event.eventId},
                    {QStringLiteral("session_id"), event.sessionId},
                    {QStringLiteral("turn_id"), event.turnId},
                    {QStringLiteral("timestamp"), event.timestamp},
                    {QStringLiteral("memory_candidate_present"), event.memoryCandidatePresent},
                    {QStringLiteral("memory_action"), event.memoryAction},
                    {QStringLiteral("memory_type"), event.memoryType},
                    {QStringLiteral("confidence"), event.confidence},
                    {QStringLiteral("privacy_risk_level"), event.privacyRiskLevel},
                    {QStringLiteral("was_user_confirmed"), event.wasUserConfirmed},
                    {QStringLiteral("outcome_label"), event.outcomeLabel}
                });
        }
    });
}

void LearningDataCollector::recordUserFeedbackEvent(UserFeedbackEvent event)
{
    SettingsSnapshot settingsSnapshot = currentSettings();
    if (!collectionEnabled(settingsSnapshot)
        || (!settingsSnapshot.behaviorLoggingEnabled && !settingsSnapshot.memoryLoggingEnabled)) {
        return;
    }
    if (!ensureInitialized(settingsSnapshot)) {
        return;
    }

    event.schemaVersion = ensureSchemaVersion(event.schemaVersion);
    event.timestamp = ensureTimestamp(event.timestamp);
    if (event.eventId.trimmed().isEmpty()) {
        event.eventId = createEventId(QStringLiteral("feedback"));
    }

    enqueue([this, settingsSnapshot, event]() {
        m_storage.appendIndexEvent(QStringLiteral("feedback_events"), event.timestamp, event.toJson());

        if (!settingsSnapshot.allowExportPreparedDatasets) {
            return;
        }

        const QJsonObject baseRecord{
            {QStringLiteral("schema_version"), event.schemaVersion},
            {QStringLiteral("record_type"), QStringLiteral("feedback")},
            {QStringLiteral("event_id"), event.eventId},
            {QStringLiteral("session_id"), event.sessionId},
            {QStringLiteral("turn_id"), event.turnId},
            {QStringLiteral("timestamp"), event.timestamp},
            {QStringLiteral("feedback_type"), event.feedbackType},
            {QStringLiteral("linked_event_ids"), QJsonArray::fromStringList(event.linkedEventIds)},
            {QStringLiteral("freeform_text"), event.freeformText},
            {QStringLiteral("severity"), event.severity}
        };

        if (settingsSnapshot.behaviorLoggingEnabled) {
            m_storage.appendExportRecord(QStringLiteral("export_behavior_policy_manifest"), baseRecord);
        }
        if (settingsSnapshot.memoryLoggingEnabled) {
            m_storage.appendExportRecord(QStringLiteral("export_memory_policy_manifest"), baseRecord);
        }
    });
}

void LearningDataCollector::runMaintenance()
{
    SettingsSnapshot settingsSnapshot = currentSettings();
    if (!collectionEnabled(settingsSnapshot)) {
        return;
    }
    if (!ensureInitialized(settingsSnapshot)) {
        return;
    }

    enqueue([this, settingsSnapshot]() {
        m_storage.cleanupRetention(settingsSnapshot);
        const DiagnosticsSnapshot snapshot = m_storage.diagnosticsSnapshot();
        maybeLogDiagnostics(snapshot);
    });
}

DiagnosticsSnapshot LearningDataCollector::diagnosticsSnapshot() const
{
    if (!m_initialized) {
        return {};
    }
    return m_storage.diagnosticsSnapshot();
}

void LearningDataCollector::waitForIdle()
{
    m_ioPool.waitForDone();
}

bool LearningDataCollector::ensureInitialized(const SettingsSnapshot &settingsSnapshot)
{
    if (!collectionEnabled(settingsSnapshot)) {
        return false;
    }

    QMutexLocker locker(&m_initMutex);
    if (m_initialized) {
        return true;
    }

    m_initialized = m_storage.initialize();
    if (!m_initialized && m_loggingService != nullptr) {
        m_loggingService->warnFor(
            QStringLiteral("tools_mcp"),
            QStringLiteral("[learning_data] initialization failed for root=%1")
                .arg(m_storage.rootPath()));
    }
    return m_initialized;
}

bool LearningDataCollector::collectionEnabled(const SettingsSnapshot &settingsSnapshot) const
{
    return settingsSnapshot.enabled && settingsSnapshot.hasAnyCategoryEnabled();
}

void LearningDataCollector::enqueue(const std::function<void()> &task)
{
    QtConcurrent::run(&m_ioPool, task);
}

void LearningDataCollector::maybeLogDiagnostics(const DiagnosticsSnapshot &snapshot) const
{
    if (m_loggingService == nullptr) {
        return;
    }

    m_loggingService->infoFor(
        QStringLiteral("tools_mcp"),
        QStringLiteral("[learning_data] sessions=%1 audio_clips=%2 bytes=%3 tool_decisions=%4 behavior_decisions=%5 memory_decisions=%6 explicit_feedback_corrections=%7")
            .arg(snapshot.totalSessions)
            .arg(snapshot.totalAudioClips)
            .arg(snapshot.approximateDiskUsageBytes)
            .arg(snapshot.toolDecisionsLogged)
            .arg(snapshot.behaviorDecisionsLogged)
            .arg(snapshot.memoryDecisionsLogged)
            .arg(snapshot.explicitFeedbackCorrections));
}

} // namespace LearningData

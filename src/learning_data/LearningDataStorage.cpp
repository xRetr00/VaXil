#include "learning_data/LearningDataStorage.h"

#include <algorithm>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

#include "logging/LoggingService.h"

namespace LearningData {
namespace {

const QString kBucketSessions = QStringLiteral("sessions");
const QString kBucketAudioIndex = QStringLiteral("audio_index");
const QString kBucketAsrEvents = QStringLiteral("asr_events");
const QString kBucketToolDecisionEvents = QStringLiteral("tool_decision_events");
const QString kBucketToolExecutionEvents = QStringLiteral("tool_execution_events");
const QString kBucketBehaviorEvents = QStringLiteral("behavior_events");
const QString kBucketMemoryEvents = QStringLiteral("memory_events");
const QString kBucketFeedbackEvents = QStringLiteral("feedback_events");

QString defaultRootPath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString safeBase = base.trimmed().isEmpty() ? QDir::tempPath() : base;
    return QDir(safeBase).filePath(QStringLiteral("learning"));
}

QString turnKeyFor(const QJsonObject &obj)
{
    return obj.value(QStringLiteral("session_id")).toString()
        + QStringLiteral("::")
        + obj.value(QStringLiteral("turn_id")).toString();
}

QString optionalString(const QJsonObject &obj, const QString &key)
{
    const QJsonValue value = obj.value(key);
    return value.isString() ? value.toString() : QString();
}

void appendObjectsForKey(QMultiHash<QString, QJsonObject> *map,
                         const QString &key,
                         const QJsonObject &payload)
{
    if (!map || key.trimmed().isEmpty()) {
        return;
    }
    map->insert(key, payload);
}

} // namespace

LearningDataStorage::LearningDataStorage(QString rootPath, LoggingService *loggingService)
    : m_rootPath(rootPath.trimmed())
    , m_loggingService(loggingService)
{
}

bool LearningDataStorage::initialize(const LearningDataSettingsSnapshot &settings)
{
    const bool ok = ensureLayout(settings);
    if (!ok && m_loggingService) {
        m_loggingService->errorFor(QStringLiteral("tool_audit"),
                                   QStringLiteral("[learning_data] initialize failed. root=%1").arg(settings.rootDirectory));
    }
    return ok;
}

bool LearningDataStorage::writeSessionEvent(const SessionEvent &event)
{
    SessionEvent payload = event;
    if (payload.schemaVersion.trimmed().isEmpty()) {
        payload.schemaVersion = kSchemaVersion;
    }
    if (payload.startedAt.trimmed().isEmpty()) {
        payload.startedAt = toIsoUtcNow();
    }

    if (!appendJsonLine(datedJsonlPath(kBucketSessions, payload.startedAt), toJson(payload))) {
        appendFailureRecord(QStringLiteral("session"),
                            QStringLiteral("append_failed"),
                            toJson(payload));
        return false;
    }
    return true;
}

bool LearningDataStorage::writeAudioCaptureEvent(AudioCaptureEvent event, const QByteArray &pcmData)
{
    if (event.schemaVersion.trimmed().isEmpty()) {
        event.schemaVersion = kSchemaVersion;
    }
    if (event.timestamp.trimmed().isEmpty()) {
        event.timestamp = toIsoUtcNow();
    }

    QString fileHash;
    qint64 durationMs = 0;
    const QString absolutePath = buildAudioFilePath(event);
    if (!writeWavFile(absolutePath,
                      pcmData,
                      event.sampleRate,
                      event.channels,
                      &fileHash,
                      &durationMs)) {
        appendFailureRecord(QStringLiteral("audio_capture"),
                            QStringLiteral("wav_write_failed"),
                            toJson(event));
        return false;
    }

    event.filePath = asRelativePath(absolutePath);
    event.fileHashSha256 = fileHash;
    event.durationMs = durationMs;

    const QJsonObject payload = toJson(event);
    if (!appendJsonLine(datedJsonlPath(kBucketAudioIndex, event.timestamp), payload)) {
        appendFailureRecord(QStringLiteral("audio_capture"),
                            QStringLiteral("append_failed"),
                            payload);
        return false;
    }
    return true;
}

bool LearningDataStorage::writeAsrEvent(const AsrEvent &event)
{
    AsrEvent payload = event;
    if (payload.schemaVersion.trimmed().isEmpty()) {
        payload.schemaVersion = kSchemaVersion;
    }
    if (payload.timestamp.trimmed().isEmpty()) {
        payload.timestamp = toIsoUtcNow();
    }

    const QJsonObject obj = toJson(payload);
    if (!appendJsonLine(datedJsonlPath(kBucketAsrEvents, payload.timestamp), obj)) {
        appendFailureRecord(QStringLiteral("asr"), QStringLiteral("append_failed"), obj);
        return false;
    }
    return true;
}

bool LearningDataStorage::writeToolDecisionEvent(const ToolDecisionEvent &event)
{
    ToolDecisionEvent payload = event;
    if (payload.schemaVersion.trimmed().isEmpty()) {
        payload.schemaVersion = kSchemaVersion;
    }
    if (payload.timestamp.trimmed().isEmpty()) {
        payload.timestamp = toIsoUtcNow();
    }

    const QJsonObject obj = toJson(payload);
    if (!appendJsonLine(datedJsonlPath(kBucketToolDecisionEvents, payload.timestamp), obj)) {
        appendFailureRecord(QStringLiteral("tool_decision"), QStringLiteral("append_failed"), obj);
        return false;
    }
    return true;
}

bool LearningDataStorage::writeToolExecutionEvent(const ToolExecutionEvent &event)
{
    ToolExecutionEvent payload = event;
    if (payload.schemaVersion.trimmed().isEmpty()) {
        payload.schemaVersion = kSchemaVersion;
    }
    if (payload.timestamp.trimmed().isEmpty()) {
        payload.timestamp = toIsoUtcNow();
    }

    const QJsonObject obj = toJson(payload);
    if (!appendJsonLine(datedJsonlPath(kBucketToolExecutionEvents, payload.timestamp), obj)) {
        appendFailureRecord(QStringLiteral("tool_execution"), QStringLiteral("append_failed"), obj);
        return false;
    }
    return true;
}

bool LearningDataStorage::writeBehaviorDecisionEvent(const BehaviorDecisionEvent &event)
{
    BehaviorDecisionEvent payload = event;
    if (payload.schemaVersion.trimmed().isEmpty()) {
        payload.schemaVersion = kSchemaVersion;
    }
    if (payload.timestamp.trimmed().isEmpty()) {
        payload.timestamp = toIsoUtcNow();
    }

    const QJsonObject obj = toJson(payload);
    if (!appendJsonLine(datedJsonlPath(kBucketBehaviorEvents, payload.timestamp), obj)) {
        appendFailureRecord(QStringLiteral("behavior_decision"), QStringLiteral("append_failed"), obj);
        return false;
    }
    return true;
}

bool LearningDataStorage::writeMemoryDecisionEvent(const MemoryDecisionEvent &event)
{
    MemoryDecisionEvent payload = event;
    if (payload.schemaVersion.trimmed().isEmpty()) {
        payload.schemaVersion = kSchemaVersion;
    }
    if (payload.timestamp.trimmed().isEmpty()) {
        payload.timestamp = toIsoUtcNow();
    }

    const QJsonObject obj = toJson(payload);
    if (!appendJsonLine(datedJsonlPath(kBucketMemoryEvents, payload.timestamp), obj)) {
        appendFailureRecord(QStringLiteral("memory_decision"), QStringLiteral("append_failed"), obj);
        return false;
    }
    return true;
}

bool LearningDataStorage::writeUserFeedbackEvent(const UserFeedbackEvent &event)
{
    UserFeedbackEvent payload = event;
    if (payload.schemaVersion.trimmed().isEmpty()) {
        payload.schemaVersion = kSchemaVersion;
    }
    if (payload.timestamp.trimmed().isEmpty()) {
        payload.timestamp = toIsoUtcNow();
    }

    const QJsonObject obj = toJson(payload);
    if (!appendJsonLine(datedJsonlPath(kBucketFeedbackEvents, payload.timestamp), obj)) {
        appendFailureRecord(QStringLiteral("user_feedback"), QStringLiteral("append_failed"), obj);
        return false;
    }
    return true;
}

bool LearningDataStorage::runRetention(const LearningDataSettingsSnapshot &settings)
{
    if (!ensureLayout(settings)) {
        return false;
    }

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    const QDateTime audioCutoffUtc = nowUtc.addDays(-std::max(1, settings.maxDaysToKeepAudio));
    const QDateTime structuredCutoffUtc = nowUtc.addDays(-std::max(1, settings.maxDaysToKeepStructuredLogs));

    bool ok = true;
    ok = removeFilesOlderThan(audioRoot(),
                              audioCutoffUtc,
                              QStringLiteral("audio_file"),
                              QStringLiteral("audio_retention_days"))
        && ok;
    ok = enforceAudioStorageLimitGb(settings.maxAudioStorageGb) && ok;
    ok = removeFilesOlderThan(indexRoot(),
                              structuredCutoffUtc,
                              QStringLiteral("structured_log"),
                              QStringLiteral("structured_log_retention_days"))
        && ok;

    const LearningDataDiagnostics diagnostics = collectDiagnostics();
    if (m_loggingService) {
        m_loggingService->infoFor(
            QStringLiteral("tool_audit"),
            QStringLiteral("[learning_data] sessions=%1 audio=%2 tool_decisions=%3 behavior=%4 memory=%5 feedback=%6 bytes=%7")
                .arg(diagnostics.sessions)
                .arg(diagnostics.audioClips)
                .arg(diagnostics.toolDecisions)
                .arg(diagnostics.behaviorDecisions)
                .arg(diagnostics.memoryDecisions)
                .arg(diagnostics.feedbackEvents)
                .arg(diagnostics.approximateDiskUsageBytes));
    }

    return ok;
}

bool LearningDataStorage::exportPreparedManifests(const LearningDataSettingsSnapshot &settings)
{
    if (!settings.allowPreparedDatasetExport) {
        return true;
    }

    if (!ensureLayout(settings)) {
        return false;
    }

    const QString exportDir = QDir(exportsRoot()).filePath(
        QStringLiteral("export_%1").arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss"))));
    if (!QDir().mkpath(exportDir)) {
        return false;
    }

    const QList<QJsonObject> audioRows = readAllJsonlObjects(QDir(indexRoot()).filePath(kBucketAudioIndex));
    const QList<QJsonObject> asrRows = readAllJsonlObjects(QDir(indexRoot()).filePath(kBucketAsrEvents));
    const QList<QJsonObject> toolDecisionRows = readAllJsonlObjects(QDir(indexRoot()).filePath(kBucketToolDecisionEvents));
    const QList<QJsonObject> toolExecutionRows = readAllJsonlObjects(QDir(indexRoot()).filePath(kBucketToolExecutionEvents));
    const QList<QJsonObject> behaviorRows = readAllJsonlObjects(QDir(indexRoot()).filePath(kBucketBehaviorEvents));
    const QList<QJsonObject> memoryRows = readAllJsonlObjects(QDir(indexRoot()).filePath(kBucketMemoryEvents));
    const QList<QJsonObject> feedbackRows = readAllJsonlObjects(QDir(indexRoot()).filePath(kBucketFeedbackEvents));

    QHash<QString, QJsonObject> asrByAudioEvent;
    QMultiHash<QString, QJsonObject> asrByTurn;
    for (const QJsonObject &asr : asrRows) {
        const QString sourceAudio = optionalString(asr, QStringLiteral("source_audio_event_id"));
        if (!sourceAudio.trimmed().isEmpty() && !asrByAudioEvent.contains(sourceAudio)) {
            asrByAudioEvent.insert(sourceAudio, asr);
        }
        appendObjectsForKey(&asrByTurn, turnKeyFor(asr), asr);
    }

    QList<QJsonObject> audioManifest;
    for (const QJsonObject &audio : audioRows) {
        const QString audioRole = optionalString(audio, QStringLiteral("audio_role"));
        if (audioRole != QStringLiteral("command_raw") && audioRole != QStringLiteral("command_trimmed")) {
            continue;
        }

        QJsonObject row{
            {QStringLiteral("schema_version"), kSchemaVersion},
            {QStringLiteral("session_id"), optionalString(audio, QStringLiteral("session_id"))},
            {QStringLiteral("turn_id"), optionalString(audio, QStringLiteral("turn_id"))},
            {QStringLiteral("audio_event_id"), optionalString(audio, QStringLiteral("event_id"))},
            {QStringLiteral("audio_role"), audioRole},
            {QStringLiteral("file_path"), optionalString(audio, QStringLiteral("file_path"))},
            {QStringLiteral("label_status"), optionalString(audio, QStringLiteral("label_status"))},
            {QStringLiteral("duration_ms"), audio.value(QStringLiteral("duration_ms"))},
            {QStringLiteral("sample_rate"), audio.value(QStringLiteral("sample_rate"))},
            {QStringLiteral("channels"), audio.value(QStringLiteral("channels"))},
            {QStringLiteral("sample_format"), optionalString(audio, QStringLiteral("sample_format"))},
            {QStringLiteral("hash_sha256"), optionalString(audio, QStringLiteral("file_hash_sha256"))}
        };

        QJsonObject linkedAsr;
        const QString audioEventId = optionalString(audio, QStringLiteral("event_id"));
        if (asrByAudioEvent.contains(audioEventId)) {
            linkedAsr = asrByAudioEvent.value(audioEventId);
        } else {
            const QString turnKey = turnKeyFor(audio);
            const QList<QJsonObject> candidates = asrByTurn.values(turnKey);
            if (!candidates.isEmpty()) {
                linkedAsr = candidates.first();
            }
        }

        if (!linkedAsr.isEmpty()) {
            row.insert(QStringLiteral("raw_transcript"), optionalString(linkedAsr, QStringLiteral("raw_transcript")));
            row.insert(QStringLiteral("normalized_transcript"), optionalString(linkedAsr, QStringLiteral("normalized_transcript")));
            row.insert(QStringLiteral("final_transcript"), optionalString(linkedAsr, QStringLiteral("final_transcript")));
            row.insert(QStringLiteral("transcript_source"), optionalString(linkedAsr, QStringLiteral("transcript_source")));
            row.insert(QStringLiteral("transcript_confidence"), linkedAsr.value(QStringLiteral("confidence")));
            row.insert(QStringLiteral("was_user_edited"), linkedAsr.value(QStringLiteral("was_user_edited")));
        }

        audioManifest.push_back(row);
    }

    QHash<QString, QJsonObject> executionByTurn;
    for (const QJsonObject &execution : toolExecutionRows) {
        const QString key = turnKeyFor(execution);
        if (!executionByTurn.contains(key)) {
            executionByTurn.insert(key, execution);
        }
    }

    QList<QJsonObject> toolManifest;
    QSet<QString> mergedExecutionKeys;
    for (const QJsonObject &decision : toolDecisionRows) {
        const QString key = turnKeyFor(decision);
        QJsonObject row{
            {QStringLiteral("schema_version"), kSchemaVersion},
            {QStringLiteral("session_id"), optionalString(decision, QStringLiteral("session_id"))},
            {QStringLiteral("turn_id"), optionalString(decision, QStringLiteral("turn_id"))},
            {QStringLiteral("tool_decision_event_id"), optionalString(decision, QStringLiteral("event_id"))},
            {QStringLiteral("input_mode"), optionalString(decision, QStringLiteral("input_mode"))},
            {QStringLiteral("selected_tool"), optionalString(decision, QStringLiteral("selected_tool"))},
            {QStringLiteral("decision_source"), optionalString(decision, QStringLiteral("decision_source"))},
            {QStringLiteral("expected_confirmation_level"), optionalString(decision, QStringLiteral("expected_confirmation_level"))},
            {QStringLiteral("no_tool_option_considered"), decision.value(QStringLiteral("no_tool_option_considered"))},
            {QStringLiteral("available_tools"), decision.value(QStringLiteral("available_tools"))},
            {QStringLiteral("candidate_tools_with_scores"), decision.value(QStringLiteral("candidate_tools_with_scores"))}
        };

        if (executionByTurn.contains(key)) {
            mergedExecutionKeys.insert(key);
            const QJsonObject execution = executionByTurn.value(key);
            row.insert(QStringLiteral("tool_execution_event_id"), optionalString(execution, QStringLiteral("event_id")));
            row.insert(QStringLiteral("execution_selected_tool"), optionalString(execution, QStringLiteral("selected_tool")));
            row.insert(QStringLiteral("tool_args_redacted"), execution.value(QStringLiteral("tool_args_redacted")));
            row.insert(QStringLiteral("succeeded"), execution.value(QStringLiteral("succeeded")));
            row.insert(QStringLiteral("failure_type"), optionalString(execution, QStringLiteral("failure_type")));
            row.insert(QStringLiteral("latency_ms"), execution.value(QStringLiteral("latency_ms")));
            row.insert(QStringLiteral("retry_count"), execution.value(QStringLiteral("retry_count")));
            row.insert(QStringLiteral("final_outcome_label"), optionalString(execution, QStringLiteral("final_outcome_label")));
        }

        toolManifest.push_back(row);
    }

    for (auto it = executionByTurn.begin(); it != executionByTurn.end(); ++it) {
        if (mergedExecutionKeys.contains(it.key())) {
            continue;
        }
        const QJsonObject execution = it.value();
        QJsonObject row{
            {QStringLiteral("schema_version"), kSchemaVersion},
            {QStringLiteral("session_id"), optionalString(execution, QStringLiteral("session_id"))},
            {QStringLiteral("turn_id"), optionalString(execution, QStringLiteral("turn_id"))},
            {QStringLiteral("tool_execution_event_id"), optionalString(execution, QStringLiteral("event_id"))},
            {QStringLiteral("execution_selected_tool"), optionalString(execution, QStringLiteral("selected_tool"))},
            {QStringLiteral("tool_args_redacted"), execution.value(QStringLiteral("tool_args_redacted"))},
            {QStringLiteral("succeeded"), execution.value(QStringLiteral("succeeded"))},
            {QStringLiteral("failure_type"), optionalString(execution, QStringLiteral("failure_type"))},
            {QStringLiteral("latency_ms"), execution.value(QStringLiteral("latency_ms"))},
            {QStringLiteral("retry_count"), execution.value(QStringLiteral("retry_count"))},
            {QStringLiteral("final_outcome_label"), optionalString(execution, QStringLiteral("final_outcome_label"))}
        };
        toolManifest.push_back(row);
    }

    QMultiHash<QString, QJsonObject> feedbackByTurn;
    for (const QJsonObject &feedback : feedbackRows) {
        appendObjectsForKey(&feedbackByTurn, turnKeyFor(feedback), feedback);
    }

    QList<QJsonObject> behaviorManifest;
    for (const QJsonObject &behavior : behaviorRows) {
        const QString key = turnKeyFor(behavior);
        const QList<QJsonObject> linkedFeedback = feedbackByTurn.values(key);
        QJsonArray feedbackTypes;
        for (const QJsonObject &feedback : linkedFeedback) {
            feedbackTypes.push_back(optionalString(feedback, QStringLiteral("feedback_type")));
        }

        QJsonObject row{
            {QStringLiteral("schema_version"), kSchemaVersion},
            {QStringLiteral("session_id"), optionalString(behavior, QStringLiteral("session_id"))},
            {QStringLiteral("turn_id"), optionalString(behavior, QStringLiteral("turn_id"))},
            {QStringLiteral("behavior_event_id"), optionalString(behavior, QStringLiteral("event_id"))},
            {QStringLiteral("response_mode"), optionalString(behavior, QStringLiteral("response_mode"))},
            {QStringLiteral("why_selected"), optionalString(behavior, QStringLiteral("why_selected"))},
            {QStringLiteral("follow_up_attempted"), behavior.value(QStringLiteral("follow_up_attempted"))},
            {QStringLiteral("verbosity_level"), optionalString(behavior, QStringLiteral("verbosity_level"))},
            {QStringLiteral("speaking_duration_ms"), behavior.value(QStringLiteral("speaking_duration_ms"))},
            {QStringLiteral("feedback_types"), feedbackTypes},
            {QStringLiteral("feedback_count"), linkedFeedback.size()}
        };
        behaviorManifest.push_back(row);
    }

    QList<QJsonObject> memoryManifest;
    for (const QJsonObject &memory : memoryRows) {
        const QString key = turnKeyFor(memory);
        const QList<QJsonObject> linkedFeedback = feedbackByTurn.values(key);
        QJsonArray feedbackTypes;
        for (const QJsonObject &feedback : linkedFeedback) {
            feedbackTypes.push_back(optionalString(feedback, QStringLiteral("feedback_type")));
        }

        QJsonObject row{
            {QStringLiteral("schema_version"), kSchemaVersion},
            {QStringLiteral("session_id"), optionalString(memory, QStringLiteral("session_id"))},
            {QStringLiteral("turn_id"), optionalString(memory, QStringLiteral("turn_id"))},
            {QStringLiteral("memory_event_id"), optionalString(memory, QStringLiteral("event_id"))},
            {QStringLiteral("memory_candidate_present"), memory.value(QStringLiteral("memory_candidate_present"))},
            {QStringLiteral("memory_action"), optionalString(memory, QStringLiteral("memory_action"))},
            {QStringLiteral("memory_type"), optionalString(memory, QStringLiteral("memory_type"))},
            {QStringLiteral("privacy_risk_level"), optionalString(memory, QStringLiteral("privacy_risk_level"))},
            {QStringLiteral("outcome_label"), optionalString(memory, QStringLiteral("outcome_label"))},
            {QStringLiteral("feedback_types"), feedbackTypes},
            {QStringLiteral("feedback_count"), linkedFeedback.size()}
        };
        memoryManifest.push_back(row);
    }

    bool ok = true;
    ok = writeJsonlFile(QDir(exportDir).filePath(QStringLiteral("export_audio_manifest.jsonl")), audioManifest) && ok;
    ok = writeJsonlFile(QDir(exportDir).filePath(QStringLiteral("export_tool_policy_manifest.jsonl")), toolManifest) && ok;
    ok = writeJsonlFile(QDir(exportDir).filePath(QStringLiteral("export_behavior_policy_manifest.jsonl")), behaviorManifest) && ok;
    ok = writeJsonlFile(QDir(exportDir).filePath(QStringLiteral("export_memory_policy_manifest.jsonl")), memoryManifest) && ok;

    if (m_loggingService) {
        m_loggingService->infoFor(
            QStringLiteral("tool_audit"),
            QStringLiteral("[learning_data] export path=%1 audio_rows=%2 tool_rows=%3 behavior_rows=%4 memory_rows=%5")
                .arg(exportDir)
                .arg(audioManifest.size())
                .arg(toolManifest.size())
                .arg(behaviorManifest.size())
                .arg(memoryManifest.size()));
    }

    return ok;
}

LearningDataDiagnostics LearningDataStorage::collectDiagnostics() const
{
    LearningDataDiagnostics out;

    auto countRows = [](const QString &rootDir) -> qint64 {
        qint64 count = 0;
        QDirIterator it(rootDir, QStringList{QStringLiteral("*.jsonl")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QFile file(it.next());
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                continue;
            }
            while (!file.atEnd()) {
                const QByteArray line = file.readLine().trimmed();
                if (!line.isEmpty()) {
                    ++count;
                }
            }
        }
        return count;
    };

    out.sessions = countRows(QDir(indexRoot()).filePath(kBucketSessions));
    out.audioClips = countRows(QDir(indexRoot()).filePath(kBucketAudioIndex));
    out.toolDecisions = countRows(QDir(indexRoot()).filePath(kBucketToolDecisionEvents));
    out.toolExecutions = countRows(QDir(indexRoot()).filePath(kBucketToolExecutionEvents));
    out.behaviorDecisions = countRows(QDir(indexRoot()).filePath(kBucketBehaviorEvents));
    out.memoryDecisions = countRows(QDir(indexRoot()).filePath(kBucketMemoryEvents));
    out.feedbackEvents = countRows(QDir(indexRoot()).filePath(kBucketFeedbackEvents));

    qint64 sizeBytes = 0;
    QDirIterator sizeIt(rootPath(), QDir::Files, QDirIterator::Subdirectories);
    while (sizeIt.hasNext()) {
        const QFileInfo info(sizeIt.next());
        sizeBytes += info.size();
    }
    out.approximateDiskUsageBytes = sizeBytes;

    return out;
}

bool LearningDataStorage::ensureLayout(const LearningDataSettingsSnapshot &settings)
{
    if (m_rootPath.trimmed().isEmpty()) {
        m_rootPath = settings.rootDirectory.trimmed();
    }
    if (m_rootPath.trimmed().isEmpty()) {
        m_rootPath = defaultRootPath();
    }

    const QStringList requiredDirs{
        rootPath(),
        indexRoot(),
        audioRoot(),
        snapshotsRoot(),
        exportsRoot(),
        quarantineRoot(),
        retentionRoot()
    };

    for (const QString &dirPath : requiredDirs) {
        if (!QDir().mkpath(dirPath)) {
            return false;
        }
    }

    return writeSchemaVersionFile();
}

QString LearningDataStorage::rootPath() const
{
    return m_rootPath;
}

QString LearningDataStorage::indexRoot() const
{
    return QDir(rootPath()).filePath(QStringLiteral("index"));
}

QString LearningDataStorage::audioRoot() const
{
    return QDir(rootPath()).filePath(QStringLiteral("audio"));
}

QString LearningDataStorage::snapshotsRoot() const
{
    return QDir(rootPath()).filePath(QStringLiteral("snapshots"));
}

QString LearningDataStorage::exportsRoot() const
{
    return QDir(rootPath()).filePath(QStringLiteral("exports"));
}

QString LearningDataStorage::quarantineRoot() const
{
    return QDir(rootPath()).filePath(QStringLiteral("quarantine"));
}

QString LearningDataStorage::retentionRoot() const
{
    return QDir(rootPath()).filePath(QStringLiteral("retention"));
}

QString LearningDataStorage::datedJsonlPath(const QString &bucket, const QString &isoTimestamp) const
{
    const QDateTime dt = parseIsoOrNow(isoTimestamp);
    const QString year = dt.toString(QStringLiteral("yyyy"));
    const QString month = dt.toString(QStringLiteral("MM"));
    const QString day = dt.toString(QStringLiteral("dd"));
    return QDir(indexRoot()).filePath(
        QStringLiteral("%1/%2/%3/%4.jsonl").arg(bucket, year, month, day));
}

QString LearningDataStorage::asRelativePath(const QString &absolutePath) const
{
    return QDir(rootPath()).relativeFilePath(absolutePath);
}

bool LearningDataStorage::appendJsonLine(const QString &absolutePath, const QJsonObject &payload) const
{
    const QFileInfo info(absolutePath);
    if (!QDir().mkpath(info.absolutePath())) {
        return false;
    }

    QFile file(absolutePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }

    const QByteArray line = toJsonLine(payload);
    const qint64 written = file.write(line);
    if (written != line.size()) {
        return false;
    }

    file.flush();
    return true;
}

bool LearningDataStorage::appendTombstone(const QString &kind,
                                          const QString &reason,
                                          const QString &targetPath,
                                          qint64 sizeBytes,
                                          bool deleted) const
{
    const QJsonObject tombstone{
        {QStringLiteral("schema_version"), kSchemaVersion},
        {QStringLiteral("event_kind"), QStringLiteral("retention_tombstone")},
        {QStringLiteral("timestamp"), toIsoUtcNow()},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("reason"), reason},
        {QStringLiteral("target_path"), targetPath},
        {QStringLiteral("size_bytes"), sizeBytes},
        {QStringLiteral("deleted"), deleted}
    };
    const QString path = QDir(retentionRoot()).filePath(QStringLiteral("tombstones.jsonl"));
    return appendJsonLine(path, tombstone);
}

void LearningDataStorage::appendFailureRecord(const QString &kind,
                                              const QString &reason,
                                              const QJsonObject &payload) const
{
    QJsonObject failure{
        {QStringLiteral("schema_version"), kSchemaVersion},
        {QStringLiteral("event_kind"), QStringLiteral("write_failure")},
        {QStringLiteral("timestamp"), toIsoUtcNow()},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("reason"), reason}
    };
    failure.insert(QStringLiteral("payload"), payload);

    const QString path = QDir(quarantineRoot()).filePath(QStringLiteral("write_failures.jsonl"));
    if (!appendJsonLine(path, failure) && m_loggingService) {
        m_loggingService->errorFor(
            QStringLiteral("tool_audit"),
            QStringLiteral("[learning_data] failed to append failure record. kind=%1 reason=%2").arg(kind, reason));
    }
}

bool LearningDataStorage::writeSchemaVersionFile() const
{
    QSaveFile file(QDir(rootPath()).filePath(QStringLiteral("schema_version.json")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    const QJsonObject payload{
        {QStringLiteral("schema_version"), kSchemaVersion},
        {QStringLiteral("written_at"), toIsoUtcNow()},
        {QStringLiteral("app_name"), QCoreApplication::applicationName().trimmed().isEmpty()
             ? QStringLiteral("vaxil")
             : QCoreApplication::applicationName()}
    };

    file.write(QJsonDocument(payload).toJson(QJsonDocument::Indented));
    return file.commit();
}

QString LearningDataStorage::buildAudioFilePath(const AudioCaptureEvent &event) const
{
    const QDateTime timestamp = parseIsoOrNow(event.timestamp);
    const QString year = timestamp.toString(QStringLiteral("yyyy"));
    const QString month = timestamp.toString(QStringLiteral("MM"));

    const QString safeSession = QStringLiteral("session_%1").arg(
        sanitizeToken(event.sessionId, QStringLiteral("unknown")));
    const QString safeTurn = QStringLiteral("turn_%1").arg(
        sanitizeToken(event.turnId, QStringLiteral("unknown")));
    const QString safeRole = sanitizeToken(event.audioRole, QStringLiteral("audio"));
    const QString safeEvent = sanitizeToken(event.eventId, QUuid::createUuid().toString(QUuid::WithoutBraces));

    const QString fileName = QStringLiteral("%1_%2_%3_%4.wav")
                                 .arg(safeRole,
                                      sanitizeToken(event.sessionId, QStringLiteral("session")),
                                      timestamp.toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")),
                                      safeEvent);

    return QDir(audioRoot()).filePath(
        QStringLiteral("%1/%2/%3/%4/%5").arg(year, month, safeSession, safeTurn, fileName));
}

bool LearningDataStorage::writeWavFile(const QString &absolutePath,
                                       const QByteArray &pcmData,
                                       int sampleRate,
                                       int channels,
                                       QString *sha256Hex,
                                       qint64 *durationMs) const
{
    const int safeSampleRate = sampleRate > 0 ? sampleRate : 16000;
    const int safeChannels = channels > 0 ? channels : 1;
    const int bitsPerSample = 16;
    const int bytesPerSample = bitsPerSample / 8;

    if (!QDir().mkpath(QFileInfo(absolutePath).absolutePath())) {
        return false;
    }

    const quint32 dataSize = static_cast<quint32>(std::max<qsizetype>(0, pcmData.size()));
    const quint32 chunkSize = 36 + dataSize;
    const quint32 byteRate = static_cast<quint32>(safeSampleRate * safeChannels * bytesPerSample);
    const quint16 blockAlign = static_cast<quint16>(safeChannels * bytesPerSample);

    QByteArray payload;
    payload.reserve(static_cast<int>(44 + dataSize));

    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    stream.writeRawData("RIFF", 4);
    stream << chunkSize;
    stream.writeRawData("WAVE", 4);

    stream.writeRawData("fmt ", 4);
    stream << static_cast<quint32>(16);
    stream << static_cast<quint16>(1);
    stream << static_cast<quint16>(safeChannels);
    stream << static_cast<quint32>(safeSampleRate);
    stream << byteRate;
    stream << blockAlign;
    stream << static_cast<quint16>(bitsPerSample);

    stream.writeRawData("data", 4);
    stream << dataSize;
    payload.append(pcmData);

    QSaveFile file(absolutePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    if (file.write(payload) != payload.size()) {
        file.cancelWriting();
        return false;
    }

    if (!file.commit()) {
        return false;
    }

    if (sha256Hex) {
        *sha256Hex = QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
    }

    if (durationMs) {
        const qint64 bytesPerSecond = static_cast<qint64>(safeSampleRate) * safeChannels * bytesPerSample;
        *durationMs = bytesPerSecond > 0 ? (static_cast<qint64>(pcmData.size()) * 1000) / bytesPerSecond : 0;
    }

    return true;
}

QString LearningDataStorage::sanitizeToken(const QString &value, const QString &fallback)
{
    QString cleaned = value.trimmed();
    cleaned.replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_-]+")), QStringLiteral("_"));
    cleaned = cleaned.left(80);
    if (cleaned.isEmpty()) {
        return fallback;
    }
    return cleaned;
}

QDateTime LearningDataStorage::parseIsoOrNow(const QString &value)
{
    const QDateTime parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (parsed.isValid()) {
        return parsed.toUTC();
    }
    return QDateTime::currentDateTimeUtc();
}

QList<QJsonObject> LearningDataStorage::readAllJsonlObjects(const QString &rootDir)
{
    QList<QJsonObject> rows;
    QDirIterator it(rootDir, QStringList{QStringLiteral("*.jsonl")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFile file(it.next());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        while (!file.atEnd()) {
            const QByteArray line = file.readLine().trimmed();
            if (line.isEmpty()) {
                continue;
            }
            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (doc.isObject()) {
                rows.push_back(doc.object());
            }
        }
    }
    return rows;
}

bool LearningDataStorage::writeJsonlFile(const QString &absolutePath, const QList<QJsonObject> &rows)
{
    const QFileInfo info(absolutePath);
    if (!QDir().mkpath(info.absolutePath())) {
        return false;
    }

    QSaveFile file(absolutePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    for (const QJsonObject &row : rows) {
        const QByteArray payload = toJsonLine(row);
        if (file.write(payload) != payload.size()) {
            file.cancelWriting();
            return false;
        }
    }

    return file.commit();
}

bool LearningDataStorage::enforceAudioStorageLimitGb(double maxGb)
{
    const qint64 capBytes = static_cast<qint64>(std::max(0.1, maxGb) * 1024.0 * 1024.0 * 1024.0);

    struct AudioFile {
        QString path;
        qint64 size = 0;
        QDateTime modifiedUtc;
    };

    QList<AudioFile> files;
    qint64 totalBytes = 0;
    QDirIterator it(audioRoot(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        AudioFile file;
        file.path = info.absoluteFilePath();
        file.size = info.size();
        file.modifiedUtc = info.lastModified().toUTC();
        totalBytes += file.size;
        files.push_back(file);
    }

    std::sort(files.begin(), files.end(), [](const AudioFile &left, const AudioFile &right) {
        return left.modifiedUtc < right.modifiedUtc;
    });

    bool ok = true;
    for (const AudioFile &file : files) {
        if (totalBytes <= capBytes) {
            break;
        }
        const bool deleted = QFile::remove(file.path);
        appendTombstone(QStringLiteral("audio_file"),
                        QStringLiteral("audio_storage_cap_gb"),
                        asRelativePath(file.path),
                        file.size,
                        deleted);
        if (deleted) {
            totalBytes -= file.size;
        } else {
            ok = false;
            appendFailureRecord(QStringLiteral("retention"),
                                QStringLiteral("audio_storage_cap_delete_failed"),
                                QJsonObject{{QStringLiteral("path"), asRelativePath(file.path)}});
        }
    }

    return ok;
}

bool LearningDataStorage::removeFilesOlderThan(const QString &rootDir,
                                               const QDateTime &cutoffUtc,
                                               const QString &kind,
                                               const QString &reason)
{
    bool ok = true;
    QDirIterator it(rootDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        if (info.lastModified().toUTC() >= cutoffUtc) {
            continue;
        }

        const bool deleted = QFile::remove(info.absoluteFilePath());
        appendTombstone(kind,
                        reason,
                        asRelativePath(info.absoluteFilePath()),
                        info.size(),
                        deleted);
        if (!deleted) {
            ok = false;
            appendFailureRecord(QStringLiteral("retention"),
                                QStringLiteral("delete_failed"),
                                QJsonObject{{QStringLiteral("path"), asRelativePath(info.absoluteFilePath())}});
        }
    }
    return ok;
}

} // namespace LearningData

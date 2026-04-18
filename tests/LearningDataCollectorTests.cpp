#include <QtTest>

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>

#include "learning_data/LearningDataCollector.h"
#include "learning_data/LearningDataSettings.h"
#include "learning_data/LearningDataStorage.h"
#include "learning_data/LearningDataTypes.h"
#include "settings/AppSettings.h"

namespace {

LearningData::LearningDataSettingsSnapshot makeSnapshot(const QString &rootDir)
{
    LearningData::LearningDataSettingsSnapshot snapshot;
    snapshot.enabled = true;
    snapshot.audioCollectionEnabled = true;
    snapshot.transcriptCollectionEnabled = true;
    snapshot.toolLoggingEnabled = true;
    snapshot.behaviorLoggingEnabled = true;
    snapshot.memoryLoggingEnabled = true;
    snapshot.maxAudioStorageGb = 4.0;
    snapshot.maxDaysToKeepAudio = 30;
    snapshot.maxDaysToKeepStructuredLogs = 90;
    snapshot.allowPreparedDatasetExport = true;
    snapshot.rootDirectory = rootDir;
    return snapshot;
}

QList<QJsonObject> readJsonl(const QString &path)
{
    QList<QJsonObject> rows;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return rows;
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

    return rows;
}

QString firstJsonlFileUnder(const QString &rootDir)
{
    QDirIterator it(rootDir, QStringList{QStringLiteral("*.jsonl")}, QDir::Files, QDirIterator::Subdirectories);
    return it.hasNext() ? it.next() : QString();
}

QString newestExportDir(const QString &exportsRoot)
{
    QDir dir(exportsRoot);
    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (const QFileInfo &entry : entries) {
        if (entry.fileName().startsWith(QStringLiteral("export_"))) {
            return entry.absoluteFilePath();
        }
    }
    return QString();
}

} // namespace

class LearningDataCollectorTests : public QObject
{
    Q_OBJECT

private slots:
    void eventSerializationRoundTripIncludesSchemaVersion();
    void appendOnlyLogWritingPreservesRows();
    void wavCaptureWritesFileAndIndexEntry();
    void retentionRemovesOldFilesAndWritesTombstones();
    void disabledCollectorIsNoOp();
    void invalidRootWriteFailsSafely();
    void exportPreparedManifestsCreatesExpectedFiles();
};

void LearningDataCollectorTests::eventSerializationRoundTripIncludesSchemaVersion()
{
    LearningData::ToolDecisionEvent input;
    input.sessionId = QStringLiteral("session-1");
    input.turnId = QStringLiteral("1");
    input.eventId = QStringLiteral("tool_decision_1");
    input.timestamp = LearningData::toIsoUtcNow();
    input.userInputText = QStringLiteral("open calculator");
    input.inputMode = QStringLiteral("voice");
    input.availableTools = {QStringLiteral("calculator"), QStringLiteral("notepad")};
    input.selectedTool = QStringLiteral("calculator");
    input.candidateToolsWithScores = {
        LearningData::ToolCandidateScore{QStringLiteral("calculator"), 0.91},
        LearningData::ToolCandidateScore{QStringLiteral("notepad"), 0.32}
    };
    input.decisionSource = QStringLiteral("heuristic");
    input.expectedConfirmationLevel = QStringLiteral("none");

    const QJsonObject json = LearningData::toJson(input);
    QVERIFY(json.contains(QStringLiteral("schema_version")));
    QCOMPARE(json.value(QStringLiteral("schema_version")).toString(), LearningData::kSchemaVersion);

    const LearningData::ToolDecisionEvent output = LearningData::toolDecisionEventFromJson(json);
    QCOMPARE(output.schemaVersion, LearningData::kSchemaVersion);
    QCOMPARE(output.sessionId, input.sessionId);
    QCOMPARE(output.turnId, input.turnId);
    QCOMPARE(output.eventId, input.eventId);
    QCOMPARE(output.selectedTool, input.selectedTool);
    QCOMPARE(output.availableTools.size(), input.availableTools.size());
    QCOMPARE(output.candidateToolsWithScores.size(), input.candidateToolsWithScores.size());
}

void LearningDataCollectorTests::appendOnlyLogWritingPreservesRows()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString root = QDir(dir.path()).filePath(QStringLiteral("learning"));
    LearningData::LearningDataStorage storage(root, nullptr);
    const LearningData::LearningDataSettingsSnapshot snapshot = makeSnapshot(root);
    QVERIFY(storage.initialize(snapshot));

    LearningData::ToolDecisionEvent decisionA;
    decisionA.sessionId = QStringLiteral("session-a");
    decisionA.turnId = QStringLiteral("1");
    decisionA.eventId = QStringLiteral("event-a");
    decisionA.timestamp = LearningData::toIsoUtcNow();
    decisionA.selectedTool = QStringLiteral("tool-a");

    LearningData::ToolDecisionEvent decisionB = decisionA;
    decisionB.eventId = QStringLiteral("event-b");
    decisionB.selectedTool = QStringLiteral("tool-b");

    QVERIFY(storage.writeToolDecisionEvent(decisionA));
    QVERIFY(storage.writeToolDecisionEvent(decisionB));

    const QString logFile = firstJsonlFileUnder(QDir(root).filePath(QStringLiteral("index/tool_decision_events")));
    QVERIFY(!logFile.isEmpty());

    const QList<QJsonObject> rows = readJsonl(logFile);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(0).value(QStringLiteral("event_id")).toString(), QStringLiteral("event-a"));
    QCOMPARE(rows.at(1).value(QStringLiteral("event_id")).toString(), QStringLiteral("event-b"));
    QCOMPARE(rows.at(0).value(QStringLiteral("schema_version")).toString(), LearningData::kSchemaVersion);
    QCOMPARE(rows.at(1).value(QStringLiteral("schema_version")).toString(), LearningData::kSchemaVersion);
}

void LearningDataCollectorTests::wavCaptureWritesFileAndIndexEntry()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString root = QDir(dir.path()).filePath(QStringLiteral("learning"));
    LearningData::LearningDataStorage storage(root, nullptr);
    const LearningData::LearningDataSettingsSnapshot snapshot = makeSnapshot(root);
    QVERIFY(storage.initialize(snapshot));

    QByteArray pcmData;
    pcmData.resize(16000 * 2);
    for (int i = 0; i < pcmData.size(); ++i) {
        pcmData[i] = static_cast<char>(i % 127);
    }

    LearningData::AudioCaptureEvent event;
    event.sessionId = QStringLiteral("session-a");
    event.turnId = QStringLiteral("1");
    event.eventId = QStringLiteral("audio-a");
    event.timestamp = LearningData::toIsoUtcNow();
    event.audioRole = QStringLiteral("command_raw");
    event.sampleRate = 16000;
    event.channels = 1;
    event.sampleFormat = QStringLiteral("pcm_s16le");

    QVERIFY(storage.writeAudioCaptureEvent(event, pcmData));

    const QString logFile = firstJsonlFileUnder(QDir(root).filePath(QStringLiteral("index/audio_index")));
    QVERIFY(!logFile.isEmpty());

    const QList<QJsonObject> rows = readJsonl(logFile);
    QCOMPARE(rows.size(), 1);
    const QJsonObject row = rows.first();
    QCOMPARE(row.value(QStringLiteral("schema_version")).toString(), LearningData::kSchemaVersion);

    const QString relativePath = row.value(QStringLiteral("file_path")).toString();
    QVERIFY(!relativePath.isEmpty());
    const QString absolutePath = QDir(root).filePath(relativePath);
    QVERIFY(QFileInfo::exists(absolutePath));

    QFile wavFile(absolutePath);
    QVERIFY(wavFile.open(QIODevice::ReadOnly));
    const QByteArray prefix = wavFile.read(4);
    QCOMPARE(prefix, QByteArray("RIFF"));
}

void LearningDataCollectorTests::retentionRemovesOldFilesAndWritesTombstones()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString root = QDir(dir.path()).filePath(QStringLiteral("learning"));
    LearningData::LearningDataStorage storage(root, nullptr);
    LearningData::LearningDataSettingsSnapshot snapshot = makeSnapshot(root);
    snapshot.maxDaysToKeepAudio = 1;
    snapshot.maxDaysToKeepStructuredLogs = 1;
    QVERIFY(storage.initialize(snapshot));

    const QString oldAudioPath = QDir(root).filePath(QStringLiteral("audio/1999/01/session_old/turn_1/old.wav"));
    QDir().mkpath(QFileInfo(oldAudioPath).absolutePath());
    {
        QFile file(oldAudioPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("deadbeef");
    }

    const QString oldLogPath = QDir(root).filePath(QStringLiteral("index/asr_events/1999/01/01.jsonl"));
    QDir().mkpath(QFileInfo(oldLogPath).absolutePath());
    {
        QFile file(oldLogPath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("{}\n");
    }

    const QDateTime veryOld = QDateTime::currentDateTimeUtc().addDays(-90);
    QFile oldAudio(oldAudioPath);
    QVERIFY(oldAudio.setFileTime(veryOld, QFileDevice::FileModificationTime));
    QFile oldLog(oldLogPath);
    QVERIFY(oldLog.setFileTime(veryOld, QFileDevice::FileModificationTime));

    QVERIFY(storage.runRetention(snapshot));

    QVERIFY(!QFileInfo::exists(oldAudioPath));
    QVERIFY(!QFileInfo::exists(oldLogPath));

    const QString tombstonesPath = QDir(root).filePath(QStringLiteral("retention/tombstones.jsonl"));
    QVERIFY(QFileInfo::exists(tombstonesPath));
    QVERIFY(readJsonl(tombstonesPath).size() >= 2);
}

void LearningDataCollectorTests::disabledCollectorIsNoOp()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    AppSettings settings;
    const QString root = QDir(dir.path()).filePath(QStringLiteral("learning"));
    LearningData::LearningDataCollector collector(&settings, nullptr, root);
    collector.initialize();

    LearningData::ToolDecisionEvent event;
    event.sessionId = QStringLiteral("session-disabled");
    event.turnId = QStringLiteral("1");
    event.eventId = QStringLiteral("disabled-event");
    event.timestamp = LearningData::toIsoUtcNow();
    event.selectedTool = QStringLiteral("tool");

    collector.recordToolDecisionEvent(event);
    collector.waitForIdle();

    QVERIFY(!QDir(root).exists());
}

void LearningDataCollectorTests::invalidRootWriteFailsSafely()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString invalidRoot = QDir(dir.path()).filePath(QStringLiteral("blocked_root"));
    {
        QFile file(invalidRoot);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not_a_directory");
    }

    LearningData::LearningDataStorage storage(invalidRoot, nullptr);
    const LearningData::LearningDataSettingsSnapshot snapshot = makeSnapshot(invalidRoot);

    QVERIFY(!storage.initialize(snapshot));

    LearningData::AsrEvent event;
    event.sessionId = QStringLiteral("session");
    event.turnId = QStringLiteral("1");
    event.eventId = QStringLiteral("asr-1");
    event.timestamp = LearningData::toIsoUtcNow();
    event.rawTranscript = QStringLiteral("hello");

    QVERIFY(!storage.writeAsrEvent(event));
}

void LearningDataCollectorTests::exportPreparedManifestsCreatesExpectedFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString root = QDir(dir.path()).filePath(QStringLiteral("learning"));
    LearningData::LearningDataStorage storage(root, nullptr);
    LearningData::LearningDataSettingsSnapshot snapshot = makeSnapshot(root);
    snapshot.allowPreparedDatasetExport = true;
    QVERIFY(storage.initialize(snapshot));

    QByteArray pcmData;
    pcmData.resize(3200);
    pcmData.fill('\x01');

    LearningData::AudioCaptureEvent audio;
    audio.sessionId = QStringLiteral("session-export");
    audio.turnId = QStringLiteral("1");
    audio.eventId = QStringLiteral("audio-export");
    audio.timestamp = LearningData::toIsoUtcNow();
    audio.audioRole = QStringLiteral("command_raw");
    QVERIFY(storage.writeAudioCaptureEvent(audio, pcmData));

    LearningData::AsrEvent asr;
    asr.sessionId = audio.sessionId;
    asr.turnId = audio.turnId;
    asr.eventId = QStringLiteral("asr-export");
    asr.timestamp = LearningData::toIsoUtcNow();
    asr.sourceAudioEventId = audio.eventId;
    asr.rawTranscript = QStringLiteral("turn on the lights");
    asr.normalizedTranscript = QStringLiteral("turn on the lights");
    asr.finalTranscript = QStringLiteral("turn on the lights");
    QVERIFY(storage.writeAsrEvent(asr));

    LearningData::ToolDecisionEvent toolDecision;
    toolDecision.sessionId = audio.sessionId;
    toolDecision.turnId = audio.turnId;
    toolDecision.eventId = QStringLiteral("tool-decision-export");
    toolDecision.timestamp = LearningData::toIsoUtcNow();
    toolDecision.selectedTool = QStringLiteral("home_automation");
    toolDecision.availableTools = {QStringLiteral("home_automation")};
    QVERIFY(storage.writeToolDecisionEvent(toolDecision));

    LearningData::ToolExecutionEvent toolExecution;
    toolExecution.sessionId = audio.sessionId;
    toolExecution.turnId = audio.turnId;
    toolExecution.eventId = QStringLiteral("tool-exec-export");
    toolExecution.timestamp = LearningData::toIsoUtcNow();
    toolExecution.selectedTool = QStringLiteral("home_automation");
    toolExecution.succeeded = true;
    toolExecution.finalOutcomeLabel = QStringLiteral("good");
    QVERIFY(storage.writeToolExecutionEvent(toolExecution));

    LearningData::BehaviorDecisionEvent behavior;
    behavior.sessionId = audio.sessionId;
    behavior.turnId = audio.turnId;
    behavior.eventId = QStringLiteral("behavior-export");
    behavior.timestamp = LearningData::toIsoUtcNow();
    behavior.responseMode = QStringLiteral("concise_report");
    QVERIFY(storage.writeBehaviorDecisionEvent(behavior));

    LearningData::MemoryDecisionEvent memory;
    memory.sessionId = audio.sessionId;
    memory.turnId = audio.turnId;
    memory.eventId = QStringLiteral("memory-export");
    memory.timestamp = LearningData::toIsoUtcNow();
    memory.memoryAction = QStringLiteral("save");
    QVERIFY(storage.writeMemoryDecisionEvent(memory));

    LearningData::UserFeedbackEvent feedback;
    feedback.sessionId = audio.sessionId;
    feedback.turnId = audio.turnId;
    feedback.eventId = QStringLiteral("feedback-export");
    feedback.timestamp = LearningData::toIsoUtcNow();
    feedback.feedbackType = QStringLiteral("explicit_positive");
    QVERIFY(storage.writeUserFeedbackEvent(feedback));

    QVERIFY(storage.exportPreparedManifests(snapshot));

    const QString exportDir = newestExportDir(QDir(root).filePath(QStringLiteral("exports")));
    QVERIFY(!exportDir.isEmpty());

    QVERIFY(QFileInfo::exists(QDir(exportDir).filePath(QStringLiteral("export_audio_manifest.jsonl"))));
    QVERIFY(QFileInfo::exists(QDir(exportDir).filePath(QStringLiteral("export_tool_policy_manifest.jsonl"))));
    QVERIFY(QFileInfo::exists(QDir(exportDir).filePath(QStringLiteral("export_behavior_policy_manifest.jsonl"))));
    QVERIFY(QFileInfo::exists(QDir(exportDir).filePath(QStringLiteral("export_memory_policy_manifest.jsonl"))));

    QVERIFY(!readJsonl(QDir(exportDir).filePath(QStringLiteral("export_audio_manifest.jsonl"))).isEmpty());
    QVERIFY(!readJsonl(QDir(exportDir).filePath(QStringLiteral("export_tool_policy_manifest.jsonl"))).isEmpty());
}

QTEST_APPLESS_MAIN(LearningDataCollectorTests)
#include "LearningDataCollectorTests.moc"

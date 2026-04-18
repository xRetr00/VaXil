#include "learning_data/LearningDataStorage.h"

#include <algorithm>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QStringConverter>
#include <QTextStream>

#include "logging/LoggingService.h"

namespace LearningData {

namespace {

QDateTime parseTimestamp(const QString &timestamp)
{
    QDateTime parsed = QDateTime::fromString(timestamp, Qt::ISODateWithMs);
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(timestamp, Qt::ISODate);
    }
    if (!parsed.isValid()) {
        parsed = QDateTime::currentDateTimeUtc();
    }
    return parsed.toUTC();
}

QString toIsoNow()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

void writeWaveHeader(QFile &file, quint32 pcmSize, int sampleRate, int channels)
{
    const quint16 bitsPerSample = 16;
    const quint32 byteRate = static_cast<quint32>(sampleRate * channels * bitsPerSample / 8);
    const quint16 blockAlign = static_cast<quint16>(channels * bitsPerSample / 8);
    const quint32 chunkSize = 36 + pcmSize;
    const quint32 subChunkSize = 16;
    const quint16 audioFormat = 1;
    const quint16 safeChannels = static_cast<quint16>(std::clamp(channels, 1, 2));
    const quint32 safeSampleRate = static_cast<quint32>(std::max(sampleRate, 8000));

    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char *>(&chunkSize), 4);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    file.write(reinterpret_cast<const char *>(&subChunkSize), 4);
    file.write(reinterpret_cast<const char *>(&audioFormat), 2);
    file.write(reinterpret_cast<const char *>(&safeChannels), 2);
    file.write(reinterpret_cast<const char *>(&safeSampleRate), 4);
    file.write(reinterpret_cast<const char *>(&byteRate), 4);
    file.write(reinterpret_cast<const char *>(&blockAlign), 2);
    file.write(reinterpret_cast<const char *>(&bitsPerSample), 2);
    file.write("data", 4);
    file.write(reinterpret_cast<const char *>(&pcmSize), 4);
}

} // namespace

LearningDataStorage::LearningDataStorage(QString rootPath, LoggingService *loggingService)
    : m_rootPath(rootPath.trimmed())
    , m_loggingService(loggingService)
{
    if (m_rootPath.isEmpty()) {
        m_rootPath = defaultRootPath();
    }
}

bool LearningDataStorage::initialize()
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLayout()) {
        return false;
    }
    return writeSchemaVersionFile();
}

QString LearningDataStorage::rootPath() const
{
    return m_rootPath;
}

bool LearningDataStorage::appendIndexEvent(const QString &stream,
                                           const QString &timestamp,
                                           const QJsonObject &event,
                                           QString *relativePathOut)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLayout()) {
        return false;
    }

    QJsonObject line = event;
    if (!line.contains(QStringLiteral("schema_version"))) {
        line.insert(QStringLiteral("schema_version"), defaultSchemaVersion());
    }

    const QString relativePath = indexDatePath(stream, timestamp);
    if (!appendJsonLine(relativePath, line)) {
        logStorageIssue(QStringLiteral("appendIndexEvent"), relativePath, QStringLiteral("Unable to append JSONL line"));
        return false;
    }

    if (relativePathOut != nullptr) {
        *relativePathOut = relativePath;
    }
    return true;
}

bool LearningDataStorage::appendExportRecord(const QString &manifestName,
                                             const QJsonObject &record,
                                             QString *relativePathOut)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLayout()) {
        return false;
    }

    QJsonObject line = record;
    if (!line.contains(QStringLiteral("schema_version"))) {
        line.insert(QStringLiteral("schema_version"), defaultSchemaVersion());
    }

    const QString safeName = manifestName.trimmed().isEmpty()
        ? QStringLiteral("export_unknown_manifest")
        : manifestName.trimmed();
    const QString relativePath = QStringLiteral("exports/%1.jsonl").arg(safeName);
    if (!appendJsonLine(relativePath, line)) {
        logStorageIssue(QStringLiteral("appendExportRecord"), relativePath, QStringLiteral("Unable to append export record"));
        return false;
    }

    if (relativePathOut != nullptr) {
        *relativePathOut = relativePath;
    }
    return true;
}

QString LearningDataStorage::writeAudioClip(const QString &sessionId,
                                            const QString &turnId,
                                            const QString &eventId,
                                            const QString &fileStem,
                                            const QByteArray &pcmData,
                                            int sampleRate,
                                            int channels,
                                            QString *sha256Out,
                                            qint64 *durationMsOut)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLayout()) {
        return {};
    }

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    const QString year = nowUtc.toString(QStringLiteral("yyyy"));
    const QString month = nowUtc.toString(QStringLiteral("MM"));
    const QString safeSession = sessionId.trimmed().isEmpty() ? QStringLiteral("unknown") : sessionId.trimmed();
    const QString safeTurn = turnId.trimmed().isEmpty() ? QStringLiteral("0") : turnId.trimmed();
    const QString safeStem = fileStem.trimmed().isEmpty() ? QStringLiteral("command_raw") : fileStem.trimmed();
    const QString safeEvent = eventId.trimmed().isEmpty() ? QStringLiteral("evt") : eventId.left(10);
    const QString fileName = QStringLiteral("%1_%2_%3.wav")
        .arg(safeStem, nowUtc.toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")), safeEvent);

    const QString relativePath = QStringLiteral("audio/%1/%2/session_%3/turn_%4/%5")
        .arg(year, month, safeSession, safeTurn, fileName);
    const QString absolutePath = QDir(m_rootPath).filePath(relativePath);

    const QFileInfo parentInfo(absolutePath);
    QDir().mkpath(parentInfo.absolutePath());
    if (!writeWaveFile(absolutePath, pcmData, sampleRate, channels)) {
        logStorageIssue(QStringLiteral("writeAudioClip"), absolutePath, QStringLiteral("Unable to write WAV audio clip"));
        return {};
    }

    if (sha256Out != nullptr) {
        *sha256Out = sha256Hex(pcmData);
    }

    if (durationMsOut != nullptr) {
        const int effectiveChannels = std::max(1, channels);
        const int bytesPerSampleFrame = effectiveChannels * static_cast<int>(sizeof(qint16));
        const int totalFrames = bytesPerSampleFrame > 0 ? (pcmData.size() / bytesPerSampleFrame) : 0;
        *durationMsOut = sampleRate > 0
            ? static_cast<qint64>((static_cast<double>(totalFrames) * 1000.0) / static_cast<double>(sampleRate))
            : 0;
    }

    return relativePath;
}

bool LearningDataStorage::cleanupRetention(const SettingsSnapshot &settings)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLayout()) {
        return false;
    }

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    const QDateTime audioCutoff = settings.maxDaysToKeepAudio > 0
        ? nowUtc.addDays(-settings.maxDaysToKeepAudio)
        : QDateTime{};
    const QDate logsCutoffDate = settings.maxDaysToKeepStructuredLogs > 0
        ? nowUtc.date().addDays(-settings.maxDaysToKeepStructuredLogs)
        : QDate{};

    const QString audioRoot = QDir(m_rootPath).filePath(QStringLiteral("audio"));
    QDirIterator audioIt(audioRoot,
                         QStringList{QStringLiteral("*.wav")},
                         QDir::Files,
                         QDirIterator::Subdirectories);

    QList<QFileInfo> audioFiles;
    while (audioIt.hasNext()) {
        audioIt.next();
        audioFiles.push_back(audioIt.fileInfo());
    }

    for (const QFileInfo &fileInfo : audioFiles) {
        if (audioCutoff.isValid() && fileInfo.lastModified().toUTC() < audioCutoff) {
            const QString relative = QDir(m_rootPath).relativeFilePath(fileInfo.absoluteFilePath());
            QFile::remove(fileInfo.absoluteFilePath());
            appendIndexEvent(
                QStringLiteral("retention_events"),
                toIsoNow(),
                {
                    {QStringLiteral("schema_version"), defaultSchemaVersion()},
                    {QStringLiteral("timestamp"), toIsoNow()},
                    {QStringLiteral("action"), QStringLiteral("delete")},
                    {QStringLiteral("target"), relative},
                    {QStringLiteral("reason"), QStringLiteral("audio_age_expired")}
                });
        }
    }

    qint64 totalAudioBytes = 0;
    audioFiles.clear();
    QDirIterator audioAfterAgeIt(audioRoot,
                                 QStringList{QStringLiteral("*.wav")},
                                 QDir::Files,
                                 QDirIterator::Subdirectories);
    while (audioAfterAgeIt.hasNext()) {
        audioAfterAgeIt.next();
        const QFileInfo info = audioAfterAgeIt.fileInfo();
        audioFiles.push_back(info);
        totalAudioBytes += info.size();
    }

    const qint64 maxAudioBytes = settings.maxAudioStorageGb > 0.0
        ? static_cast<qint64>(settings.maxAudioStorageGb * 1024.0 * 1024.0 * 1024.0)
        : 0;
    if (maxAudioBytes > 0 && totalAudioBytes > maxAudioBytes) {
        std::sort(audioFiles.begin(), audioFiles.end(), [](const QFileInfo &left, const QFileInfo &right) {
            return left.lastModified() < right.lastModified();
        });

        for (const QFileInfo &fileInfo : audioFiles) {
            if (totalAudioBytes <= maxAudioBytes) {
                break;
            }
            const QString relative = QDir(m_rootPath).relativeFilePath(fileInfo.absoluteFilePath());
            totalAudioBytes -= fileInfo.size();
            QFile::remove(fileInfo.absoluteFilePath());
            appendIndexEvent(
                QStringLiteral("retention_events"),
                toIsoNow(),
                {
                    {QStringLiteral("schema_version"), defaultSchemaVersion()},
                    {QStringLiteral("timestamp"), toIsoNow()},
                    {QStringLiteral("action"), QStringLiteral("delete")},
                    {QStringLiteral("target"), relative},
                    {QStringLiteral("reason"), QStringLiteral("audio_storage_cap")}
                });
        }
    }

    const QString indexRoot = QDir(m_rootPath).filePath(QStringLiteral("index"));
    QDirIterator indexIt(indexRoot,
                         QStringList{QStringLiteral("*.jsonl")},
                         QDir::Files,
                         QDirIterator::Subdirectories);
    while (indexIt.hasNext()) {
        indexIt.next();
        const QFileInfo info = indexIt.fileInfo();
        const QString streamName = info.dir().dirName();
        if (streamName == QStringLiteral("retention_events")) {
            continue;
        }

        const QString baseName = info.completeBaseName();
        const QDate date = QDate::fromString(baseName, Qt::ISODate);
        if (!date.isValid() || !logsCutoffDate.isValid() || date >= logsCutoffDate) {
            continue;
        }

        const QString relative = QDir(m_rootPath).relativeFilePath(info.absoluteFilePath());
        QFile::remove(info.absoluteFilePath());
        appendIndexEvent(
            QStringLiteral("retention_events"),
            toIsoNow(),
            {
                {QStringLiteral("schema_version"), defaultSchemaVersion()},
                {QStringLiteral("timestamp"), toIsoNow()},
                {QStringLiteral("action"), QStringLiteral("delete")},
                {QStringLiteral("target"), relative},
                {QStringLiteral("reason"), QStringLiteral("structured_log_age_expired")}
            });
    }

    cleanupEmptyDirectories(audioRoot, QDir(m_rootPath).filePath(QStringLiteral("audio")));
    cleanupEmptyDirectories(indexRoot, QDir(m_rootPath).filePath(QStringLiteral("index")));
    return true;
}

DiagnosticsSnapshot LearningDataStorage::diagnosticsSnapshot() const
{
    QMutexLocker locker(&m_mutex);

    DiagnosticsSnapshot snapshot;
    snapshot.approximateDiskUsageBytes = directorySizeBytes(m_rootPath);

    const QString sessionsRoot = QDir(m_rootPath).filePath(QStringLiteral("index/sessions"));
    QDirIterator sessionsIt(sessionsRoot,
                            QStringList{QStringLiteral("*.jsonl")},
                            QDir::Files,
                            QDirIterator::Subdirectories);
    while (sessionsIt.hasNext()) {
        sessionsIt.next();
        snapshot.totalSessions += countLinesInFile(sessionsIt.filePath());
    }

    const QString audioRoot = QDir(m_rootPath).filePath(QStringLiteral("audio"));
    QDirIterator audioIt(audioRoot,
                         QStringList{QStringLiteral("*.wav")},
                         QDir::Files,
                         QDirIterator::Subdirectories);
    while (audioIt.hasNext()) {
        audioIt.next();
        ++snapshot.totalAudioClips;
    }

    const auto countStream = [this](const QString &stream) {
        qint64 lines = 0;
        const QString root = QDir(m_rootPath).filePath(QStringLiteral("index/%1").arg(stream));
        QDirIterator it(root,
                        QStringList{QStringLiteral("*.jsonl")},
                        QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            lines += countLinesInFile(it.filePath());
        }
        return lines;
    };

    snapshot.toolDecisionsLogged = countStream(QStringLiteral("tool_decisions"));
    snapshot.behaviorDecisionsLogged = countStream(QStringLiteral("behavior_events"));
    snapshot.memoryDecisionsLogged = countStream(QStringLiteral("memory_events"));

    const QString feedbackRoot = QDir(m_rootPath).filePath(QStringLiteral("index/feedback_events"));
    QDirIterator feedbackIt(feedbackRoot,
                            QStringList{QStringLiteral("*.jsonl")},
                            QDir::Files,
                            QDirIterator::Subdirectories);
    while (feedbackIt.hasNext()) {
        feedbackIt.next();
        QFile file(feedbackIt.filePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        QTextStream stream(&file);
        stream.setEncoding(QStringConverter::Utf8);
        while (!stream.atEnd()) {
            const QString line = stream.readLine().trimmed();
            if (line.isEmpty()) {
                continue;
            }

            const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
            if (!doc.isObject()) {
                continue;
            }
            const QString feedbackType = doc.object().value(QStringLiteral("feedback_type")).toString().toLower();
            if (feedbackType.contains(QStringLiteral("correction"))
                || feedbackType.contains(QStringLiteral("wrong_"))
                || feedbackType == QStringLiteral("too_verbose")
                || feedbackType == QStringLiteral("wrong_tool")
                || feedbackType == QStringLiteral("wrong_memory")
                || feedbackType == QStringLiteral("wrong_transcript")) {
                ++snapshot.explicitFeedbackCorrections;
            }
        }
    }

    return snapshot;
}

QString LearningDataStorage::defaultRootPath() const
{
    const QString baseRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(baseRoot).filePath(QStringLiteral("data/learning"));
}

bool LearningDataStorage::ensureLayout() const
{
    const QStringList paths{
        m_rootPath,
        QDir(m_rootPath).filePath(QStringLiteral("index")),
        QDir(m_rootPath).filePath(QStringLiteral("audio")),
        QDir(m_rootPath).filePath(QStringLiteral("snapshots")),
        QDir(m_rootPath).filePath(QStringLiteral("exports")),
        QDir(m_rootPath).filePath(QStringLiteral("quarantine")),
        QDir(m_rootPath).filePath(QStringLiteral("retention")),
        QDir(m_rootPath).filePath(QStringLiteral("index/sessions")),
        QDir(m_rootPath).filePath(QStringLiteral("index/audio_events")),
        QDir(m_rootPath).filePath(QStringLiteral("index/asr_events")),
        QDir(m_rootPath).filePath(QStringLiteral("index/tool_decisions")),
        QDir(m_rootPath).filePath(QStringLiteral("index/tool_executions")),
        QDir(m_rootPath).filePath(QStringLiteral("index/behavior_events")),
        QDir(m_rootPath).filePath(QStringLiteral("index/memory_events")),
        QDir(m_rootPath).filePath(QStringLiteral("index/feedback_events")),
        QDir(m_rootPath).filePath(QStringLiteral("index/retention_events"))
    };

    for (const QString &path : paths) {
        if (!QDir().mkpath(path)) {
            return false;
        }
    }

    return true;
}

bool LearningDataStorage::appendJsonLine(const QString &relativePath,
                                         const QJsonObject &lineObject,
                                         QString *absolutePathOut) const
{
    const QString absolutePath = QDir(m_rootPath).filePath(relativePath);
    const QFileInfo info(absolutePath);
    if (!QDir().mkpath(info.absolutePath())) {
        return false;
    }

    const bool ok = appendRawJsonLine(absolutePath, lineObject);
    if (absolutePathOut != nullptr) {
        *absolutePathOut = absolutePath;
    }
    return ok;
}

bool LearningDataStorage::appendRawJsonLine(const QString &absolutePath, const QJsonObject &lineObject) const
{
    QFile file(absolutePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    const QByteArray jsonLine = QJsonDocument(lineObject).toJson(QJsonDocument::Compact);
    stream << jsonLine << '\n';
    stream.flush();
    const bool ok = stream.status() == QTextStream::Ok;
    file.close();
    return ok;
}

bool LearningDataStorage::writeSchemaVersionFile() const
{
    const QString schemaPath = QDir(m_rootPath).filePath(QStringLiteral("schema_version.json"));
    QFile file(schemaPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }

    const QJsonObject schemaObj{
        {QStringLiteral("schema_version"), defaultSchemaVersion()},
        {QStringLiteral("updated_at"), toIsoNow()},
        {QStringLiteral("layout_version"), QStringLiteral("learning_data_v1")}
    };
    file.write(QJsonDocument(schemaObj).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

QString LearningDataStorage::indexDatePath(const QString &stream, const QString &timestamp) const
{
    const QString safeStream = stream.trimmed().isEmpty() ? QStringLiteral("events") : stream.trimmed();
    const QDateTime dateTime = parseTimestamp(timestamp);
    return QStringLiteral("index/%1/%2.jsonl").arg(safeStream, dateTime.date().toString(Qt::ISODate));
}

QString LearningDataStorage::timestampForFileName(const QString &timestamp) const
{
    const QDateTime dateTime = parseTimestamp(timestamp);
    return dateTime.toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
}

qint64 LearningDataStorage::countLinesInFile(const QString &absolutePath)
{
    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 0;
    }

    qint64 count = 0;
    while (!file.atEnd()) {
        file.readLine();
        ++count;
    }
    return count;
}

qint64 LearningDataStorage::directorySizeBytes(const QString &absolutePath)
{
    qint64 total = 0;
    QDirIterator it(absolutePath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

QString LearningDataStorage::sha256Hex(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool LearningDataStorage::writeWaveFile(const QString &absolutePath,
                                        const QByteArray &pcmData,
                                        int sampleRate,
                                        int channels)
{
    QFile file(absolutePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    writeWaveHeader(file, static_cast<quint32>(pcmData.size()), sampleRate, channels);
    const qint64 written = file.write(pcmData);
    file.close();
    return written == pcmData.size();
}

void LearningDataStorage::logStorageIssue(const QString &operation,
                                          const QString &path,
                                          const QString &error) const
{
    if (m_loggingService != nullptr) {
        m_loggingService->warnFor(
            QStringLiteral("tools_mcp"),
            QStringLiteral("[learning_data] operation=%1 path=%2 error=%3")
                .arg(operation, path, error));
    }

    const QString quarantineDir = QDir(m_rootPath).filePath(QStringLiteral("quarantine"));
    QDir().mkpath(quarantineDir);
    const QString quarantineFile = QDir(quarantineDir).filePath(QStringLiteral("write_failures.jsonl"));
    appendRawJsonLine(
        quarantineFile,
        {
            {QStringLiteral("schema_version"), defaultSchemaVersion()},
            {QStringLiteral("timestamp"), toIsoNow()},
            {QStringLiteral("operation"), operation},
            {QStringLiteral("path"), path},
            {QStringLiteral("error"), error}
        });
}

void LearningDataStorage::cleanupEmptyDirectories(const QString &absolutePath,
                                                  const QString &stopAtAbsolutePath) const
{
    QDir root(absolutePath);
    if (!root.exists()) {
        return;
    }

    QDirIterator it(absolutePath,
                    QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    QStringList directories;
    while (it.hasNext()) {
        it.next();
        directories.push_back(it.filePath());
    }

    std::sort(directories.begin(), directories.end(), [](const QString &left, const QString &right) {
        return left.size() > right.size();
    });

    const QString stopAt = QDir::cleanPath(stopAtAbsolutePath);
    for (const QString &directoryPath : directories) {
        const QString normalized = QDir::cleanPath(directoryPath);
        if (normalized == stopAt) {
            continue;
        }

        QDir dir(normalized);
        const QFileInfoList entries = dir.entryInfoList(
            QDir::NoDotAndDotDot | QDir::AllEntries,
            QDir::Name | QDir::DirsFirst);
        if (entries.isEmpty()) {
            dir.rmdir(normalized);
        }
    }
}

} // namespace LearningData

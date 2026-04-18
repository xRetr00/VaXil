#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QMutex>
#include <QString>

#include "learning_data/LearningDataSettings.h"
#include "learning_data/LearningDataTypes.h"

class LoggingService;

namespace LearningData {

class LearningDataStorage
{
public:
    explicit LearningDataStorage(QString rootPath = QString(), LoggingService *loggingService = nullptr);

    [[nodiscard]] bool initialize();
    [[nodiscard]] QString rootPath() const;

    [[nodiscard]] bool appendIndexEvent(const QString &stream,
                                        const QString &timestamp,
                                        const QJsonObject &event,
                                        QString *relativePathOut = nullptr);

    [[nodiscard]] bool appendExportRecord(const QString &manifestName,
                                          const QJsonObject &record,
                                          QString *relativePathOut = nullptr);

    [[nodiscard]] QString writeAudioClip(const QString &sessionId,
                                         const QString &turnId,
                                         const QString &eventId,
                                         const QString &fileStem,
                                         const QByteArray &pcmData,
                                         int sampleRate,
                                         int channels,
                                         QString *sha256Out = nullptr,
                                         qint64 *durationMsOut = nullptr);

    [[nodiscard]] bool cleanupRetention(const SettingsSnapshot &settings);
    [[nodiscard]] DiagnosticsSnapshot diagnosticsSnapshot() const;

private:
    [[nodiscard]] QString defaultRootPath() const;
    [[nodiscard]] bool ensureLayout() const;
    [[nodiscard]] bool appendJsonLine(const QString &relativePath,
                                      const QJsonObject &lineObject,
                                      QString *absolutePathOut = nullptr) const;
    [[nodiscard]] bool appendRawJsonLine(const QString &absolutePath, const QJsonObject &lineObject) const;
    [[nodiscard]] bool writeSchemaVersionFile() const;
    [[nodiscard]] QString indexDatePath(const QString &stream, const QString &timestamp) const;
    [[nodiscard]] QString timestampForFileName(const QString &timestamp) const;
    [[nodiscard]] static qint64 countLinesInFile(const QString &absolutePath);
    [[nodiscard]] static qint64 directorySizeBytes(const QString &absolutePath);
    [[nodiscard]] static QString sha256Hex(const QByteArray &bytes);
    [[nodiscard]] static bool writeWaveFile(const QString &absolutePath,
                                            const QByteArray &pcmData,
                                            int sampleRate,
                                            int channels);

    void logStorageIssue(const QString &operation,
                         const QString &path,
                         const QString &error) const;
    void cleanupEmptyDirectories(const QString &absolutePath, const QString &stopAtAbsolutePath) const;

    QString m_rootPath;
    LoggingService *m_loggingService = nullptr;
    mutable QMutex m_mutex;
};

} // namespace LearningData

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include "learning_data/LearningDataSettings.h"
#include "learning_data/LearningDataTypes.h"

class LoggingService;

namespace LearningData {

class LearningDataStorage
{
public:
    explicit LearningDataStorage(QString rootPath = QString(),
                                 LoggingService *loggingService = nullptr);

    bool initialize(const LearningDataSettingsSnapshot &settings);

    bool writeSessionEvent(const SessionEvent &event);
    bool writeAudioCaptureEvent(AudioCaptureEvent event, const QByteArray &pcmData);
    bool writeAsrEvent(const AsrEvent &event);
    bool writeToolDecisionEvent(const ToolDecisionEvent &event);
    bool writeToolExecutionEvent(const ToolExecutionEvent &event);
    bool writeBehaviorDecisionEvent(const BehaviorDecisionEvent &event);
    bool writeMemoryDecisionEvent(const MemoryDecisionEvent &event);
    bool writeUserFeedbackEvent(const UserFeedbackEvent &event);

    bool runRetention(const LearningDataSettingsSnapshot &settings);
    bool exportPreparedManifests(const LearningDataSettingsSnapshot &settings);
    LearningDataDiagnostics collectDiagnostics() const;

private:
    bool ensureLayout(const LearningDataSettingsSnapshot &settings);

    [[nodiscard]] QString rootPath() const;
    [[nodiscard]] QString indexRoot() const;
    [[nodiscard]] QString audioRoot() const;
    [[nodiscard]] QString snapshotsRoot() const;
    [[nodiscard]] QString exportsRoot() const;
    [[nodiscard]] QString quarantineRoot() const;
    [[nodiscard]] QString retentionRoot() const;

    [[nodiscard]] QString datedJsonlPath(const QString &bucket, const QString &isoTimestamp) const;
    [[nodiscard]] QString asRelativePath(const QString &absolutePath) const;

    bool appendJsonLine(const QString &absolutePath, const QJsonObject &payload) const;
    bool appendTombstone(const QString &kind,
                         const QString &reason,
                         const QString &targetPath,
                         qint64 sizeBytes,
                         bool deleted) const;
    void appendFailureRecord(const QString &kind,
                             const QString &reason,
                             const QJsonObject &payload) const;

    bool writeSchemaVersionFile() const;
    QString buildAudioFilePath(const AudioCaptureEvent &event) const;
    bool writeWavFile(const QString &absolutePath,
                      const QByteArray &pcmData,
                      int sampleRate,
                      int channels,
                      QString *sha256Hex,
                      qint64 *durationMs) const;

    static QString sanitizeToken(const QString &value, const QString &fallback);
    static QDateTime parseIsoOrNow(const QString &value);

    static QList<QJsonObject> readAllJsonlObjects(const QString &rootDir);
    static bool writeJsonlFile(const QString &absolutePath, const QList<QJsonObject> &rows);

    bool enforceAudioStorageLimitGb(double maxGb);
    bool removeFilesOlderThan(const QString &rootDir,
                              const QDateTime &cutoffUtc,
                              const QString &kind,
                              const QString &reason);

    QString m_rootPath;
    LoggingService *m_loggingService = nullptr;
};

} // namespace LearningData

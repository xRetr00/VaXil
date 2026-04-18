#include "learning_data/LearningDataSettings.h"

#include <QDir>
#include <QFileInfo>

#include "settings/AppSettings.h"

namespace LearningData {

LearningDataSettings::LearningDataSettings(const AppSettings *settings)
    : m_settings(settings)
{
}

LearningDataSettingsSnapshot LearningDataSettings::snapshot() const
{
    LearningDataSettingsSnapshot values;

    if (!m_settings) {
        return values;
    }

    values.enabled = m_settings->learningDataCollectionEnabled();
    values.audioCollectionEnabled = m_settings->learningAudioCollectionEnabled();
    values.transcriptCollectionEnabled = m_settings->learningTranscriptCollectionEnabled();
    values.toolLoggingEnabled = m_settings->learningToolLoggingEnabled();
    values.behaviorLoggingEnabled = m_settings->learningBehaviorLoggingEnabled();
    values.memoryLoggingEnabled = m_settings->learningMemoryLoggingEnabled();
    values.maxAudioStorageGb = m_settings->learningMaxAudioStorageGb();
    values.maxDaysToKeepAudio = m_settings->learningMaxDaysToKeepAudio();
    values.maxDaysToKeepStructuredLogs = m_settings->learningMaxDaysToKeepStructuredLogs();
    values.allowPreparedDatasetExport = m_settings->learningAllowPreparedDatasetExport();

    const QFileInfo settingsFileInfo(m_settings->storagePath());
    const QString appDataRoot = settingsFileInfo.absolutePath();
    values.rootDirectory = QDir(appDataRoot).filePath(QStringLiteral("learning"));

    return values;
}

} // namespace LearningData

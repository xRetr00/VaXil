#include "learning_data/LearningDataSettings.h"

#include "settings/AppSettings.h"

namespace LearningData {

SettingsSnapshot SettingsSnapshot::fromAppSettings(const AppSettings *settings)
{
    SettingsSnapshot snapshot;
    if (settings == nullptr) {
        return snapshot;
    }

    snapshot.enabled = settings->learningDataCollectionEnabled();
    snapshot.audioCollectionEnabled = settings->learningAudioCollectionEnabled();
    snapshot.transcriptCollectionEnabled = settings->learningTranscriptCollectionEnabled();
    snapshot.toolLoggingEnabled = settings->learningToolLoggingEnabled();
    snapshot.behaviorLoggingEnabled = settings->learningBehaviorLoggingEnabled();
    snapshot.memoryLoggingEnabled = settings->learningMemoryLoggingEnabled();
    snapshot.maxAudioStorageGb = settings->learningMaxAudioStorageGb();
    snapshot.maxDaysToKeepAudio = settings->learningMaxDaysToKeepAudio();
    snapshot.maxDaysToKeepStructuredLogs = settings->learningMaxDaysToKeepStructuredLogs();
    snapshot.allowExportPreparedDatasets = settings->learningAllowPreparedDatasetExport();
    return snapshot;
}

} // namespace LearningData

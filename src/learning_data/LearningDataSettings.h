#pragma once

#include <QString>

class AppSettings;

namespace LearningData {

struct LearningDataSettingsSnapshot
{
    bool enabled = false;
    bool audioCollectionEnabled = false;
    bool wakeWordCollectionEnabled = false;
    bool wakeWordPositiveCollectionEnabled = false;
    bool wakeWordNegativeCollectionEnabled = false;
    bool wakeWordHardNegativeCollectionEnabled = false;
    bool transcriptCollectionEnabled = false;
    bool toolLoggingEnabled = false;
    bool behaviorLoggingEnabled = false;
    bool memoryLoggingEnabled = false;
    double maxAudioStorageGb = 4.0;
    double maxWakeWordStorageGb = 2.0;
    int maxDaysToKeepAudio = 30;
    int maxDaysToKeepWakeWordAudio = 45;
    int maxDaysToKeepStructuredLogs = 90;
    bool allowPreparedDatasetExport = false;
    QString rootDirectory;

    [[nodiscard]] bool hasAnyCategoryEnabled() const
    {
        return audioCollectionEnabled
            || wakeWordCollectionEnabled
            || transcriptCollectionEnabled
            || toolLoggingEnabled
            || behaviorLoggingEnabled
            || memoryLoggingEnabled;
    }
};

class LearningDataSettings
{
public:
    explicit LearningDataSettings(const AppSettings *settings = nullptr);

    [[nodiscard]] LearningDataSettingsSnapshot snapshot() const;

private:
    const AppSettings *m_settings = nullptr;
};

} // namespace LearningData

#pragma once

#include <QJsonObject>

class AppSettings;

namespace LearningData {

struct SettingsSnapshot {
    bool enabled = false;
    bool audioCollectionEnabled = false;
    bool transcriptCollectionEnabled = false;
    bool toolLoggingEnabled = false;
    bool behaviorLoggingEnabled = false;
    bool memoryLoggingEnabled = false;
    double maxAudioStorageGb = 4.0;
    int maxDaysToKeepAudio = 30;
    int maxDaysToKeepStructuredLogs = 90;
    bool allowExportPreparedDatasets = false;

    [[nodiscard]] bool hasAnyCategoryEnabled() const
    {
        return audioCollectionEnabled
            || transcriptCollectionEnabled
            || toolLoggingEnabled
            || behaviorLoggingEnabled
            || memoryLoggingEnabled;
    }

    [[nodiscard]] QJsonObject toJson() const
    {
        return {
            {QStringLiteral("enable_learning_data_collection"), enabled},
            {QStringLiteral("enable_audio_collection"), audioCollectionEnabled},
            {QStringLiteral("enable_transcript_collection"), transcriptCollectionEnabled},
            {QStringLiteral("enable_tool_logging"), toolLoggingEnabled},
            {QStringLiteral("enable_behavior_logging"), behaviorLoggingEnabled},
            {QStringLiteral("enable_memory_logging"), memoryLoggingEnabled},
            {QStringLiteral("max_audio_storage_gb"), maxAudioStorageGb},
            {QStringLiteral("max_days_to_keep_audio"), maxDaysToKeepAudio},
            {QStringLiteral("max_days_to_keep_structured_logs"), maxDaysToKeepStructuredLogs},
            {QStringLiteral("allow_export_prepared_datasets"), allowExportPreparedDatasets}
        };
    }

    [[nodiscard]] static SettingsSnapshot fromAppSettings(const AppSettings *settings);
};

} // namespace LearningData

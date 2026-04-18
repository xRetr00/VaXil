#pragma once

#include <functional>

#include <QMutex>
#include <QThreadPool>

#include "learning_data/LearningDataSettings.h"
#include "learning_data/LearningDataStorage.h"
#include "learning_data/LearningDataTypes.h"

class AppSettings;
class LoggingService;

namespace LearningData {

class LearningDataCollector
{
public:
    explicit LearningDataCollector(AppSettings *settings,
                                   LoggingService *loggingService,
                                   QString rootPath = QString());
    ~LearningDataCollector();

    [[nodiscard]] bool initialize();
    [[nodiscard]] QString rootPath() const;
    [[nodiscard]] SettingsSnapshot currentSettings() const;

    [[nodiscard]] static QString createEventId(const QString &prefix);

    void recordSessionEvent(SessionEvent event);
    void recordAudioCaptureEvent(AudioCaptureEvent event, const QByteArray &pcmData);
    void recordAsrEvent(AsrEvent event);
    void recordToolDecisionEvent(ToolDecisionEvent event);
    void recordToolExecutionEvent(ToolExecutionEvent event);
    void recordBehaviorDecisionEvent(BehaviorDecisionEvent event);
    void recordMemoryDecisionEvent(MemoryDecisionEvent event);
    void recordUserFeedbackEvent(UserFeedbackEvent event);

    void runMaintenance();
    [[nodiscard]] DiagnosticsSnapshot diagnosticsSnapshot() const;
    void waitForIdle();

private:
    [[nodiscard]] bool ensureInitialized(const SettingsSnapshot &settingsSnapshot);
    [[nodiscard]] bool collectionEnabled(const SettingsSnapshot &settingsSnapshot) const;
    void enqueue(const std::function<void()> &task);
    void maybeLogDiagnostics(const DiagnosticsSnapshot &snapshot) const;

    AppSettings *m_settings = nullptr;
    LoggingService *m_loggingService = nullptr;
    LearningDataStorage m_storage;
    mutable QMutex m_mutex;
    mutable QMutex m_initMutex;
    mutable bool m_initialized = false;
    QThreadPool m_ioPool;
};

} // namespace LearningData

#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include <QByteArray>
#include <QString>

#include "learning_data/LearningDataSettings.h"
#include "learning_data/LearningDataStorage.h"
#include "learning_data/LearningDataTypes.h"

class AppSettings;
class LoggingService;

namespace LearningData {

class LearningDataCollector
{
public:
    LearningDataCollector(AppSettings *settings,
                          LoggingService *loggingService,
                          QString rootPath = QString());
    ~LearningDataCollector();

    void initialize();
    void waitForIdle();
    void runMaintenance();

    [[nodiscard]] LearningDataSettingsSnapshot currentSettings() const;
    [[nodiscard]] LearningDataDiagnostics diagnosticsSnapshot();

    void recordSessionEvent(const SessionEvent &event);
    void recordAudioCaptureEvent(const AudioCaptureEvent &event, const QByteArray &pcmData);
    void recordWakeWordEvent(const WakeWordEvent &event, const QByteArray &pcmData);
    void recordAsrEvent(const AsrEvent &event);
    void recordToolDecisionEvent(const ToolDecisionEvent &event);
    void recordToolExecutionEvent(const ToolExecutionEvent &event);
    void recordBehaviorDecisionEvent(const BehaviorDecisionEvent &event);
    void recordMemoryDecisionEvent(const MemoryDecisionEvent &event);
    void recordUserFeedbackEvent(const UserFeedbackEvent &event);

    static QString createEventId(const QString &prefix);

private:
    void enqueueTask(std::function<void()> task);
    void runWorkerLoop();
    bool shouldRecordCategory(bool categoryEnabled) const;

    AppSettings *m_settings = nullptr;
    LoggingService *m_loggingService = nullptr;
    LearningDataSettings m_settingsProvider;
    LearningDataStorage m_storage;

    std::thread m_workerThread;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::condition_variable m_idleCv;
    std::deque<std::function<void()>> m_tasks;
    std::size_t m_pendingTaskCount = 0;
    bool m_stopRequested = false;
};

} // namespace LearningData

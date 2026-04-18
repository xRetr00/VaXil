#include "learning_data/LearningDataCollector.h"

#include <future>
#include <utility>

#include <QDateTime>
#include <QUuid>

#include "logging/LoggingService.h"

namespace LearningData {

LearningDataCollector::LearningDataCollector(AppSettings *settings,
                                             LoggingService *loggingService,
                                             QString rootPath)
    : m_settings(settings)
    , m_loggingService(loggingService)
    , m_settingsProvider(settings)
    , m_storage(std::move(rootPath), loggingService)
{
    m_workerThread = std::thread([this]() { runWorkerLoop(); });
}

LearningDataCollector::~LearningDataCollector()
{
    waitForIdle();

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_stopRequested = true;
    }
    m_queueCv.notify_all();

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

void LearningDataCollector::initialize()
{
    const LearningDataSettingsSnapshot settings = currentSettings();
    if (!settings.enabled || !settings.hasAnyCategoryEnabled()) {
        return;
    }

    enqueueTask([this, settings]() {
        (void)m_storage.initialize(settings);
    });
}

void LearningDataCollector::waitForIdle()
{
    std::unique_lock<std::mutex> lock(m_queueMutex);
    m_idleCv.wait(lock, [this]() { return m_pendingTaskCount == 0; });
}

void LearningDataCollector::runMaintenance()
{
    const LearningDataSettingsSnapshot settings = currentSettings();
    if (!settings.enabled || !settings.hasAnyCategoryEnabled()) {
        return;
    }

    enqueueTask([this, settings]() {
        if (!m_storage.initialize(settings)) {
            return;
        }

        (void)m_storage.runRetention(settings);
        if (settings.allowPreparedDatasetExport) {
            (void)m_storage.exportPreparedManifests(settings);
        }
    });
}

LearningDataSettingsSnapshot LearningDataCollector::currentSettings() const
{
    return m_settingsProvider.snapshot();
}

LearningDataDiagnostics LearningDataCollector::diagnosticsSnapshot()
{
    LearningDataDiagnostics diagnostics;
    const LearningDataSettingsSnapshot settings = currentSettings();
    if (!settings.enabled || !settings.hasAnyCategoryEnabled()) {
        return diagnostics;
    }

    auto promise = std::make_shared<std::promise<LearningDataDiagnostics>>();
    std::future<LearningDataDiagnostics> future = promise->get_future();
    enqueueTask([this, settings, promise]() {
        if (!m_storage.initialize(settings)) {
            promise->set_value(LearningDataDiagnostics{});
            return;
        }
        promise->set_value(m_storage.collectDiagnostics());
    });

    return future.get();
}

void LearningDataCollector::recordSessionEvent(const SessionEvent &event)
{
    const LearningDataSettingsSnapshot settings = currentSettings();
    if (!shouldRecordCategory(settings.hasAnyCategoryEnabled())) {
        return;
    }

    SessionEvent payload = event;
    enqueueTask([this, settings, payload]() {
        if (!m_storage.initialize(settings)) {
            return;
        }
        (void)m_storage.writeSessionEvent(payload);
    });
}

void LearningDataCollector::recordAudioCaptureEvent(const AudioCaptureEvent &event, const QByteArray &pcmData)
{
    const LearningDataSettingsSnapshot settings = currentSettings();
    if (!shouldRecordCategory(settings.audioCollectionEnabled)) {
        return;
    }

    AudioCaptureEvent payload = event;
    QByteArray payloadPcm = pcmData;
    enqueueTask([this, settings, payload = std::move(payload), payloadPcm = std::move(payloadPcm)]() {
        if (!m_storage.initialize(settings)) {
            return;
        }
        (void)m_storage.writeAudioCaptureEvent(payload, payloadPcm);
    });
}

void LearningDataCollector::recordAsrEvent(const AsrEvent &event)
{
    const LearningDataSettingsSnapshot settings = currentSettings();
    if (!shouldRecordCategory(settings.transcriptCollectionEnabled)) {
        return;
    }

    AsrEvent payload = event;
    enqueueTask([this, settings, payload]() {
        if (!m_storage.initialize(settings)) {
            return;
        }
        (void)m_storage.writeAsrEvent(payload);
    });
}

void LearningDataCollector::recordToolDecisionEvent(const ToolDecisionEvent &event)
{
    const LearningDataSettingsSnapshot settings = currentSettings();
    if (!shouldRecordCategory(settings.toolLoggingEnabled)) {
        return;
    }

    ToolDecisionEvent payload = event;
    enqueueTask([this, settings, payload]() {
        if (!m_storage.initialize(settings)) {
            return;
        }
        (void)m_storage.writeToolDecisionEvent(payload);
    });
}

void LearningDataCollector::recordToolExecutionEvent(const ToolExecutionEvent &event)
{
    const LearningDataSettingsSnapshot settings = currentSettings();
    if (!shouldRecordCategory(settings.toolLoggingEnabled)) {
        return;
    }

    ToolExecutionEvent payload = event;
    enqueueTask([this, settings, payload]() {
        if (!m_storage.initialize(settings)) {
            return;
        }
        (void)m_storage.writeToolExecutionEvent(payload);
    });
}

void LearningDataCollector::recordBehaviorDecisionEvent(const BehaviorDecisionEvent &event)
{
    const LearningDataSettingsSnapshot settings = currentSettings();
    if (!shouldRecordCategory(settings.behaviorLoggingEnabled)) {
        return;
    }

    BehaviorDecisionEvent payload = event;
    enqueueTask([this, settings, payload]() {
        if (!m_storage.initialize(settings)) {
            return;
        }
        (void)m_storage.writeBehaviorDecisionEvent(payload);
    });
}

void LearningDataCollector::recordMemoryDecisionEvent(const MemoryDecisionEvent &event)
{
    const LearningDataSettingsSnapshot settings = currentSettings();
    if (!shouldRecordCategory(settings.memoryLoggingEnabled)) {
        return;
    }

    MemoryDecisionEvent payload = event;
    enqueueTask([this, settings, payload]() {
        if (!m_storage.initialize(settings)) {
            return;
        }
        (void)m_storage.writeMemoryDecisionEvent(payload);
    });
}

void LearningDataCollector::recordUserFeedbackEvent(const UserFeedbackEvent &event)
{
    const LearningDataSettingsSnapshot settings = currentSettings();
    if (!shouldRecordCategory(settings.behaviorLoggingEnabled)) {
        return;
    }

    UserFeedbackEvent payload = event;
    enqueueTask([this, settings, payload]() {
        if (!m_storage.initialize(settings)) {
            return;
        }
        (void)m_storage.writeUserFeedbackEvent(payload);
    });
}

QString LearningDataCollector::createEventId(const QString &prefix)
{
    const QString safePrefix = prefix.trimmed().isEmpty() ? QStringLiteral("event") : prefix.trimmed();
    return QStringLiteral("%1_%2_%3")
        .arg(safePrefix)
        .arg(QDateTime::currentMSecsSinceEpoch())
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
}

void LearningDataCollector::enqueueTask(std::function<void()> task)
{
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_stopRequested) {
            return;
        }
        m_tasks.push_back(std::move(task));
        ++m_pendingTaskCount;
    }
    m_queueCv.notify_one();
}

void LearningDataCollector::runWorkerLoop()
{
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCv.wait(lock, [this]() {
                return m_stopRequested || !m_tasks.empty();
            });

            if (m_stopRequested && m_tasks.empty()) {
                return;
            }

            task = std::move(m_tasks.front());
            m_tasks.pop_front();
        }

        try {
            task();
        } catch (...) {
            if (m_loggingService) {
                m_loggingService->errorFor(
                    QStringLiteral("tool_audit"),
                    QStringLiteral("[learning_data] background task failed with exception"));
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_pendingTaskCount > 0) {
                --m_pendingTaskCount;
            }
            if (m_pendingTaskCount == 0) {
                m_idleCv.notify_all();
            }
        }
    }
}

bool LearningDataCollector::shouldRecordCategory(bool categoryEnabled) const
{
    const LearningDataSettingsSnapshot settings = currentSettings();
    return settings.enabled && categoryEnabled;
}

} // namespace LearningData

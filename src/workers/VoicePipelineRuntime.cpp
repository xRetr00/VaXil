#include "workers/VoicePipelineRuntime.h"

#include <QMetaObject>

#include "logging/LoggingService.h"
#include "settings/AppSettings.h"
#include "workers/AiBackendWorker.h"
#include "workers/SpeechInputWorker.h"
#include "workers/SpeechIoWorker.h"

VoicePipelineRuntime::VoicePipelineRuntime(AppSettings *settings, LoggingService *loggingService, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_loggingService(loggingService)
{
    m_inputThread.setObjectName(QStringLiteral("SpeechInputThread"));
    m_ioThread.setObjectName(QStringLiteral("SpeechIoThread"));
    m_backendThread.setObjectName(QStringLiteral("AiBackendThread"));

    m_inputWorker = new SpeechInputWorker();
    m_ioWorker = new SpeechIoWorker(settings, loggingService);
    m_backendWorker = new AiBackendWorker();

    m_inputWorker->moveToThread(&m_inputThread);
    m_ioWorker->moveToThread(&m_ioThread);
    m_backendWorker->moveToThread(&m_backendThread);

    connect(&m_inputThread, &QThread::finished, m_inputWorker, &QObject::deleteLater);
    connect(&m_ioThread, &QThread::finished, m_ioWorker, &QObject::deleteLater);
    connect(&m_backendThread, &QThread::finished, m_backendWorker, &QObject::deleteLater);

    connect(m_inputWorker, &SpeechInputWorker::audioLevelChanged, this, &VoicePipelineRuntime::inputAudioLevelChanged, Qt::QueuedConnection);
    connect(m_inputWorker, &SpeechInputWorker::speechFrame, this, &VoicePipelineRuntime::speechFrame, Qt::QueuedConnection);
    connect(m_inputWorker, &SpeechInputWorker::speechActivityChanged, this, &VoicePipelineRuntime::speechActivityChanged, Qt::QueuedConnection);
    connect(m_inputWorker, &SpeechInputWorker::captureFinished, this, &VoicePipelineRuntime::inputCaptureFinished, Qt::QueuedConnection);
    connect(m_inputWorker, &SpeechInputWorker::captureFailed, this, &VoicePipelineRuntime::inputCaptureFailed, Qt::QueuedConnection);

    connect(m_ioWorker, &SpeechIoWorker::transcriptionReady, this, &VoicePipelineRuntime::transcriptionReady, Qt::QueuedConnection);
    connect(m_ioWorker, &SpeechIoWorker::transcriptionFailed, this, &VoicePipelineRuntime::transcriptionFailed, Qt::QueuedConnection);
    connect(m_ioWorker, &SpeechIoWorker::playbackStarted, this, &VoicePipelineRuntime::playbackStarted, Qt::QueuedConnection);
    connect(m_ioWorker, &SpeechIoWorker::playbackFinished, this, &VoicePipelineRuntime::playbackFinished, Qt::QueuedConnection);
    connect(m_ioWorker, &SpeechIoWorker::playbackFailed, this, &VoicePipelineRuntime::playbackFailed, Qt::QueuedConnection);
    connect(m_ioWorker, &SpeechIoWorker::farEndFrameReady, this, &VoicePipelineRuntime::farEndFrameReady, Qt::QueuedConnection);
    connect(m_ioWorker, &SpeechIoWorker::farEndFrameReady, this, [this](quint64, const AudioFrame &frame) {
        QMetaObject::invokeMethod(
            m_inputWorker,
            [worker = m_inputWorker, frame]() {
                worker->setFarEndFrame(frame);
            },
            Qt::QueuedConnection);
    });

    connect(m_backendWorker, &AiBackendWorker::modelsReady, this, &VoicePipelineRuntime::modelsReady, Qt::QueuedConnection);
    connect(m_backendWorker, &AiBackendWorker::availabilityChanged, this, &VoicePipelineRuntime::availabilityChanged, Qt::QueuedConnection);
    connect(m_backendWorker, &AiBackendWorker::capabilitiesChanged, this, &VoicePipelineRuntime::capabilitiesChanged, Qt::QueuedConnection);
    connect(m_backendWorker, &AiBackendWorker::requestStarted, this, &VoicePipelineRuntime::requestStarted, Qt::QueuedConnection);
    connect(m_backendWorker, &AiBackendWorker::requestDelta, this, &VoicePipelineRuntime::requestDelta, Qt::QueuedConnection);
    connect(m_backendWorker, &AiBackendWorker::requestFinished, this, &VoicePipelineRuntime::requestFinished, Qt::QueuedConnection);
    connect(m_backendWorker, &AiBackendWorker::agentResponseReady, this, &VoicePipelineRuntime::agentResponseReady, Qt::QueuedConnection);
    connect(m_backendWorker, &AiBackendWorker::requestFailed, this, &VoicePipelineRuntime::requestFailed, Qt::QueuedConnection);
}

VoicePipelineRuntime::~VoicePipelineRuntime()
{
    shutdown();
}

void VoicePipelineRuntime::start()
{
    if (m_started) {
        return;
    }

    m_inputThread.start();
    m_ioThread.start();
    m_backendThread.start();
    m_started = true;
}

void VoicePipelineRuntime::shutdown()
{
    if (!m_started) {
        return;
    }

    m_inputThread.quit();
    m_ioThread.quit();
    m_backendThread.quit();

    m_inputThread.wait();
    m_ioThread.wait();
    m_backendThread.wait();
    m_started = false;
}

SpeechInputWorker *VoicePipelineRuntime::speechInputWorker() const
{
    return m_inputWorker;
}

SpeechIoWorker *VoicePipelineRuntime::speechIoWorker() const
{
    return m_ioWorker;
}

AiBackendWorker *VoicePipelineRuntime::aiBackendWorker() const
{
    return m_backendWorker;
}

void VoicePipelineRuntime::configureAudioProcessing(const AudioProcessingConfig &config)
{
    QMetaObject::invokeMethod(
        m_inputWorker,
        [worker = m_inputWorker, config]() {
            worker->configure(config);
        },
        Qt::QueuedConnection);
}

void VoicePipelineRuntime::startInputGeneration(quint64 generationId)
{
    QMetaObject::invokeMethod(
        m_inputWorker,
        [worker = m_inputWorker, generationId]() {
            worker->startGeneration(generationId);
        },
        Qt::QueuedConnection);
}

void VoicePipelineRuntime::startInputCapture(quint64 generationId, double sensitivity, const QString &preferredDeviceId)
{
    QMetaObject::invokeMethod(
        m_inputWorker,
        [worker = m_inputWorker, generationId, sensitivity, preferredDeviceId]() {
            worker->startCapture(generationId, sensitivity, preferredDeviceId);
        },
        Qt::QueuedConnection);
}

void VoicePipelineRuntime::stopInputCapture(bool finalize)
{
    QMetaObject::invokeMethod(
        m_inputWorker,
        [worker = m_inputWorker, finalize]() {
            worker->stopCapture(finalize);
        },
        Qt::QueuedConnection);
}

void VoicePipelineRuntime::clearInputCapture()
{
    QMetaObject::invokeMethod(m_inputWorker, &SpeechInputWorker::clearCapture, Qt::QueuedConnection);
}

void VoicePipelineRuntime::startWakeCapture(quint64 generationId, const QString &preferredDeviceId)
{
    QMetaObject::invokeMethod(
        m_inputWorker,
        [worker = m_inputWorker, generationId, preferredDeviceId]() {
            worker->startWakeMonitor(generationId, preferredDeviceId);
        },
        Qt::QueuedConnection);
}

void VoicePipelineRuntime::stopWakeCapture()
{
    QMetaObject::invokeMethod(m_inputWorker, &SpeechInputWorker::stopWakeMonitor, Qt::QueuedConnection);
}

void VoicePipelineRuntime::transcribe(quint64 generationId, const QByteArray &pcmData, const QString &initialPrompt, bool suppressNonSpeechTokens)
{
    QMetaObject::invokeMethod(
        m_ioWorker,
        [worker = m_ioWorker, generationId, pcmData, initialPrompt, suppressNonSpeechTokens]() {
            worker->transcribe(generationId, pcmData, initialPrompt, suppressNonSpeechTokens);
        },
        Qt::QueuedConnection);
}

void VoicePipelineRuntime::speak(quint64 generationId, const QString &text)
{
    QMetaObject::invokeMethod(
        m_ioWorker,
        [worker = m_ioWorker, generationId, text]() {
            worker->speak(generationId, text);
        },
        Qt::QueuedConnection);
}

void VoicePipelineRuntime::cancelSpeechIo()
{
    QMetaObject::invokeMethod(m_ioWorker, &SpeechIoWorker::cancel, Qt::QueuedConnection);
}

void VoicePipelineRuntime::setBackendEndpoint(const QString &endpoint)
{
    QMetaObject::invokeMethod(
        m_backendWorker,
        [worker = m_backendWorker, endpoint]() {
            worker->setEndpoint(endpoint);
        },
        Qt::QueuedConnection);
}

void VoicePipelineRuntime::refreshModels()
{
    QMetaObject::invokeMethod(m_backendWorker, &AiBackendWorker::refreshModels, Qt::QueuedConnection);
}

void VoicePipelineRuntime::sendAiRequest(quint64 generationId, const QList<AiMessage> &messages, const QString &model, const AiRequestOptions &options)
{
    QMetaObject::invokeMethod(
        m_backendWorker,
        [worker = m_backendWorker, generationId, messages, model, options]() {
            worker->sendRequest(generationId, messages, model, options);
        },
        Qt::QueuedConnection);
}

void VoicePipelineRuntime::sendAgentRequest(quint64 generationId, const AgentRequest &request)
{
    QMetaObject::invokeMethod(
        m_backendWorker,
        [worker = m_backendWorker, generationId, request]() {
            worker->sendAgentRequest(generationId, request);
        },
        Qt::QueuedConnection);
}

void VoicePipelineRuntime::cancelAiRequest()
{
    QMetaObject::invokeMethod(m_backendWorker, &AiBackendWorker::cancelActiveRequest, Qt::QueuedConnection);
}

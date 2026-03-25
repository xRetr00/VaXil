#pragma once

#include <QObject>
#include <QThread>

#include "audio/AudioProcessingTypes.h"
#include "core/AssistantTypes.h"

class AppSettings;
class LoggingService;
class AiBackendWorker;
class SpeechInputWorker;
class SpeechIoWorker;

class VoicePipelineRuntime : public QObject
{
    Q_OBJECT

public:
    VoicePipelineRuntime(AppSettings *settings, LoggingService *loggingService, QObject *parent = nullptr);
    ~VoicePipelineRuntime() override;

    void start();
    void shutdown();

    SpeechInputWorker *speechInputWorker() const;
    SpeechIoWorker *speechIoWorker() const;
    AiBackendWorker *aiBackendWorker() const;

public slots:
    void configureAudioProcessing(const AudioProcessingConfig &config);
    void setWakeActive(bool active);
    void startInputGeneration(quint64 generationId);
    void ingestMicFrame(const AudioFrame &frame);
    void transcribe(quint64 generationId, const QByteArray &pcmData, const QString &initialPrompt, bool suppressNonSpeechTokens = true);
    void speak(quint64 generationId, const QString &text);
    void cancelSpeechIo();
    void setBackendEndpoint(const QString &endpoint);
    void refreshModels();
    void sendAiRequest(quint64 generationId, const QList<AiMessage> &messages, const QString &model, const AiRequestOptions &options);
    void cancelAiRequest();

signals:
    void wakeDetected(quint64 generationId);
    void speechFrame(quint64 generationId, const AudioFrame &frame);
    void speechActivityChanged(quint64 generationId, bool active);
    void transcriptionReady(quint64 generationId, const TranscriptionResult &result);
    void transcriptionFailed(quint64 generationId, const QString &errorText);
    void playbackStarted(quint64 generationId);
    void playbackFinished(quint64 generationId);
    void playbackFailed(quint64 generationId, const QString &errorText);
    void farEndFrameReady(quint64 generationId, const AudioFrame &frame);
    void modelsReady(const QList<ModelInfo> &models);
    void availabilityChanged(const AiAvailability &availability);
    void requestStarted(quint64 generationId);
    void requestDelta(quint64 generationId, const QString &delta);
    void requestFinished(quint64 generationId, const QString &text);
    void requestFailed(quint64 generationId, const QString &errorText);

private:
    AppSettings *m_settings = nullptr;
    LoggingService *m_loggingService = nullptr;
    QThread m_inputThread;
    QThread m_ioThread;
    QThread m_backendThread;
    SpeechInputWorker *m_inputWorker = nullptr;
    SpeechIoWorker *m_ioWorker = nullptr;
    AiBackendWorker *m_backendWorker = nullptr;
    bool m_started = false;
};

#pragma once

#include <QObject>

#include "audio/AudioProcessingTypes.h"
#include "core/AssistantTypes.h"

class AppSettings;
class LoggingService;
class PiperTtsEngine;
class WhisperSttEngine;

class SpeechIoWorker : public QObject
{
    Q_OBJECT

public:
    SpeechIoWorker(AppSettings *settings, LoggingService *loggingService, QObject *parent = nullptr);

public slots:
    void transcribe(quint64 generationId, const QByteArray &pcmData, const QString &initialPrompt, bool suppressNonSpeechTokens = true);
    void speak(quint64 generationId, const QString &text);
    void cancel();

signals:
    void transcriptionReady(quint64 generationId, const TranscriptionResult &result);
    void transcriptionFailed(quint64 generationId, const QString &errorText);
    void playbackStarted(quint64 generationId);
    void playbackFinished(quint64 generationId);
    void playbackFailed(quint64 generationId, const QString &errorText);
    void farEndFrameReady(quint64 generationId, const AudioFrame &frame);

private:
    WhisperSttEngine *m_whisper = nullptr;
    PiperTtsEngine *m_tts = nullptr;
    quint64 m_activePlaybackGeneration = 0;
    quint64 m_sttGeneration = 0;
};

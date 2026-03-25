#pragma once

#include "stt/SpeechRecognizer.h"

class AppSettings;
class LoggingService;

class WhisperSttEngine : public SpeechRecognizer
{
    Q_OBJECT

public:
    explicit WhisperSttEngine(AppSettings *settings, LoggingService *loggingService, QObject *parent = nullptr);

    quint64 transcribePcm(
        const QByteArray &pcmData,
        const QString &initialPrompt = {},
        bool suppressNonSpeechTokens = true);

signals:
    void transcriptionReady(quint64 requestId, const TranscriptionResult &result);
    void transcriptionFailed(quint64 requestId, const QString &errorText);

private:
    QString writeWaveFile(const QByteArray &pcmData) const;

    AppSettings *m_settings = nullptr;
    LoggingService *m_loggingService = nullptr;
    quint64 m_requestCounter = 0;
};

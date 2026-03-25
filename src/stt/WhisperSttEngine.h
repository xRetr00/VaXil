#pragma once

#include <QObject>

#include "core/AssistantTypes.h"

class AppSettings;
class LoggingService;

class WhisperSttEngine : public QObject
{
    Q_OBJECT

public:
    explicit WhisperSttEngine(AppSettings *settings, LoggingService *loggingService, QObject *parent = nullptr);

    void transcribePcm(
        const QByteArray &pcmData,
        const QString &initialPrompt = {},
        bool suppressNonSpeechTokens = true);

signals:
    void transcriptionReady(const TranscriptionResult &result);
    void transcriptionFailed(const QString &errorText);

private:
    QString writeWaveFile(const QByteArray &pcmData) const;

    AppSettings *m_settings = nullptr;
    LoggingService *m_loggingService = nullptr;
};

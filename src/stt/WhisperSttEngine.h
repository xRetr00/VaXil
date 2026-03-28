#pragma once

#include <QSet>

#include "stt/SpeechRecognizer.h"

class AppSettings;
class LoggingService;
class QProcess;

class WhisperSttEngine : public SpeechRecognizer
{
    Q_OBJECT

public:
    explicit WhisperSttEngine(AppSettings *settings, LoggingService *loggingService, QObject *parent = nullptr);
    ~WhisperSttEngine() override;

    quint64 transcribePcm(
        const QByteArray &pcmData,
        const QString &initialPrompt = {},
        bool suppressNonSpeechTokens = true);

signals:
    void transcriptionReady(quint64 requestId, const TranscriptionResult &result);
    void transcriptionFailed(quint64 requestId, const QString &errorText);

private:
    void stopActiveProcesses(const QString &reason, bool waitForExit = false);
    QString writeWaveFile(const QByteArray &pcmData) const;

    AppSettings *m_settings = nullptr;
    LoggingService *m_loggingService = nullptr;
    quint64 m_requestCounter = 0;
    QSet<QProcess *> m_activeProcesses;
};

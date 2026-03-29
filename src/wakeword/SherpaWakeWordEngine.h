#pragma once

#include <QDateTime>
#include <QPointer>
#include <QProcess>

#include "wakeword/WakeWordEngine.h"

class AppSettings;
class LoggingService;

class SherpaWakeWordEngine : public WakeWordEngine
{
    Q_OBJECT

public:
    explicit SherpaWakeWordEngine(AppSettings *settings, LoggingService *loggingService, QObject *parent = nullptr);
    ~SherpaWakeWordEngine() override;

    bool start(
        const QString &enginePath,
        const QString &modelPath,
        float threshold,
        int cooldownMs,
        const QString &preferredDeviceId = {}) override;
    void pause() override;
    void resume() override;
    void stop() override;
    bool isActive() const override;
    bool isPaused() const override;
    bool usesExternalAudioInput() const override { return false; }

private:
    bool startHelperProcess();
    void consumeHelperStdout();
    void consumeHelperStderr();
    void handleHelperFinished(int exitCode, QProcess::ExitStatus exitStatus);
    QString resolveHelperExecutablePath() const;
    QString resolveModelFile(const QString &rootPath, const QStringList &fileNames) const;
    void handleTranscriptEvent(const QString &transcript, bool isFinal);

    AppSettings *m_settings = nullptr;
    LoggingService *m_loggingService = nullptr;
    float m_threshold = 0.8f;
    int m_cooldownMs = 450;
    QString m_preferredDeviceId;
    bool m_paused = false;
    bool m_ready = false;
    bool m_stopRequested = false;
    int m_activationWarmupMs = 250;
    QString m_runtimeRoot;
    QString m_modelRoot;
    QString m_encoderPath;
    QString m_decoderPath;
    QString m_joinerPath;
    QString m_tokensPath;
    QString m_bpeModelPath;
    QString m_helperPath;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    qint64 m_lastTranscriptWakeMs = 0;
    QPointer<QProcess> m_helperProcess;
};

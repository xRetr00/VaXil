#pragma once

#include <QAudioFormat>
#include <QAudioSource>
#include <QByteArray>
#include <QDateTime>
#include <QIODevice>
#include <QPointer>
#include <QProcess>
#include <QQueue>
#include <memory>

#include "wakeword/WakeWordEngine.h"

class LoggingService;

class WakeWordEnginePrecise : public WakeWordEngine
{
    Q_OBJECT

public:
    explicit WakeWordEnginePrecise(LoggingService *loggingService, QObject *parent = nullptr);
    ~WakeWordEnginePrecise() override;

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

private:
    bool startAudioCapture();
    void stopAudioCapture();
    void resetDetectionState();
    void flushAudioToEngine();
    void consumeEngineOutput();
    void handleProcessError(QProcess::ProcessError error);

    LoggingService *m_loggingService = nullptr;
    std::unique_ptr<QAudioSource> m_audioSource;
    QPointer<QIODevice> m_audioIoDevice;
    QAudioFormat m_format;
    QByteArray m_pendingAudio;
    QByteArray m_stdoutBuffer;
    QMetaObject::Connection m_audioReadyReadConnection;
    QProcess m_engineProcess;
    qint64 m_lastActivationMs = 0;
    int m_chunkBytes = 2048;
    float m_threshold = 0.30f;
    int m_cooldownMs = 750;
    int m_consistentFramesRequired = 2;
    int m_movingAverageWindowFrames = 3;
    int m_consecutiveAboveThreshold = 0;
    int m_probabilityLogCounter = 0;
    float m_probabilitySum = 0.0f;
    QQueue<float> m_recentProbabilities;
    QString m_preferredDeviceId;
    qint64 m_ignoreDetectionsUntilMs = 0;
    int m_activationWarmupMs = 1500;
    bool m_paused = false;
};

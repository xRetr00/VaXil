#pragma once

#include <QAudioFormat>
#include <QAudioSource>
#include <QByteArray>
#include <QElapsedTimer>
#include <QPointer>
#include <QObject>
#include <memory>

#include "audio/AudioProcessingChain.h"
#include "core/AssistantTypes.h"

class SpeechInputWorker : public QObject
{
    Q_OBJECT

public:
    explicit SpeechInputWorker(QObject *parent = nullptr);

public slots:
    void configure(const AudioProcessingConfig &config);
    void startGeneration(quint64 generationId);
    void startCapture(quint64 generationId, double sensitivity, const QString &preferredDeviceId);
    void stopCapture(bool finalize);
    void clearCapture();
    void setFarEndFrame(const AudioFrame &frame);
    void setWakeActive(bool active);
    void reset();

signals:
    void audioLevelChanged(quint64 generationId, const AudioLevel &level);
    void wakeDetected(quint64 generationId);
    void speechFrame(quint64 generationId, const AudioFrame &frame);
    void speechActivityChanged(quint64 generationId, bool active);
    void captureFinished(quint64 generationId, const QByteArray &pcmData, bool hadSpeech);
    void captureFailed(quint64 generationId, const QString &errorText);

private:
    void processMicBuffer();
    void finishCapture(bool hadSpeech);
    bool startAudioDevice(const QString &preferredDeviceId);
    void stopAudioDevice();
    float computePeak(const AudioFrame &frame) const;
    AudioFrame buildFrame(const qint16 *samples, int sampleCount, qint64 sequence) const;

    AudioProcessingChain m_chain;
    std::unique_ptr<QAudioSource> m_audioSource;
    QPointer<QIODevice> m_audioIoDevice;
    QAudioFormat m_format;
    QByteArray m_recordedPcm;
    QByteArray m_pendingPcm;
    quint64 m_generationId = 0;
    bool m_wakeActive = false;
    bool m_lastSpeechActive = false;
    bool m_captureActive = false;
    bool m_hasDetectedSpeech = false;
    double m_silenceThreshold = 0.02;
    int m_consecutiveSpeechMs = 0;
    int m_consecutiveSilenceMs = 0;
    qint64 m_frameSequence = 0;
    QElapsedTimer m_captureElapsed;
    QElapsedTimer m_speechElapsed;
};

#pragma once

#include <QAudioFormat>
#include <QAudioSource>
#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <memory>

#include "core/AssistantTypes.h"

struct Fvad;

class AudioInputService : public QObject
{
    Q_OBJECT

public:
    explicit AudioInputService(QObject *parent = nullptr);
    ~AudioInputService() override;

    bool start(double sensitivity, const QString &preferredDeviceId = {});
    void stop();
    void clearRecordedAudio();
    QByteArray recordedPcm() const;
    bool isActive() const;

signals:
    void audioLevelChanged(const AudioLevel &level);
    void speechDetected();
    void speechEnded();
    void captureWindowElapsed(bool hadSpeech);

private:
    void processBuffer();
    void initializeVad();
    void clearVad();

    std::unique_ptr<QAudioSource> m_audioSource;
    QPointer<QIODevice> m_ioDevice;
    QAudioFormat m_format;
    QByteArray m_recordedData;
    QByteArray m_vadPendingData;
    QTimer m_levelTimer;
    double m_silenceThreshold = 0.02;
    int m_consecutiveSpeechMs = 0;
    int m_consecutiveSilenceMs = 0;
    bool m_speechStarted = false;
    bool m_hasDetectedSpeech = false;
    QElapsedTimer m_captureElapsed;
    QElapsedTimer m_speechElapsed;
    Fvad *m_vad = nullptr;
};

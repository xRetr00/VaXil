#pragma once

#include <QAudioFormat>
#include <QAudioSource>
#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <memory>

#include "core/AssistantTypes.h"

class AudioInputService : public QObject
{
    Q_OBJECT

public:
    explicit AudioInputService(QObject *parent = nullptr);
    ~AudioInputService() override;

    bool start(double sensitivity, const QString &preferredDeviceId = {});
    void stop();
    QByteArray recordedPcm() const;
    bool isActive() const;

signals:
    void audioLevelChanged(const AudioLevel &level);
    void speechDetected();
    void speechEnded();

private:
    void processBuffer();

    std::unique_ptr<QAudioSource> m_audioSource;
    QPointer<QIODevice> m_ioDevice;
    QAudioFormat m_format;
    QByteArray m_recordedData;
    QTimer m_levelTimer;
    double m_silenceThreshold = 0.02;
    int m_silenceFrames = 0;
    bool m_speechStarted = false;
};

#include "audio/AudioInputService.h"

#include <QtMath>
#include <QMediaDevices>

AudioInputService::AudioInputService(QObject *parent)
    : QObject(parent)
{
    m_format.setSampleRate(16000);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);

    connect(&m_levelTimer, &QTimer::timeout, this, &AudioInputService::processBuffer);
    m_levelTimer.setInterval(50);
}

AudioInputService::~AudioInputService()
{
    stop();
}

bool AudioInputService::start(double sensitivity, const QString &preferredDeviceId)
{
    stop();

    QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (!preferredDeviceId.isEmpty()) {
        const auto inputs = QMediaDevices::audioInputs();
        for (const QAudioDevice &candidate : inputs) {
            if (QString::fromUtf8(candidate.id()) == preferredDeviceId) {
                device = candidate;
                break;
            }
        }
    }

    if (device.isNull()) {
        return false;
    }

    m_silenceThreshold = sensitivity;
    m_recordedData.clear();
    m_silenceFrames = 0;
    m_speechStarted = false;
    m_audioSource = std::make_unique<QAudioSource>(device, m_format, this);
    m_ioDevice = m_audioSource->start();
    if (!m_ioDevice) {
        m_audioSource.reset();
        return false;
    }

    m_levelTimer.start();
    return true;
}

void AudioInputService::stop()
{
    m_levelTimer.stop();
    if (m_audioSource) {
        m_audioSource->stop();
        m_audioSource.reset();
    }
    m_ioDevice = nullptr;
}

QByteArray AudioInputService::recordedPcm() const
{
    return m_recordedData;
}

bool AudioInputService::isActive() const
{
    return m_audioSource != nullptr;
}

void AudioInputService::processBuffer()
{
    if (!m_ioDevice) {
        return;
    }

    const QByteArray data = m_ioDevice->readAll();
    if (data.isEmpty()) {
        return;
    }

    m_recordedData.append(data);

    const auto *samples = reinterpret_cast<const qint16 *>(data.constData());
    const int sampleCount = data.size() / static_cast<int>(sizeof(qint16));
    if (sampleCount <= 0) {
        return;
    }

    double sumSquares = 0.0;
    qint16 peak = 0;
    for (int i = 0; i < sampleCount; ++i) {
        const qint16 sample = samples[i];
        sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
        peak = std::max<qint16>(peak, static_cast<qint16>(std::abs(sample)));
    }

    const double rmsValue = qSqrt(sumSquares / static_cast<double>(sampleCount)) / 32768.0;
    const double peakValue = static_cast<double>(peak) / 32768.0;
    emit audioLevelChanged({static_cast<float>(rmsValue), static_cast<float>(peakValue)});

    if (rmsValue >= m_silenceThreshold) {
        m_silenceFrames = 0;
        if (!m_speechStarted) {
            m_speechStarted = true;
            emit speechDetected();
        }
        return;
    }

    if (m_speechStarted && ++m_silenceFrames >= 20) {
        emit speechEnded();
        m_speechStarted = false;
        m_silenceFrames = 0;
    }
}

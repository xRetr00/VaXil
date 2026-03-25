#include "audio/AudioInputService.h"

#include <algorithm>

#include <QtMath>
#include <QMediaDevices>

extern "C" {
#include <fvad.h>
}

namespace {
constexpr int kLevelIntervalMs = 50;
constexpr int kVadFrameMs = 20;
constexpr int kVadFrameSamples = 320;
constexpr int kVadFrameBytes = kVadFrameSamples * static_cast<int>(sizeof(qint16));
constexpr int kMinSpeechMs = 300;
constexpr int kSilenceHoldMs = 800;
constexpr int kIdleCaptureWindowMs = 2500;
constexpr int kMaxSpeechCaptureWindowMs = 9000;
constexpr int kVadMode = 2;
}

AudioInputService::AudioInputService(QObject *parent)
    : QObject(parent)
{
    m_format.setSampleRate(16000);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);

    connect(&m_levelTimer, &QTimer::timeout, this, &AudioInputService::processBuffer);
    m_levelTimer.setInterval(kLevelIntervalMs);
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
    m_vadPendingData.clear();
    m_consecutiveSpeechMs = 0;
    m_consecutiveSilenceMs = 0;
    m_speechStarted = false;
    m_hasDetectedSpeech = false;
    initializeVad();
    if (!m_vad) {
        return false;
    }
    m_audioSource = std::make_unique<QAudioSource>(device, m_format, this);
    m_ioDevice = m_audioSource->start();
    if (!m_ioDevice) {
        clearVad();
        m_audioSource.reset();
        return false;
    }

    m_captureElapsed.restart();
    m_speechElapsed.invalidate();
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
    clearVad();
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
        if (!m_speechStarted && m_captureElapsed.isValid() && m_captureElapsed.elapsed() >= kIdleCaptureWindowMs) {
            m_levelTimer.stop();
            emit captureWindowElapsed(false);
        }
        return;
    }

    m_recordedData.append(data);
    m_vadPendingData.append(data);

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

    if (!m_speechStarted && m_captureElapsed.isValid() && m_captureElapsed.elapsed() >= kIdleCaptureWindowMs) {
        m_levelTimer.stop();
        emit captureWindowElapsed(false);
        return;
    }

    while (m_vadPendingData.size() >= kVadFrameBytes && m_vad) {
        const QByteArray frameData = m_vadPendingData.left(kVadFrameBytes);
        m_vadPendingData.remove(0, kVadFrameBytes);

        const auto *frameSamples = reinterpret_cast<const qint16 *>(frameData.constData());
        double frameSumSquares = 0.0;
        qint16 framePeak = 0;
        for (int i = 0; i < kVadFrameSamples; ++i) {
            const qint16 sample = frameSamples[i];
            frameSumSquares += static_cast<double>(sample) * static_cast<double>(sample);
            framePeak = std::max<qint16>(framePeak, static_cast<qint16>(std::abs(sample)));
        }

        const double frameRms = qSqrt(frameSumSquares / static_cast<double>(kVadFrameSamples)) / 32768.0;
        const double framePeakValue = static_cast<double>(framePeak) / 32768.0;
        const int vadDecision = fvad_process(m_vad, reinterpret_cast<const int16_t *>(frameData.constData()), kVadFrameSamples);
        const bool speechLike = vadDecision == 1 || frameRms >= std::max(m_silenceThreshold * 1.5, 0.03);

        if (speechLike) {
            m_consecutiveSpeechMs += kVadFrameMs;
            m_consecutiveSilenceMs = 0;
        } else {
            m_consecutiveSilenceMs += kVadFrameMs;
            m_consecutiveSpeechMs = 0;
        }

        if (!m_speechStarted && speechLike && m_consecutiveSpeechMs >= kMinSpeechMs) {
            m_speechStarted = true;
            m_hasDetectedSpeech = true;
            m_speechElapsed.restart();
            emit speechDetected();
        }

        if (!m_speechStarted && m_captureElapsed.isValid() && m_captureElapsed.elapsed() >= kIdleCaptureWindowMs) {
            m_levelTimer.stop();
            emit captureWindowElapsed(false);
            return;
        }

        if (m_speechStarted) {
            if (m_speechElapsed.isValid() && m_speechElapsed.elapsed() >= kMaxSpeechCaptureWindowMs) {
                m_levelTimer.stop();
                emit captureWindowElapsed(true);
                return;
            }

            const bool sustainedSilence = m_consecutiveSilenceMs >= kSilenceHoldMs;
            const bool lowEnergySilence = frameRms < m_silenceThreshold && framePeakValue < std::max(m_silenceThreshold * 2.0, 0.06);
            if (sustainedSilence && lowEnergySilence) {
                m_levelTimer.stop();
                emit speechEnded();
                m_speechStarted = false;
                m_consecutiveSilenceMs = 0;
                return;
            }
        }
    }
}

void AudioInputService::initializeVad()
{
    clearVad();

    m_vad = fvad_new();
    if (!m_vad) {
        return;
    }

    if (fvad_set_mode(m_vad, kVadMode) != 0 || fvad_set_sample_rate(m_vad, m_format.sampleRate()) != 0) {
        clearVad();
    }
}

void AudioInputService::clearVad()
{
    if (m_vad) {
        fvad_free(m_vad);
        m_vad = nullptr;
    }

    m_vadPendingData.clear();
    m_consecutiveSpeechMs = 0;
    m_consecutiveSilenceMs = 0;
}

#include "workers/SpeechInputWorker.h"

#include <algorithm>
#include <cmath>

#include <QMediaDevices>

namespace {
constexpr int kSampleRate = 16000;
constexpr int kFrameMs = 20;
constexpr int kFrameSamples = 320;
constexpr int kFrameBytes = kFrameSamples * static_cast<int>(sizeof(qint16));
constexpr int kMinSpeechMs = 300;
constexpr int kSilenceHoldMs = 800;
constexpr int kIdleCaptureWindowMs = 2500;
constexpr int kMaxSpeechCaptureWindowMs = 9000;
}

SpeechInputWorker::SpeechInputWorker(QObject *parent)
    : QObject(parent)
{
    m_format.setSampleRate(kSampleRate);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);
}

void SpeechInputWorker::configure(const AudioProcessingConfig &config)
{
    m_chain.initialize(config);
}

void SpeechInputWorker::startGeneration(quint64 generationId)
{
    m_generationId = generationId;
    m_lastSpeechActive = false;
    m_frameSequence = 0;
}

void SpeechInputWorker::startCapture(quint64 generationId, double sensitivity, const QString &preferredDeviceId)
{
    if (m_captureActive) {
        stopCapture(false);
    }

    m_generationId = generationId;
    m_lastSpeechActive = false;
    m_captureActive = false;
    m_hasDetectedSpeech = false;
    m_silenceThreshold = sensitivity;
    m_consecutiveSpeechMs = 0;
    m_consecutiveSilenceMs = 0;
    m_frameSequence = 0;
    m_recordedPcm.clear();
    m_pendingPcm.clear();

    if (!startAudioDevice(preferredDeviceId)) {
        emit captureFailed(m_generationId, QStringLiteral("No microphone available"));
        return;
    }

    m_captureActive = true;
    m_captureElapsed.restart();
    m_speechElapsed.invalidate();
}

void SpeechInputWorker::stopCapture(bool finalize)
{
    if (!m_captureActive && !finalize) {
        return;
    }

    if (finalize) {
        finishCapture(m_hasDetectedSpeech);
        return;
    }

    stopAudioDevice();
    m_captureActive = false;
    m_recordedPcm.clear();
    m_pendingPcm.clear();
    m_hasDetectedSpeech = false;
    m_lastSpeechActive = false;
    m_consecutiveSpeechMs = 0;
    m_consecutiveSilenceMs = 0;
    m_speechElapsed.invalidate();
    m_captureElapsed.invalidate();
}

void SpeechInputWorker::clearCapture()
{
    stopCapture(false);
}

void SpeechInputWorker::setFarEndFrame(const AudioFrame &frame)
{
    m_chain.setFarEnd(frame);
}

void SpeechInputWorker::setWakeActive(bool active)
{
    m_wakeActive = active;
}

void SpeechInputWorker::reset()
{
    ++m_generationId;
    clearCapture();
}

void SpeechInputWorker::processMicBuffer()
{
    if (!m_audioIoDevice || !m_captureActive) {
        return;
    }

    const QByteArray chunk = m_audioIoDevice->readAll();
    if (chunk.isEmpty()) {
        if (!m_hasDetectedSpeech && m_captureElapsed.isValid() && m_captureElapsed.elapsed() >= kIdleCaptureWindowMs) {
            finishCapture(false);
        }
        return;
    }

    m_recordedPcm.append(chunk);
    m_pendingPcm.append(chunk);

    while (m_pendingPcm.size() >= kFrameBytes) {
        const auto *samples = reinterpret_cast<const qint16 *>(m_pendingPcm.constData());
        const AudioFrame inputFrame = buildFrame(samples, kFrameSamples, ++m_frameSequence);
        m_pendingPcm.remove(0, kFrameBytes);

        AudioFrame processed = m_chain.process(inputFrame);
        emit speechFrame(m_generationId, processed);

        const float rms = [&processed]() {
            if (processed.sampleCount <= 0) {
                return 0.0f;
            }

            float sumSquares = 0.0f;
            for (int i = 0; i < processed.sampleCount; ++i) {
                const float sample = processed.samples[static_cast<size_t>(i)];
                sumSquares += sample * sample;
            }
            return std::sqrt(sumSquares / static_cast<float>(processed.sampleCount));
        }();
        emit audioLevelChanged(m_generationId, {rms, computePeak(processed)});

        if (processed.speechDetected != m_lastSpeechActive) {
            m_lastSpeechActive = processed.speechDetected;
            emit speechActivityChanged(m_generationId, m_lastSpeechActive);
        }

        if (m_wakeActive && processed.speechDetected) {
            emit wakeDetected(m_generationId);
        }

        if (processed.speechDetected) {
            m_consecutiveSpeechMs += kFrameMs;
            m_consecutiveSilenceMs = 0;
        } else {
            m_consecutiveSilenceMs += kFrameMs;
            m_consecutiveSpeechMs = 0;
        }

        if (!m_hasDetectedSpeech && processed.speechDetected && m_consecutiveSpeechMs >= kMinSpeechMs) {
            m_hasDetectedSpeech = true;
            m_speechElapsed.restart();
        }

        if (!m_hasDetectedSpeech && m_captureElapsed.isValid() && m_captureElapsed.elapsed() >= kIdleCaptureWindowMs) {
            finishCapture(false);
            return;
        }

        if (m_hasDetectedSpeech) {
            if (m_speechElapsed.isValid() && m_speechElapsed.elapsed() >= kMaxSpeechCaptureWindowMs) {
                finishCapture(true);
                return;
            }

            const bool sustainedSilence = m_consecutiveSilenceMs >= kSilenceHoldMs;
            const bool lowEnergySilence = rms < static_cast<float>(m_silenceThreshold)
                && computePeak(processed) < std::max(static_cast<float>(m_silenceThreshold * 2.0), 0.06f);
            if (sustainedSilence && lowEnergySilence) {
                finishCapture(true);
                return;
            }
        }
    }
}

void SpeechInputWorker::finishCapture(bool hadSpeech)
{
    const quint64 generationId = m_generationId;
    const QByteArray pcmData = m_recordedPcm;

    stopAudioDevice();
    m_captureActive = false;
    m_recordedPcm.clear();
    m_pendingPcm.clear();
    m_lastSpeechActive = false;
    m_hasDetectedSpeech = false;
    m_consecutiveSpeechMs = 0;
    m_consecutiveSilenceMs = 0;
    m_speechElapsed.invalidate();
    m_captureElapsed.invalidate();

    emit captureFinished(generationId, pcmData, hadSpeech);
}

bool SpeechInputWorker::startAudioDevice(const QString &preferredDeviceId)
{
    QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (!preferredDeviceId.isEmpty()) {
        for (const QAudioDevice &candidate : QMediaDevices::audioInputs()) {
            if (QString::fromUtf8(candidate.id()) == preferredDeviceId) {
                device = candidate;
                break;
            }
        }
    }

    if (device.isNull() || !device.isFormatSupported(m_format)) {
        return false;
    }

    stopAudioDevice();
    m_audioSource = std::make_unique<QAudioSource>(device, m_format, this);
    m_audioSource->setBufferSize(kFrameBytes * 8);
    m_audioIoDevice = m_audioSource->start();
    if (!m_audioIoDevice) {
        m_audioSource.reset();
        return false;
    }

    connect(m_audioIoDevice, &QIODevice::readyRead, this, &SpeechInputWorker::processMicBuffer, Qt::UniqueConnection);
    return true;
}

void SpeechInputWorker::stopAudioDevice()
{
    if (m_audioIoDevice) {
        disconnect(m_audioIoDevice, &QIODevice::readyRead, this, &SpeechInputWorker::processMicBuffer);
    }

    if (m_audioSource) {
        m_audioSource->stop();
        m_audioSource.reset();
    }

    m_audioIoDevice = nullptr;
}

float SpeechInputWorker::computePeak(const AudioFrame &frame) const
{
    float peak = 0.0f;
    for (int i = 0; i < frame.sampleCount; ++i) {
        peak = std::max(peak, std::abs(frame.samples[static_cast<size_t>(i)]));
    }
    return peak;
}

AudioFrame SpeechInputWorker::buildFrame(const qint16 *samples, int sampleCount, qint64 sequence) const
{
    AudioFrame frame;
    frame.sampleCount = std::clamp(sampleCount, 0, AudioFrame::kMaxSamples);
    frame.sampleRate = kSampleRate;
    frame.channels = 1;
    frame.sequence = sequence;

    static constexpr float kScale = 1.0f / 32768.0f;
    for (int i = 0; i < frame.sampleCount; ++i) {
        frame.samples[static_cast<size_t>(i)] = static_cast<float>(samples[i]) * kScale;
    }

    return frame;
}

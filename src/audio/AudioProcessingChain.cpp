#include "audio/AudioProcessingChain.h"

#include <algorithm>
#include <cmath>

extern "C" {
#include <fvad.h>
}

namespace {
constexpr int kSileroWindowSamples = 320;
constexpr float kMinAecMix = 0.08f;
}

AudioProcessingChain::AudioProcessingChain() = default;

AudioProcessingChain::~AudioProcessingChain()
{
    if (m_vad != nullptr) {
        fvad_free(m_vad);
        m_vad = nullptr;
    }
}

void AudioProcessingChain::initialize(const AudioProcessingConfig &config)
{
    m_config = config;
    if (m_vad != nullptr) {
        fvad_free(m_vad);
        m_vad = nullptr;
    }
    m_vad = fvad_new();
    if (m_vad != nullptr) {
        fvad_set_mode(m_vad, 2);
        fvad_set_sample_rate(m_vad, 16000);
    }
    m_farEndSamples.fill(0.0f);
    m_farEndSampleCount = 0;
}

AudioFrame AudioProcessingChain::process(const AudioFrame &in)
{
    AudioFrame out = in;
    out.speechDetected = false;

    const int sampleCount = std::clamp(in.sampleCount, 0, AudioFrame::kMaxSamples);
    if (sampleCount <= 0) {
        out.sampleCount = 0;
        return out;
    }

    if (m_config.aecEnabled && m_farEndSampleCount > 0) {
        const int echoSamples = std::min(sampleCount, m_farEndSampleCount);
        for (int i = 0; i < echoSamples; ++i) {
            out.samples[static_cast<std::size_t>(i)] -= m_farEndSamples[static_cast<std::size_t>(i)] * kMinAecMix;
        }
    }

    if (m_config.noiseSuppressionEnabled) {
        const float noiseFloor = 0.004f + ((1.0f - m_config.vadSensitivity) * 0.018f);
        for (int i = 0; i < sampleCount; ++i) {
            if (std::abs(out.samples[static_cast<std::size_t>(i)]) < noiseFloor) {
                out.samples[static_cast<std::size_t>(i)] = 0.0f;
            }
        }
    }

    if (m_config.rnnoiseEnabled) {
        float previous = 0.0f;
        for (int i = 0; i < sampleCount; ++i) {
            const float current = out.samples[static_cast<std::size_t>(i)];
            out.samples[static_cast<std::size_t>(i)] = (current * 0.82f) + (previous * 0.18f);
            previous = current;
        }
    }

    if (m_config.agcEnabled) {
        const float rms = computeRms(out);
        if (rms > 0.0001f) {
            const float target = 0.12f;
            const float gain = std::clamp(target / rms, 0.7f, 3.2f);
            for (int i = 0; i < sampleCount; ++i) {
                out.samples[static_cast<std::size_t>(i)] = std::clamp(
                    out.samples[static_cast<std::size_t>(i)] * gain,
                    -1.0f,
                    1.0f);
            }
        }
    }

    out.speechDetected = detectVoiceActivity(out);
    return out;
}

void AudioProcessingChain::setFarEnd(const AudioFrame &ttsFrame)
{
    m_farEndSampleCount = std::clamp(ttsFrame.sampleCount, 0, AudioFrame::kMaxSamples);
    for (int i = 0; i < m_farEndSampleCount; ++i) {
        m_farEndSamples[static_cast<std::size_t>(i)] = ttsFrame.samples[static_cast<std::size_t>(i)];
    }
    for (int i = m_farEndSampleCount; i < AudioFrame::kMaxSamples; ++i) {
        m_farEndSamples[static_cast<std::size_t>(i)] = 0.0f;
    }
}

bool AudioProcessingChain::detectVoiceActivity(const AudioFrame &frame)
{
    if (frame.sampleCount < kSileroWindowSamples || m_vad == nullptr) {
        return computeRms(frame) >= std::max(0.012f, m_config.vadSensitivity * 0.05f);
    }

    std::array<int16_t, kSileroWindowSamples> pcm{};
    for (int i = 0; i < kSileroWindowSamples; ++i) {
        const float value = std::clamp(frame.samples[static_cast<std::size_t>(i)], -1.0f, 1.0f);
        pcm[static_cast<std::size_t>(i)] = static_cast<int16_t>(value * 32767.0f);
    }

    const int vad = fvad_process(m_vad, pcm.data(), kSileroWindowSamples);
    if (vad == 1) {
        return true;
    }

    const float rms = computeRms(frame);
    return rms >= std::max(0.012f, m_config.vadSensitivity * 0.05f);
}

float AudioProcessingChain::computeRms(const AudioFrame &frame) const
{
    if (frame.sampleCount <= 0) {
        return 0.0f;
    }

    float sumSquares = 0.0f;
    for (int i = 0; i < frame.sampleCount; ++i) {
        const float sample = frame.samples[static_cast<std::size_t>(i)];
        sumSquares += sample * sample;
    }
    return std::sqrt(sumSquares / static_cast<float>(frame.sampleCount));
}

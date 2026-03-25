#include "audio/AudioProcessingChain.h"

#include <algorithm>
#include <cmath>

#include <QDir>
#include <QFileInfo>

#include "audio/SileroVadSession.h"

extern "C" {
#include <fvad.h>
#include <rnnoise.h>
#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>
#include <speex/speex_resampler.h>
}

namespace {
constexpr int kFvadWindowSamples = 320;
constexpr int kEchoTailMs = 180;
constexpr int kResamplerQuality = 4;

QString resolveSileroModelPath()
{
    const QStringList candidates = {
        QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/third_party/models/silero_vad.onnx"),
        QDir::currentPath() + QStringLiteral("/third_party/models/silero_vad.onnx")
    };

    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }

    return {};
}
}

AudioProcessingChain::AudioProcessingChain()
    : m_sileroVad(std::make_unique<SileroVadSession>())
{
}

AudioProcessingChain::~AudioProcessingChain()
{
    if (m_vad != nullptr) {
        fvad_free(m_vad);
    }
    if (m_echoState != nullptr) {
        speex_echo_state_destroy(m_echoState);
    }
    if (m_preprocessState != nullptr) {
        speex_preprocess_state_destroy(m_preprocessState);
    }
    if (m_farEndResampler != nullptr) {
        speex_resampler_destroy(m_farEndResampler);
    }
    if (m_rnnoise != nullptr) {
        rnnoise_destroy(m_rnnoise);
    }
}

void AudioProcessingChain::initialize(const AudioProcessingConfig &config)
{
    m_config = config;
    m_nativeProcessingEnabled =
#ifdef Q_OS_WIN
        qEnvironmentVariableIntValue("JARVIS_ENABLE_EXPERIMENTAL_AUDIO_DSP") == 1;
#else
        true;
#endif

    if (m_vad != nullptr) {
        fvad_free(m_vad);
        m_vad = nullptr;
    }
    m_vad = fvad_new();
    if (m_vad != nullptr) {
        fvad_set_mode(m_vad, 2);
        fvad_set_sample_rate(m_vad, kProcessSampleRate);
    }

    if (m_echoState != nullptr) {
        speex_echo_state_destroy(m_echoState);
        m_echoState = nullptr;
    }
    if (m_preprocessState != nullptr) {
        speex_preprocess_state_destroy(m_preprocessState);
        m_preprocessState = nullptr;
    }
    if (m_farEndResampler != nullptr) {
        speex_resampler_destroy(m_farEndResampler);
        m_farEndResampler = nullptr;
    }
    if (m_rnnoise != nullptr) {
        rnnoise_destroy(m_rnnoise);
        m_rnnoise = nullptr;
    }

    if (m_nativeProcessingEnabled) {
        m_echoState = speex_echo_state_init(
            kProcessFrameSamples,
            (kProcessSampleRate * kEchoTailMs) / 1000);
        if (m_echoState != nullptr) {
            int sampleRate = kProcessSampleRate;
            speex_echo_ctl(m_echoState, SPEEX_ECHO_SET_SAMPLING_RATE, &sampleRate);
        }

        m_preprocessState = speex_preprocess_state_init(kProcessFrameSamples, kProcessSampleRate);
        configureSpeexPreprocessor();

        m_rnnoise = rnnoise_create(nullptr);
    }
    initializeSileroVad();
    resetProcessingState();
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

    if (sampleCount != kProcessFrameSamples || in.sampleRate != kProcessSampleRate) {
        out.speechDetected = detectVoiceActivity(out, false);
        return out;
    }

    if (!m_nativeProcessingEnabled) {
        out.sampleCount = sampleCount;
        out.sampleRate = in.sampleRate;
        out.channels = in.channels;
        out.speechDetected = detectVoiceActivity(out, false);
        return out;
    }

    for (int i = 0; i < kProcessFrameSamples; ++i) {
        m_inputPcm[static_cast<size_t>(i)] = toInt16Sample(in.samples[static_cast<size_t>(i)]);
        m_aecPcm[static_cast<size_t>(i)] = m_inputPcm[static_cast<size_t>(i)];
    }

    if (m_config.aecEnabled && m_echoState != nullptr) {
        speex_echo_capture(m_echoState, m_inputPcm.data(), m_aecPcm.data());
    }

    std::copy(m_aecPcm.begin(), m_aecPcm.end(), m_preprocessPcm.begin());
    bool speexVadDetected = false;
    if (m_preprocessState != nullptr) {
        speexVadDetected = speex_preprocess_run(m_preprocessState, m_preprocessPcm.data()) == 1;
    }

    if (m_config.rnnoiseEnabled && m_rnnoise != nullptr) {
        for (int i = 0; i < kProcessFrameSamples; ++i) {
            m_rnnoiseFrame[static_cast<size_t>(i)] = static_cast<float>(m_preprocessPcm[static_cast<size_t>(i)]);
        }
        rnnoise_process_frame(m_rnnoise, m_rnnoiseOutput.data(), m_rnnoiseFrame.data());
        for (int i = 0; i < kProcessFrameSamples; ++i) {
            out.samples[static_cast<size_t>(i)] = std::clamp(
                m_rnnoiseOutput[static_cast<size_t>(i)] / 32768.0f,
                -1.0f,
                1.0f);
        }
    } else {
        for (int i = 0; i < kProcessFrameSamples; ++i) {
            out.samples[static_cast<size_t>(i)] = static_cast<float>(m_preprocessPcm[static_cast<size_t>(i)]) / 32768.0f;
        }
    }

    out.sampleCount = kProcessFrameSamples;
    out.sampleRate = kProcessSampleRate;
    out.channels = 1;
    out.speechDetected = detectVoiceActivity(out, speexVadDetected);
    return out;
}

void AudioProcessingChain::setFarEnd(const AudioFrame &ttsFrame)
{
    if (!m_nativeProcessingEnabled || !m_config.aecEnabled || m_echoState == nullptr || ttsFrame.sampleCount <= 0) {
        return;
    }

    const float *inputSamples = ttsFrame.samples.data();
    int inputSampleCount = std::clamp(ttsFrame.sampleCount, 0, AudioFrame::kMaxSamples);
    int inputRate = ttsFrame.sampleRate > 0 ? ttsFrame.sampleRate : kProcessSampleRate;

    if (inputRate != kProcessSampleRate) {
        if (!ensureFarEndResampler(inputRate)) {
            return;
        }

        spx_uint32_t inLen = static_cast<spx_uint32_t>(inputSampleCount);
        spx_uint32_t outLen = static_cast<spx_uint32_t>(m_farEndResampled.size());
        if (speex_resampler_process_float(
                m_farEndResampler,
                0,
                inputSamples,
                &inLen,
                m_farEndResampled.data(),
                &outLen) != RESAMPLER_ERR_SUCCESS) {
            return;
        }

        inputSamples = m_farEndResampled.data();
        inputSampleCount = static_cast<int>(outLen);
    }

    for (int i = 0; i < inputSampleCount; ++i) {
        m_farEndPlaybackPcm[static_cast<size_t>(m_farEndPlaybackSampleCount++)] = toInt16Sample(inputSamples[static_cast<size_t>(i)]);
        if (m_farEndPlaybackSampleCount == kProcessFrameSamples) {
            speex_echo_playback(m_echoState, m_farEndPlaybackPcm.data());
            m_farEndPlaybackSampleCount = 0;
        }
    }
}

void AudioProcessingChain::resetProcessingState()
{
    m_farEndPlaybackSampleCount = 0;
    m_inputPcm.fill(0);
    m_aecPcm.fill(0);
    m_preprocessPcm.fill(0);
    m_farEndPlaybackPcm.fill(0);
    m_rnnoiseFrame.fill(0.0f);
    m_rnnoiseOutput.fill(0.0f);
    m_farEndResampled.fill(0.0f);
    m_farEndResamplerInputRate = 0;

    if (m_echoState != nullptr) {
        speex_echo_state_reset(m_echoState);
    }
    if (m_preprocessState != nullptr) {
        configureSpeexPreprocessor();
    }
}

void AudioProcessingChain::configureSpeexPreprocessor()
{
    if (m_preprocessState == nullptr) {
        return;
    }

    const int denoise = m_config.noiseSuppressionEnabled ? 1 : 0;
    const int agc = m_config.agcEnabled ? 1 : 0;
    const int vad = 1;
    const int noiseSuppressDb = m_config.noiseSuppressionEnabled ? -28 : 0;
    const int echoSuppressDb = m_config.aecEnabled ? -40 : 0;
    const int echoSuppressActiveDb = m_config.aecEnabled ? -18 : 0;
    const int probabilityStart = std::clamp(
        static_cast<int>(65.0f - (m_config.vadSensitivity * 30.0f)),
        28,
        80);
    const int probabilityContinue = std::clamp(probabilityStart - 12, 18, 72);
    const int agcLevel = 16000;
    const int agcIncrement = 12;
    const int agcDecrement = -18;
    const int agcMaxGain = 18;

    speex_preprocess_ctl(m_preprocessState, SPEEX_PREPROCESS_SET_DENOISE, const_cast<int *>(&denoise));
    speex_preprocess_ctl(m_preprocessState, SPEEX_PREPROCESS_SET_AGC, const_cast<int *>(&agc));
    speex_preprocess_ctl(m_preprocessState, SPEEX_PREPROCESS_SET_VAD, const_cast<int *>(&vad));
    speex_preprocess_ctl(m_preprocessState, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, const_cast<int *>(&noiseSuppressDb));
    speex_preprocess_ctl(m_preprocessState, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS, const_cast<int *>(&echoSuppressDb));
    speex_preprocess_ctl(m_preprocessState, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS_ACTIVE, const_cast<int *>(&echoSuppressActiveDb));
    speex_preprocess_ctl(m_preprocessState, SPEEX_PREPROCESS_SET_PROB_START, const_cast<int *>(&probabilityStart));
    speex_preprocess_ctl(m_preprocessState, SPEEX_PREPROCESS_SET_PROB_CONTINUE, const_cast<int *>(&probabilityContinue));
    speex_preprocess_ctl(m_preprocessState, SPEEX_PREPROCESS_SET_AGC_TARGET, const_cast<int *>(&agcLevel));
    speex_preprocess_ctl(m_preprocessState, SPEEX_PREPROCESS_SET_AGC_INCREMENT, const_cast<int *>(&agcIncrement));
    speex_preprocess_ctl(m_preprocessState, SPEEX_PREPROCESS_SET_AGC_DECREMENT, const_cast<int *>(&agcDecrement));
    speex_preprocess_ctl(m_preprocessState, SPEEX_PREPROCESS_SET_AGC_MAX_GAIN, const_cast<int *>(&agcMaxGain));

    SpeexEchoState *echoState = m_config.aecEnabled ? m_echoState : nullptr;
    speex_preprocess_ctl(m_preprocessState, SPEEX_PREPROCESS_SET_ECHO_STATE, &echoState);
}

bool AudioProcessingChain::ensureFarEndResampler(int inputRate)
{
    if (inputRate == kProcessSampleRate) {
        return true;
    }

    if (m_farEndResampler != nullptr && m_farEndResamplerInputRate == inputRate) {
        return true;
    }

    if (m_farEndResampler != nullptr) {
        speex_resampler_destroy(m_farEndResampler);
        m_farEndResampler = nullptr;
    }

    int error = RESAMPLER_ERR_SUCCESS;
    m_farEndResampler = speex_resampler_init(
        1,
        static_cast<spx_uint32_t>(inputRate),
        static_cast<spx_uint32_t>(kProcessSampleRate),
        kResamplerQuality,
        &error);
    if (error != RESAMPLER_ERR_SUCCESS || m_farEndResampler == nullptr) {
        m_farEndResamplerInputRate = 0;
        return false;
    }

    m_farEndResamplerInputRate = inputRate;
    speex_resampler_skip_zeros(m_farEndResampler);
    return true;
}

bool AudioProcessingChain::detectVoiceActivity(const AudioFrame &frame, bool speexVadDetected)
{
    if (speexVadDetected) {
        return true;
    }

    if (m_sileroVad && m_sileroVad->isReady()) {
        const float probability = m_sileroVad->inferProbability(
            frame.samples.data(),
            frame.sampleCount,
            frame.sampleRate);
        const float threshold = std::clamp(0.32f + ((1.0f - m_config.vadSensitivity) * 0.30f), 0.28f, 0.72f);
        if (probability >= threshold) {
            return true;
        }
    }

    if (frame.sampleCount < kFvadWindowSamples || m_vad == nullptr) {
        return computeRms(frame) >= std::max(0.012f, m_config.vadSensitivity * 0.045f);
    }

    std::array<int16_t, kFvadWindowSamples> pcm{};
    for (int i = 0; i < kFvadWindowSamples; ++i) {
        pcm[static_cast<size_t>(i)] = toInt16Sample(frame.samples[static_cast<size_t>(i)]);
    }

    if (fvad_process(m_vad, pcm.data(), kFvadWindowSamples) == 1) {
        return true;
    }

    return computeRms(frame) >= std::max(0.012f, m_config.vadSensitivity * 0.045f);
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

bool AudioProcessingChain::initializeSileroVad()
{
    if (!m_sileroVad) {
        return false;
    }

    const QString modelPath = resolveSileroModelPath();
    if (modelPath.isEmpty()) {
        m_sileroVad->reset();
        return false;
    }

    if (!m_sileroVad->initialize(modelPath)) {
        m_sileroVad->reset();
        return false;
    }

    return true;
}

qint16 AudioProcessingChain::toInt16Sample(float value) const
{
    return static_cast<qint16>(std::clamp(value * 32768.0f, -32768.0f, 32767.0f));
}

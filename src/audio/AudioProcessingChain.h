#pragma once

#include <array>
#include <memory>

#include "audio/AudioProcessingTypes.h"

struct Fvad;
struct DenoiseState;
struct SpeexEchoState_;
struct SpeexPreprocessState_;
struct SpeexResamplerState_;
class SileroVadSession;

using SpeexEchoState = SpeexEchoState_;
using SpeexPreprocessState = SpeexPreprocessState_;
using SpeexResamplerState = SpeexResamplerState_;

class AudioProcessingChain
{
public:
    AudioProcessingChain();
    ~AudioProcessingChain();

    void initialize(const AudioProcessingConfig &config);
    AudioFrame process(const AudioFrame &in);
    void setFarEnd(const AudioFrame &ttsFrame);

private:
    static constexpr int kProcessSampleRate = 16000;
    static constexpr int kProcessFrameSamples = 480;

    void resetProcessingState();
    void configureSpeexPreprocessor();
    bool ensureFarEndResampler(int inputRate);
    bool detectVoiceActivity(const AudioFrame &frame, bool speexVadDetected);
    float computeRms(const AudioFrame &frame) const;
    bool initializeSileroVad();
    qint16 toInt16Sample(float value) const;

    AudioProcessingConfig m_config;
    Fvad *m_vad = nullptr;
    std::unique_ptr<SileroVadSession> m_sileroVad;
    SpeexEchoState *m_echoState = nullptr;
    SpeexPreprocessState *m_preprocessState = nullptr;
    SpeexResamplerState *m_farEndResampler = nullptr;
    DenoiseState *m_rnnoise = nullptr;
    int m_farEndResamplerInputRate = 0;
    std::array<qint16, kProcessFrameSamples> m_inputPcm{};
    std::array<qint16, kProcessFrameSamples> m_aecPcm{};
    std::array<qint16, kProcessFrameSamples> m_preprocessPcm{};
    std::array<qint16, kProcessFrameSamples> m_farEndPlaybackPcm{};
    std::array<float, kProcessFrameSamples> m_rnnoiseFrame{};
    std::array<float, kProcessFrameSamples> m_rnnoiseOutput{};
    std::array<float, AudioFrame::kMaxSamples * 2> m_farEndResampled{};
    int m_farEndPlaybackSampleCount = 0;
    bool m_nativeProcessingEnabled = false;
};

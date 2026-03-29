#include "audio/SileroVadSession.h"

#include <algorithm>
#include <array>

#if JARVIS_HAS_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace {
constexpr int kSupportedSampleRate = 16000;
constexpr int kWindowSamples = 512;
constexpr int kStateSize = 2 * 1 * 128;
}

struct SileroVadSession::Impl
{
#if JARVIS_HAS_ONNXRUNTIME
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "vaxil-silero-vad"};
    Ort::SessionOptions sessionOptions;
    std::unique_ptr<Ort::Session> session;
    std::array<float, kWindowSamples> input{};
    std::array<float, kStateSize> state{};
    std::array<int64_t, 2> inputShape{1, kWindowSamples};
    std::array<int64_t, 3> stateShape{2, 1, 128};
    std::array<int64_t, 1> sampleRateShape{1};
    std::array<int64_t, 1> sampleRateValue{kSupportedSampleRate};
    std::array<float, 1> output{};
    std::array<float, kStateSize> outputState{};
    std::array<int64_t, 2> outputShape{1, 1};
    std::array<int64_t, 3> outputStateShape{2, 1, 128};
#endif
};

SileroVadSession::SileroVadSession()
    : m_impl(std::make_unique<Impl>())
{
}

SileroVadSession::~SileroVadSession() = default;

bool SileroVadSession::initialize(const QString &modelPath)
{
#if !JARVIS_HAS_ONNXRUNTIME
    Q_UNUSED(modelPath)
    m_ready = false;
    return false;
#else
    m_modelPath = modelPath;
    m_pendingCount = 0;
    m_lastProbability = 0.0f;
    m_impl->sessionOptions.SetIntraOpNumThreads(1);
    m_impl->sessionOptions.SetInterOpNumThreads(1);
    m_impl->sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);
    try {
        m_impl->session = std::make_unique<Ort::Session>(
            m_impl->env,
            modelPath.toStdWString().c_str(),
            m_impl->sessionOptions);
        std::fill(m_impl->state.begin(), m_impl->state.end(), 0.0f);
        m_ready = true;
        return true;
    } catch (...) {
        m_impl->session.reset();
        m_ready = false;
        return false;
    }
#endif
}

void SileroVadSession::reset()
{
    m_pendingCount = 0;
    m_lastProbability = 0.0f;
#if JARVIS_HAS_ONNXRUNTIME
    std::fill(m_impl->state.begin(), m_impl->state.end(), 0.0f);
#endif
}

bool SileroVadSession::isReady() const
{
    return m_ready;
}

float SileroVadSession::inferProbability(const float *samples, int sampleCount, int sampleRate)
{
    if (!m_ready || samples == nullptr || sampleCount <= 0 || sampleRate != kSupportedSampleRate) {
        return m_lastProbability;
    }

    const int copyCount = std::min(sampleCount, kWindowSamples);
    if (copyCount >= kWindowSamples) {
        std::copy_n(samples + (sampleCount - kWindowSamples), kWindowSamples, m_pendingSamples.begin());
        m_pendingCount = kWindowSamples;
    } else {
        if (m_pendingCount + copyCount > kWindowSamples) {
            const int overflow = (m_pendingCount + copyCount) - kWindowSamples;
            std::move(m_pendingSamples.begin() + overflow, m_pendingSamples.begin() + m_pendingCount, m_pendingSamples.begin());
            m_pendingCount -= overflow;
        }
        std::copy_n(samples + (sampleCount - copyCount), copyCount, m_pendingSamples.begin() + m_pendingCount);
        m_pendingCount += copyCount;
    }

    if (m_pendingCount < kWindowSamples) {
        return m_lastProbability;
    }

#if !JARVIS_HAS_ONNXRUNTIME
    return m_lastProbability;
#else
    std::copy(m_pendingSamples.begin(), m_pendingSamples.end(), m_impl->input.begin());
    try {
        Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::array<const char *, 3> inputNames{"input", "state", "sr"};
        std::array<const char *, 2> outputNames{"output", "stateN"};
        std::array<Ort::Value, 3> inputValues{
            Ort::Value::CreateTensor<float>(memoryInfo, m_impl->input.data(), m_impl->input.size(), m_impl->inputShape.data(), m_impl->inputShape.size()),
            Ort::Value::CreateTensor<float>(memoryInfo, m_impl->state.data(), m_impl->state.size(), m_impl->stateShape.data(), m_impl->stateShape.size()),
            Ort::Value::CreateTensor<int64_t>(memoryInfo, m_impl->sampleRateValue.data(), m_impl->sampleRateValue.size(), m_impl->sampleRateShape.data(), m_impl->sampleRateShape.size())
        };

        auto outputs = m_impl->session->Run(
            Ort::RunOptions{nullptr},
            inputNames.data(),
            inputValues.data(),
            inputValues.size(),
            outputNames.data(),
            outputNames.size());

        const float *probability = outputs[0].GetTensorData<float>();
        const float *stateOut = outputs[1].GetTensorData<float>();
        m_lastProbability = probability != nullptr ? probability[0] : 0.0f;
        if (stateOut != nullptr) {
            std::copy_n(stateOut, m_impl->state.size(), m_impl->state.begin());
        }
    } catch (...) {
        m_lastProbability = 0.0f;
    }
    return m_lastProbability;
#endif
}

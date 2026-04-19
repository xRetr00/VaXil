#include "tts/WorkerTtsEngine.h"

#include "workers/VoicePipelineRuntime.h"

WorkerTtsEngine::WorkerTtsEngine(VoicePipelineRuntime *runtime, QObject *parent)
    : TtsEngine(parent)
    , m_runtime(runtime)
{
    connect(m_runtime, &VoicePipelineRuntime::playbackStarted, this, [this](quint64 generationId) {
        if (generationId != m_activeGeneration) {
            return;
        }
        m_speaking = true;
        emit playbackStarted();
    });
    connect(m_runtime, &VoicePipelineRuntime::playbackFinished, this, [this](quint64 generationId) {
        if (generationId != m_activeGeneration) {
            return;
        }
        m_speaking = false;
        emit playbackFinished();
    });
    connect(m_runtime, &VoicePipelineRuntime::playbackFailed, this, [this](quint64 generationId, const QString &errorText) {
        if (generationId != m_activeGeneration) {
            return;
        }
        m_speaking = false;
        emit playbackFailed(errorText);
    });
    connect(m_runtime, &VoicePipelineRuntime::farEndFrameReady, this, [this](quint64 generationId, const AudioFrame &frame) {
        if (generationId == m_activeGeneration) {
            emit farEndFrameReady(frame);
        }
    });
}

void WorkerTtsEngine::speakText(const QString &text, const TtsUtteranceContext &context)
{
    if (text.trimmed().isEmpty()) {
        return;
    }

    m_activeGeneration = ++m_generationCounter;
    m_speaking = true;
    m_runtime->speak(m_activeGeneration, text, context);
}

void WorkerTtsEngine::clear()
{
    ++m_generationCounter;
    m_activeGeneration = 0;
    m_speaking = false;
    m_runtime->cancelSpeechIo();
}

bool WorkerTtsEngine::isSpeaking() const
{
    return m_speaking;
}

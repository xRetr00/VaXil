#include "stt/RuntimeSpeechRecognizer.h"

#include "workers/VoicePipelineRuntime.h"

RuntimeSpeechRecognizer::RuntimeSpeechRecognizer(VoicePipelineRuntime *runtime, QObject *parent)
    : SpeechRecognizer(parent)
    , m_runtime(runtime)
{
    connect(m_runtime, &VoicePipelineRuntime::transcriptionReady, this, &RuntimeSpeechRecognizer::transcriptionReady);
    connect(m_runtime, &VoicePipelineRuntime::transcriptionFailed, this, &RuntimeSpeechRecognizer::transcriptionFailed);
}

quint64 RuntimeSpeechRecognizer::transcribePcm(const QByteArray &pcmData, const QString &initialPrompt, bool suppressNonSpeechTokens)
{
    const quint64 requestId = ++m_requestCounter;
    m_runtime->transcribe(requestId, pcmData, initialPrompt, suppressNonSpeechTokens);
    return requestId;
}

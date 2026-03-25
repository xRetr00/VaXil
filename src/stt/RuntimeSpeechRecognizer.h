#pragma once

#include "stt/SpeechRecognizer.h"

class VoicePipelineRuntime;

class RuntimeSpeechRecognizer : public SpeechRecognizer
{
    Q_OBJECT

public:
    explicit RuntimeSpeechRecognizer(VoicePipelineRuntime *runtime, QObject *parent = nullptr);

    quint64 transcribePcm(
        const QByteArray &pcmData,
        const QString &initialPrompt = {},
        bool suppressNonSpeechTokens = true) override;

private:
    VoicePipelineRuntime *m_runtime = nullptr;
    quint64 m_requestCounter = 0;
};

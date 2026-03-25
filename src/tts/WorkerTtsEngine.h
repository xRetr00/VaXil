#pragma once

#include "tts/TtsEngine.h"

class VoicePipelineRuntime;

class WorkerTtsEngine : public TtsEngine
{
    Q_OBJECT

public:
    explicit WorkerTtsEngine(VoicePipelineRuntime *runtime, QObject *parent = nullptr);

    void speakText(const QString &text) override;
    void clear() override;
    bool isSpeaking() const override;

private:
    VoicePipelineRuntime *m_runtime = nullptr;
    quint64 m_generationCounter = 0;
    quint64 m_activeGeneration = 0;
    bool m_speaking = false;
};

#include "workers/SpeechInputWorker.h"

SpeechInputWorker::SpeechInputWorker(QObject *parent)
    : QObject(parent)
{
}

void SpeechInputWorker::configure(const AudioProcessingConfig &config)
{
    m_chain.initialize(config);
}

void SpeechInputWorker::startGeneration(quint64 generationId)
{
    m_generationId = generationId;
    m_lastSpeechActive = false;
}

void SpeechInputWorker::ingestMicFrame(const AudioFrame &frame)
{
    AudioFrame processed = m_chain.process(frame);
    emit speechFrame(m_generationId, processed);

    if (processed.speechDetected != m_lastSpeechActive) {
        m_lastSpeechActive = processed.speechDetected;
        emit speechActivityChanged(m_generationId, m_lastSpeechActive);
    }

    if (m_wakeActive && processed.speechDetected) {
        emit wakeDetected(m_generationId);
    }
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
    m_lastSpeechActive = false;
}

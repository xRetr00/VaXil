#pragma once

#include <QObject>

#include "audio/AudioProcessingChain.h"

class SpeechInputWorker : public QObject
{
    Q_OBJECT

public:
    explicit SpeechInputWorker(QObject *parent = nullptr);

public slots:
    void configure(const AudioProcessingConfig &config);
    void startGeneration(quint64 generationId);
    void ingestMicFrame(const AudioFrame &frame);
    void setFarEndFrame(const AudioFrame &frame);
    void setWakeActive(bool active);
    void reset();

signals:
    void wakeDetected(quint64 generationId);
    void speechFrame(quint64 generationId, const AudioFrame &frame);
    void speechActivityChanged(quint64 generationId, bool active);

private:
    AudioProcessingChain m_chain;
    quint64 m_generationId = 0;
    bool m_wakeActive = false;
    bool m_lastSpeechActive = false;
};

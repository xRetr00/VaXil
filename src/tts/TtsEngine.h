#pragma once

#include <QObject>

#include "audio/AudioProcessingTypes.h"

class TtsEngine : public QObject
{
    Q_OBJECT

public:
    explicit TtsEngine(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~TtsEngine() override = default;

    virtual void speakText(const QString &text) = 0;
    virtual void clear() = 0;
    virtual bool isSpeaking() const = 0;

signals:
    void playbackStarted();
    void playbackFinished();
    void playbackFailed(const QString &errorText);
    void farEndFrameReady(const AudioFrame &frame);
};

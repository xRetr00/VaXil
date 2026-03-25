#pragma once

#include <QObject>

#include "core/AssistantTypes.h"

class SpeechRecognizer : public QObject
{
    Q_OBJECT

public:
    explicit SpeechRecognizer(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~SpeechRecognizer() override = default;

    virtual quint64 transcribePcm(
        const QByteArray &pcmData,
        const QString &initialPrompt = {},
        bool suppressNonSpeechTokens = true) = 0;

signals:
    void transcriptionReady(quint64 requestId, const TranscriptionResult &result);
    void transcriptionFailed(quint64 requestId, const QString &errorText);
};

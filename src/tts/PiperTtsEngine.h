#pragma once

#include <QBuffer>
#include <QFutureWatcher>
#include <QQueue>
#include <QString>
#include <QTimer>

#include "tts/TtsEngine.h"

struct TtsSynthesisResult
{
    QString outputFile;
    quint64 generation = 0;
};

class AppSettings;
class QAudioSink;

class PiperTtsEngine : public TtsEngine
{
    Q_OBJECT

public:
    explicit PiperTtsEngine(AppSettings *settings, QObject *parent = nullptr);

    void speakText(const QString &text) override;
    void clear() override;
    bool isSpeaking() const override;

private:
    void applySelectedOutputDevice();
    void processNext();
    TtsSynthesisResult synthesizeAndProcess(const QString &text, quint64 generation) const;
    void playFile(const QString &path);
    void stopPlayback();

    AppSettings *m_settings = nullptr;
    QQueue<QString> m_pendingTexts;
    bool m_processing = false;
    QFutureWatcher<TtsSynthesisResult> m_synthesisWatcher;
    QAudioSink *m_audioSink = nullptr;
    QBuffer *m_playbackBuffer = nullptr;
    QByteArray m_playbackPcm;
    QTimer m_farEndTimer;
    qint64 m_lastFarEndOffset = 0;
    quint64 m_generationCounter = 0;
    quint64 m_activeGeneration = 0;
};

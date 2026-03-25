#pragma once

#include <QObject>
#include <QFutureWatcher>
#include <QQueue>
#include <QString>

struct TtsSynthesisResult
{
    QString outputFile;
    quint64 generation = 0;
};

class AppSettings;
class QAudioOutput;
class QMediaPlayer;

class PiperTtsEngine : public QObject
{
    Q_OBJECT

public:
    explicit PiperTtsEngine(AppSettings *settings, QObject *parent = nullptr);

    void enqueueSentence(const QString &sentence);
    void clear();
    bool isSpeaking() const;

signals:
    void playbackStarted();
    void playbackFinished();
    void playbackFailed(const QString &errorText);

private:
    void applySelectedOutputDevice();
    void processNext();
    TtsSynthesisResult synthesizeAndProcess(const QString &sentence, quint64 generation) const;
    void playFile(const QString &path);

    AppSettings *m_settings = nullptr;
    QQueue<QString> m_sentences;
    bool m_processing = false;
    QFutureWatcher<TtsSynthesisResult> m_synthesisWatcher;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    quint64 m_generationCounter = 0;
    quint64 m_activeGeneration = 0;
};

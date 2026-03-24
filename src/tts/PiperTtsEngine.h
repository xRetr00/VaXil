#pragma once

#include <QObject>
#include <QFutureWatcher>
#include <QQueue>

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
    QString synthesizeAndProcess(const QString &sentence);
    void playFile(const QString &path);

    AppSettings *m_settings = nullptr;
    QQueue<QString> m_sentences;
    bool m_processing = false;
    QFutureWatcher<QString> m_synthesisWatcher;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
};

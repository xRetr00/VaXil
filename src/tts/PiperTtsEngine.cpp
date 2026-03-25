#include "tts/PiperTtsEngine.h"

#include <algorithm>

#include <QtConcurrent>
#include <QAudioDevice>
#include <QAudioOutput>
#include <QDir>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>
#include <QUrl>

#include "settings/AppSettings.h"

namespace {
QString collapseWhitespace(const QString &text)
{
    QString normalized = text;
    normalized.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return normalized.trimmed();
}

QString stripHiddenReasoning(const QString &text)
{
    QString cleaned = text;
    cleaned.replace(QRegularExpression(QStringLiteral("(?is)<think>.*?</think>")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("(?im)^\\s*(reasoning|analysis|thought process)\\s*:\\s*.*$")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("(?im)^\\s*```(?:json|text|markdown)?\\s*$")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("(?im)^\\s*```\\s*$")), QStringLiteral(" "));
    cleaned.replace(QStringLiteral("/no_think"), QStringLiteral(" "));
    return cleaned;
}

QString removeNonSpeechArtifacts(const QString &text)
{
    QString cleaned = text;
    cleaned.replace(QRegularExpression(QStringLiteral("https?://\\S+")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("[`*_#~]+")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("[\\[\\]{}<>|]+")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("\\b[A-Z_]{2,}\\b(?=\\s|$)")), QStringLiteral(" "));
    return cleaned;
}

QString ensureSentenceCase(QString text)
{
    bool capitalize = true;
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i).isLetter() && capitalize) {
            text[i] = text.at(i).toUpper();
            capitalize = false;
            continue;
        }

        if (QStringLiteral(".!?").contains(text.at(i))) {
            capitalize = true;
        }
    }
    return text;
}

QString normalizeSpeechText(QString text)
{
    QString cleaned = stripHiddenReasoning(text);
    cleaned.replace(QRegularExpression(QStringLiteral("\\s*\\n+\\s*")), QStringLiteral(". "));
    cleaned = removeNonSpeechArtifacts(cleaned);
    cleaned.replace(QRegularExpression(QStringLiteral("\\s*([,.;:!?])\\s*")), QStringLiteral("\\1 "));
    cleaned.replace(QRegularExpression(QStringLiteral("\\s*\\.\\.\\.\\s*")), QStringLiteral("... "));
    cleaned.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    cleaned = cleaned.trimmed();

    if (cleaned.isEmpty()) {
        return {};
    }

    cleaned = ensureSentenceCase(cleaned);
    if (!cleaned.endsWith(QChar::fromLatin1('.'))
        && !cleaned.endsWith(QChar::fromLatin1('!'))
        && !cleaned.endsWith(QChar::fromLatin1('?'))) {
        cleaned += QChar::fromLatin1('.');
    }

    return cleaned;
}

QString styleFormatJarvisResponse(const QString &text)
{
    QString formatted = normalizeSpeechText(text);
    if (formatted.isEmpty()) {
        return {};
    }

    formatted.replace(QRegularExpression(QStringLiteral("\\b(uh|um|you know|kind of|sort of|basically|actually|literally|like)\\b"),
                                         QRegularExpression::CaseInsensitiveOption),
                      QStringLiteral(""));
    formatted.replace(QRegularExpression(QStringLiteral("\\b(okay|ok)\\b"), QRegularExpression::CaseInsensitiveOption),
                      QStringLiteral("Understood"));
    formatted = collapseWhitespace(formatted);
    formatted = formatted.trimmed();

    const QStringList sentences = formatted.split(QRegularExpression(QStringLiteral("(?<=[.!?])\\s+")), Qt::SkipEmptyParts);
    QStringList conciseSentences;
    conciseSentences.reserve(3);

    for (const QString &rawSentence : sentences) {
        QString sentence = rawSentence.trimmed();
        if (sentence.isEmpty()) {
            continue;
        }

        if (!sentence.endsWith(QChar::fromLatin1('.'))
            && !sentence.endsWith(QChar::fromLatin1('!'))
            && !sentence.endsWith(QChar::fromLatin1('?'))) {
            sentence += QChar::fromLatin1('.');
        }

        sentence = ensureSentenceCase(sentence);
        conciseSentences.push_back(sentence);
    }

    return conciseSentences.join(QStringLiteral(" "));
}

QString injectNaturalPauses(const QString &text)
{
    QString withPauses = text;
    if (withPauses.isEmpty()) {
        return {};
    }

    withPauses.replace(QStringLiteral(": "), QStringLiteral("... "));
    withPauses.replace(QRegularExpression(QStringLiteral("\\s*-\\s*")), QStringLiteral("... "));
    withPauses.replace(QRegularExpression(QStringLiteral("\\b(please wait|stand by)\\b"),
                                          QRegularExpression::CaseInsensitiveOption),
                       QStringLiteral("please wait"));
    withPauses.replace(QRegularExpression(QStringLiteral("\\b(and|but|while|however|meanwhile)\\b"),
                                          QRegularExpression::CaseInsensitiveOption),
                       QStringLiteral(", \\1"));
    withPauses.replace(QRegularExpression(QStringLiteral("\\b(good morning|good afternoon|good evening)([^,.!?])"),
                                          QRegularExpression::CaseInsensitiveOption),
                       QStringLiteral("\\1, \\2"));
    withPauses.replace(QRegularExpression(QStringLiteral("\\b(processing your request|running the sequence|one moment)\\b"),
                                          QRegularExpression::CaseInsensitiveOption),
                       QStringLiteral("\\1..."));
    withPauses.replace(QRegularExpression(QStringLiteral("\\s+,")), QStringLiteral(","));
    withPauses.replace(QRegularExpression(QStringLiteral(",\\s*,+")), QStringLiteral(", "));
    withPauses = collapseWhitespace(withPauses);

    return withPauses.trimmed();
}

QString applyVoicePipeline(const QString &aiResponse)
{
    const QString styled = styleFormatJarvisResponse(aiResponse);
    const QString paused = injectNaturalPauses(styled);
    return normalizeSpeechText(paused);
}
}

PiperTtsEngine::PiperTtsEngine(AppSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);
    applySelectedOutputDevice();

    connect(&m_synthesisWatcher, &QFutureWatcher<TtsSynthesisResult>::finished, this, [this]() {
        const TtsSynthesisResult result = m_synthesisWatcher.result();
        if (result.generation != m_activeGeneration || !m_processing) {
            return;
        }

        if (result.outputFile.isEmpty()) {
            m_processing = false;
            m_activeGeneration = 0;
            emit playbackFailed(QStringLiteral("Failed to synthesize audio"));
            return;
        }

        playFile(result.outputFile);
    });

    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (!m_processing) {
            return;
        }
        if (status == QMediaPlayer::EndOfMedia || status == QMediaPlayer::InvalidMedia) {
            processNext();
        }
    });
}

void PiperTtsEngine::enqueueSentence(const QString &sentence)
{
    const QString prepared = applyVoicePipeline(sentence);
    if (prepared.isEmpty()) {
        return;
    }

    m_sentences.enqueue(prepared);
    if (!m_processing) {
        processNext();
    }
}

void PiperTtsEngine::clear()
{
    ++m_generationCounter;
    m_activeGeneration = 0;
    m_sentences.clear();
    m_processing = false;
    if (m_player) {
        m_player->stop();
    }
}

bool PiperTtsEngine::isSpeaking() const
{
    return m_processing || !m_sentences.isEmpty();
}

void PiperTtsEngine::processNext()
{
    if (m_sentences.isEmpty()) {
        m_processing = false;
        m_activeGeneration = 0;
        emit playbackFinished();
        return;
    }

    if (m_settings->piperExecutable().isEmpty() || m_settings->piperVoiceModel().isEmpty()) {
        m_sentences.clear();
        m_processing = false;
        emit playbackFailed(QStringLiteral("Piper executable or voice model is not configured"));
        return;
    }

    m_processing = true;
    applySelectedOutputDevice();
    emit playbackStarted();
    const QString sentence = m_sentences.dequeue();
    const quint64 generation = ++m_generationCounter;
    m_activeGeneration = generation;
    m_synthesisWatcher.setFuture(QtConcurrent::run([this, sentence, generation]() {
        return synthesizeAndProcess(sentence, generation);
    }));
}

TtsSynthesisResult PiperTtsEngine::synthesizeAndProcess(const QString &sentence, quint64 generation) const
{
    TtsSynthesisResult result;
    result.generation = generation;

    const auto tempRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir().mkpath(tempRoot);
    const QString token = QStringLiteral("%1_%2")
        .arg(generation)
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString rawPath = tempRoot + QStringLiteral("/jarvis_tts_%1_raw.wav").arg(token);
    const QString processedPath = tempRoot + QStringLiteral("/jarvis_tts_%1_processed.wav").arg(token);

    {
        QProcess process;
        process.start(m_settings->piperExecutable(), {
            QStringLiteral("--model"), m_settings->piperVoiceModel(),
            QStringLiteral("--output_file"), rawPath,
            QStringLiteral("--length_scale"), QString::number(1.0 / std::max(0.1, m_settings->voiceSpeed()), 'f', 3)
        });
        process.write(sentence.toUtf8());
        process.closeWriteChannel();
        if (!process.waitForFinished(10000)) {
            process.kill();
            process.waitForFinished(1000);
            return result;
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            return result;
        }
    }

    if (m_settings->ffmpegExecutable().isEmpty()) {
        result.outputFile = rawPath;
        return result;
    }

    const QString filter = QStringLiteral(
                               "asetrate=22050*%1,"
                               "aresample=22050,"
                               "volume=1.4,"
                               "highpass=f=65,"
                               "lowpass=f=7600,"
                               "equalizer=f=240:t=q:w=1.0:g=0.7,"
                               "equalizer=f=3200:t=q:w=1.2:g=-1.5,"
                               "equalizer=f=5400:t=q:w=1.0:g=-0.7,"
                               "aecho=0.78:0.26:24:0.05,"
                               "acompressor=threshold=-20dB:ratio=2.4:attack=12:release=160:makeup=2,"
                               "alimiter=limit=0.92")
                               .arg(QString::number(m_settings->voicePitch(), 'f', 2));

    QProcess process;
    process.start(m_settings->ffmpegExecutable(), {
        QStringLiteral("-y"),
        QStringLiteral("-i"), rawPath,
        QStringLiteral("-af"),
        filter,
        processedPath
    });
    if (!process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished(1000);
        result.outputFile = rawPath;
        return result;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        result.outputFile = rawPath;
        return result;
    }

    result.outputFile = processedPath;
    return result;
}

void PiperTtsEngine::playFile(const QString &path)
{
    m_player->setSource(QUrl::fromLocalFile(path));
    m_player->play();
}

void PiperTtsEngine::applySelectedOutputDevice()
{
    if (!m_audioOutput || !m_settings) {
        return;
    }

    const QString selectedId = m_settings->selectedAudioOutputDeviceId();
    if (selectedId.isEmpty()) {
        m_audioOutput->setDevice(QMediaDevices::defaultAudioOutput());
        return;
    }

    for (const QAudioDevice &device : QMediaDevices::audioOutputs()) {
        if (QString::fromUtf8(device.id()) == selectedId) {
            m_audioOutput->setDevice(device);
            return;
        }
    }

    m_audioOutput->setDevice(QMediaDevices::defaultAudioOutput());
}

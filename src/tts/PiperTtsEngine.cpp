#include "tts/PiperTtsEngine.h"

#include <algorithm>

#include <QtConcurrent>
#include <QAudioOutput>
#include <QDir>
#include <QMediaPlayer>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

#include "settings/AppSettings.h"

namespace {
QString normalizeSpeechText(QString text)
{
    QString cleaned = text;
    cleaned.replace(QRegularExpression(QStringLiteral("[`*_#~]+")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("\\s*\\n+\\s*")), QStringLiteral(". "));
    cleaned = cleaned.trimmed();

    if (cleaned.isEmpty()) {
        return {};
    }

    // Keep speech concise and naturally paced.
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

    // Remove common filler language so delivery stays precise.
    formatted.replace(QRegularExpression(QStringLiteral("\\b(uh|um|you know|like)\\b"), QRegularExpression::CaseInsensitiveOption), QStringLiteral(""));
    formatted.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    formatted = formatted.trimmed();

    // Keep voice responses concise and controlled.
    const QStringList sentences = formatted.split(QRegularExpression(QStringLiteral("(?<=[.!?])\\s+")), Qt::SkipEmptyParts);
    QStringList conciseSentences;
    conciseSentences.reserve(3);

    for (const QString &rawSentence : sentences) {
        if (conciseSentences.size() >= 3) {
            break;
        }

        QString sentence = rawSentence.trimmed();
        if (sentence.isEmpty()) {
            continue;
        }

        const QStringList words = sentence.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (words.size() > 18) {
            sentence = words.mid(0, 18).join(QStringLiteral(" ")) + QStringLiteral(".");
        }

        if (!sentence.endsWith(QChar::fromLatin1('.'))
            && !sentence.endsWith(QChar::fromLatin1('!'))
            && !sentence.endsWith(QChar::fromLatin1('?'))) {
            sentence += QChar::fromLatin1('.');
        }

        sentence[0] = sentence.at(0).toUpper();
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

    // Create short and long pause opportunities.
    withPauses.replace(QStringLiteral(": "), QStringLiteral("... "));
    withPauses.replace(QRegularExpression(QStringLiteral("\\s*-\\s*")), QStringLiteral(", "));
    withPauses.replace(QRegularExpression(QStringLiteral("\\bplease\\b"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("please"));
    withPauses.replace(QRegularExpression(QStringLiteral("\\b(and|but|while|however)\\b"), QRegularExpression::CaseInsensitiveOption), QStringLiteral(", \\1"));
    withPauses.replace(QRegularExpression(QStringLiteral("\\s+,")), QStringLiteral(","));
    withPauses.replace(QRegularExpression(QStringLiteral(",\\s*,+")), QStringLiteral(", "));
    withPauses.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));

    return withPauses.trimmed();
}

QString applyVoicePipeline(const QString &aiResponse)
{
    // Full pipeline: AI response -> style formatter -> pause injection -> TTS input.
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

    connect(&m_synthesisWatcher, &QFutureWatcher<QString>::finished, this, [this]() {
        const QString outputFile = m_synthesisWatcher.result();
        if (outputFile.isEmpty()) {
            m_processing = false;
            emit playbackFailed(QStringLiteral("Failed to synthesize audio"));
            return;
        }

        playFile(outputFile);
    });

    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
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
    m_sentences.clear();
    if (m_player) {
        m_player->stop();
    }
    m_processing = false;
}

bool PiperTtsEngine::isSpeaking() const
{
    return m_processing || !m_sentences.isEmpty();
}

void PiperTtsEngine::processNext()
{
    if (m_sentences.isEmpty()) {
        m_processing = false;
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
    emit playbackStarted();
    const QString sentence = m_sentences.dequeue();
    m_synthesisWatcher.setFuture(QtConcurrent::run([this, sentence]() {
        return synthesizeAndProcess(sentence);
    }));
}

QString PiperTtsEngine::synthesizeAndProcess(const QString &sentence)
{
    const auto tempRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir().mkpath(tempRoot);
    const QString rawPath = tempRoot + QStringLiteral("/jarvis_tts_raw.wav");
    const QString processedPath = tempRoot + QStringLiteral("/jarvis_tts_processed.wav");

    {
        QProcess process;
        process.start(m_settings->piperExecutable(), {
            QStringLiteral("--model"), m_settings->piperVoiceModel(),
            QStringLiteral("--output_file"), rawPath,
            QStringLiteral("--length_scale"), QString::number(1.0 / std::max(0.1, m_settings->voiceSpeed()), 'f', 2)
        });
        process.write(sentence.toUtf8());
        process.closeWriteChannel();
        process.waitForFinished(10000);
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            return {};
        }
    }

    if (m_settings->ffmpegExecutable().isEmpty()) {
        return rawPath;
    }

    const QString filter = QStringLiteral("asetrate=22050*%1,aresample=22050,highpass=f=70,lowpass=f=9500,equalizer=f=3600:t=q:w=1.1:g=-1.2,equalizer=f=180:t=q:w=1.2:g=1.0,aecho=0.75:0.45:35:0.12,acompressor=threshold=-18dB:ratio=2.0:attack=20:release=220:makeup=1.5")
                               .arg(QString::number(m_settings->voicePitch(), 'f', 2));

    QProcess process;
    process.start(m_settings->ffmpegExecutable(), {
        QStringLiteral("-y"),
        QStringLiteral("-i"), rawPath,
        QStringLiteral("-af"),
        filter,
        processedPath
    });
    process.waitForFinished(10000);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return rawPath;
    }

    return processedPath;
}

void PiperTtsEngine::playFile(const QString &path)
{
    m_player->setSource(QUrl::fromLocalFile(path));
    m_player->play();
}

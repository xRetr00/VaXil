#include "tts/PiperTtsEngine.h"

#include <algorithm>
#include <cstring>

#include <QtConcurrent>
#include <QAudio>
#include <QAudioFormat>
#include <QAudioDevice>
#include <QAudioSink>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QMediaDevices>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

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

bool parseWaveFile(const QString &path, QByteArray *pcmData, QAudioFormat *format)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QByteArray data = file.readAll();
    if (data.size() < 44 || !data.startsWith("RIFF") || data.mid(8, 4) != "WAVE") {
        return false;
    }

    int offset = 12;
    quint16 channels = 1;
    quint32 sampleRate = 22050;
    quint16 bitsPerSample = 16;
    int dataOffset = -1;
    int dataSize = 0;

    while (offset + 8 <= data.size()) {
        const QByteArray chunkId = data.mid(offset, 4);
        quint32 chunkSize = 0;
        std::memcpy(&chunkSize, data.constData() + offset + 4, sizeof(chunkSize));
        offset += 8;

        if (chunkId == "fmt " && offset + 16 <= data.size()) {
            std::memcpy(&channels, data.constData() + offset + 2, sizeof(channels));
            std::memcpy(&sampleRate, data.constData() + offset + 4, sizeof(sampleRate));
            std::memcpy(&bitsPerSample, data.constData() + offset + 14, sizeof(bitsPerSample));
        } else if (chunkId == "data") {
            dataOffset = offset;
            dataSize = static_cast<int>(chunkSize);
            break;
        }

        offset += static_cast<int>(chunkSize);
    }

    if (dataOffset < 0 || dataSize <= 0 || dataOffset + dataSize > data.size()) {
        return false;
    }

    format->setSampleRate(static_cast<int>(sampleRate));
    format->setChannelCount(static_cast<int>(channels));
    format->setSampleFormat(bitsPerSample == 16 ? QAudioFormat::Int16 : QAudioFormat::UInt8);
    *pcmData = data.mid(dataOffset, dataSize);
    return true;
}
}

PiperTtsEngine::PiperTtsEngine(AppSettings *settings, QObject *parent)
    : TtsEngine(parent)
    , m_settings(settings)
{
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

    m_farEndTimer.setInterval(10);
    connect(&m_farEndTimer, &QTimer::timeout, this, [this]() {
        if (m_playbackBuffer == nullptr || m_playbackPcm.isEmpty()) {
            return;
        }

        const qint64 currentOffset = std::clamp(m_playbackBuffer->pos(), static_cast<qint64>(0), static_cast<qint64>(m_playbackPcm.size()));
        const qint64 bytesPerFrame = 320 * static_cast<qint64>(sizeof(qint16));
        while (m_lastFarEndOffset + bytesPerFrame <= currentOffset) {
            AudioFrame frame;
            frame.sampleRate = 16000;
            frame.channels = 1;
            frame.sampleCount = 320;
            const auto *samples = reinterpret_cast<const qint16 *>(m_playbackPcm.constData() + m_lastFarEndOffset);
            for (int i = 0; i < frame.sampleCount; ++i) {
                frame.samples[static_cast<std::size_t>(i)] = static_cast<float>(samples[i]) / 32768.0f;
            }
            emit farEndFrameReady(frame);
            m_lastFarEndOffset += bytesPerFrame;
        }
    });
}

void PiperTtsEngine::speakText(const QString &text)
{
    const QString prepared = applyVoicePipeline(text);
    if (prepared.isEmpty()) {
        return;
    }

    m_pendingTexts.enqueue(prepared);
    if (!m_processing) {
        processNext();
    }
}

void PiperTtsEngine::clear()
{
    ++m_generationCounter;
    m_activeGeneration = 0;
    m_pendingTexts.clear();
    m_processing = false;
    stopPlayback();
}

bool PiperTtsEngine::isSpeaking() const
{
    return m_processing || !m_pendingTexts.isEmpty();
}

void PiperTtsEngine::processNext()
{
    if (m_pendingTexts.isEmpty()) {
        m_processing = false;
        m_activeGeneration = 0;
        emit playbackFinished();
        return;
    }

    if (m_settings->piperExecutable().isEmpty() || m_settings->piperVoiceModel().isEmpty()) {
        m_pendingTexts.clear();
        m_processing = false;
        emit playbackFailed(QStringLiteral("Piper executable or voice model is not configured"));
        return;
    }

    m_processing = true;
    emit playbackStarted();
    const QString sentence = m_pendingTexts.dequeue();
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
    stopPlayback();

    QByteArray pcmData;
    QAudioFormat format;
    if (!parseWaveFile(path, &pcmData, &format)) {
        m_processing = false;
        emit playbackFailed(QStringLiteral("Failed to parse synthesized audio"));
        return;
    }

    QAudioDevice device = QMediaDevices::defaultAudioOutput();
    const QString selectedId = m_settings != nullptr ? m_settings->selectedAudioOutputDeviceId() : QString{};
    if (!selectedId.isEmpty()) {
        for (const QAudioDevice &candidate : QMediaDevices::audioOutputs()) {
            if (QString::fromUtf8(candidate.id()) == selectedId) {
                device = candidate;
                break;
            }
        }
    }

    m_audioSink = new QAudioSink(device, format, this);
    m_playbackPcm = pcmData;
    m_playbackBuffer = new QBuffer(this);
    m_playbackBuffer->setData(m_playbackPcm);
    m_playbackBuffer->open(QIODevice::ReadOnly);
    m_lastFarEndOffset = 0;

    connect(m_audioSink, &QAudioSink::stateChanged, this, [this](QAudio::State state) {
        if (state == QAudio::IdleState) {
            stopPlayback();
            processNext();
        } else if (state == QAudio::StoppedState && m_audioSink != nullptr && m_audioSink->error() != QtAudio::NoError) {
            stopPlayback();
            m_processing = false;
            emit playbackFailed(QStringLiteral("Audio playback failed"));
        }
    });

    m_audioSink->start(m_playbackBuffer);
    m_farEndTimer.start();
}

void PiperTtsEngine::applySelectedOutputDevice()
{
}

void PiperTtsEngine::stopPlayback()
{
    m_farEndTimer.stop();
    m_lastFarEndOffset = 0;
    if (m_audioSink != nullptr) {
        m_audioSink->stop();
        m_audioSink->deleteLater();
        m_audioSink = nullptr;
    }
    if (m_playbackBuffer != nullptr) {
        m_playbackBuffer->close();
        m_playbackBuffer->deleteLater();
        m_playbackBuffer = nullptr;
    }
    m_playbackPcm.clear();
}

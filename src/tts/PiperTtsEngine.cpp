#include "tts/PiperTtsEngine.h"

#include <algorithm>
#include <cmath>
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
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

#include "logging/LoggingService.h"
#include "settings/AppSettings.h"

namespace {
struct VoicePipelineTrace
{
    QString styled;
    QString paused;
    QString normalized;
    QStringList decimalTokens;
    int normalizedPointCount = 0;
    int normalizedDotWordCount = 0;
};

QString collapseWhitespace(const QString &text)
{
    QString normalized = text;
    normalized.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return normalized.trimmed();
}

QString ensureTerminalPunctuation(QString text)
{
    text = collapseWhitespace(text);
    if (!text.isEmpty()
        && !text.endsWith(QChar::fromLatin1('.'))
        && !text.endsWith(QChar::fromLatin1('!'))
        && !text.endsWith(QChar::fromLatin1('?'))) {
        text += QChar::fromLatin1('.');
    }
    return text;
}

QString canonicalSpeechKey(const QString &text)
{
    QString key = text.toLower();
    key.remove(QRegularExpression(QStringLiteral("[^a-z0-9]")));
    return key;
}

QString expandLetterAcronym(const QString &token)
{
    QStringList letters;
    letters.reserve(token.size());
    for (const QChar ch : token) {
        letters.push_back(QString(ch));
    }
    return letters.join(QChar::fromLatin1(' '));
}

bool isStatusOnlyUtterance(const QString &text)
{
    static const QStringList statusKeys = {
        QStringLiteral("listening"),
        QStringLiteral("processingrequest"),
        QStringLiteral("responseready"),
        QStringLiteral("standingby"),
        QStringLiteral("requestcancelled"),
        QStringLiteral("commandexecuted"),
        QStringLiteral("loadingservices"),
        QStringLiteral("settingssaved"),
        QStringLiteral("unabletostartlistening"),
        QStringLiteral("transcribedby")
    };

    const QString key = canonicalSpeechKey(text);
    return !key.isEmpty() && statusKeys.contains(key);
}

QString rewriteDotForSpeech(QString text)
{
    // Preserve decimal and identifier pronunciation so `5.4` and `model.v1` are not spoken as long pauses.
    text.replace(QRegularExpression(QStringLiteral("(?<=\\d)\\.(?=\\d)")), QStringLiteral(" point "));
    text.replace(QRegularExpression(QStringLiteral("(?<=[A-Za-z0-9_])\\.(?=[A-Za-z_][A-Za-z0-9_]*)")), QStringLiteral(" dot "));
    return text;
}

QString rewriteTimesForSpeech(const QString &text)
{
    const QRegularExpression timePattern(
        QStringLiteral("\\b(\\d{1,2}):(\\d{2})(?:\\s*([AaPp])\\.?\\s*([Mm])\\.?)?\\b"));
    QString rewritten;
    rewritten.reserve(text.size() + 16);

    int lastEnd = 0;
    const auto matches = timePattern.globalMatch(text);
    for (auto it = matches; it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        rewritten += text.mid(lastEnd, match.capturedStart() - lastEnd);

        const QString hour = match.captured(1);
        const QString minute = match.captured(2);
        QString replacement = hour + QStringLiteral(" ") + minute;
        if (!match.captured(3).isEmpty() && !match.captured(4).isEmpty()) {
            replacement += QStringLiteral(" ")
                + match.captured(3).toLower()
                + match.captured(4).toLower();
        }

        rewritten += replacement;
        lastEnd = match.capturedEnd();
    }

    rewritten += text.mid(lastEnd);
    return rewritten;
}

QString expandAcronymsForSpeech(const QString &text)
{
    const QRegularExpression acronymPattern(QStringLiteral("\\b([A-Z]{2,6})(?=(?:\\s|$|[.,!?;:]))"));
    QString rewritten;
    rewritten.reserve(text.size() + 16);

    int lastEnd = 0;
    const auto matches = acronymPattern.globalMatch(text);
    for (auto it = matches; it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        const QString token = match.captured(1);
        rewritten += text.mid(lastEnd, match.capturedStart() - lastEnd);

        if (token == QStringLiteral("AM") || token == QStringLiteral("PM")) {
            rewritten += token.toLower();
        } else {
            rewritten += expandLetterAcronym(token);
        }

        lastEnd = match.capturedEnd();
    }

    rewritten += text.mid(lastEnd);
    return rewritten;
}

QString stripHiddenReasoning(const QString &text)
{
    QString cleaned = text;
    cleaned.replace(QRegularExpression(QStringLiteral("(?is)<think>.*?(?=\\b(?:assistant|final)\\s*:|</think>|$)")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("(?is)</?think>")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("(?im)^\\s*(reasoning|analysis|thought process)\\s*:\\s*.*$")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("(?im)^\\s*```(?:json|text|markdown)?\\s*$")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("(?im)^\\s*```\\s*$")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("(?im)^\\s*(assistant|system|developer)\\s*:\\s*")), QStringLiteral(" "));
    cleaned.replace(QStringLiteral("/no_think"), QStringLiteral(" "));
    return cleaned;
}

QString removeNonSpeechArtifacts(const QString &text)
{
    QString cleaned = text;
    cleaned.replace(QRegularExpression(QStringLiteral("https?://\\S+")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("[`*_#~]+")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("[\\[\\]{}<>|]+")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("\\b[A-Z]+_[A-Z0-9_]+\\b(?=\\s|$)")), QStringLiteral(" "));
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
    cleaned = rewriteTimesForSpeech(cleaned);
    cleaned.replace(QRegularExpression(QStringLiteral("(?<![A-Za-z0-9])['\"“”‘’]+|['\"“”‘’]+(?![A-Za-z0-9])")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("(?<=[A-Za-z0-9])[_/](?=[A-Za-z0-9])")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("(?<=[A-Za-z])-(?=[A-Za-z0-9])")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("(?<=[0-9])-(?=[A-Za-z])")), QStringLiteral(" "));
    cleaned = rewriteDotForSpeech(cleaned);
    cleaned = expandAcronymsForSpeech(cleaned);
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
    constexpr int kMaxSpokenSentences = 3;
    constexpr int kMaxSpokenChars = 280;
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
    int totalChars = 0;
    bool truncated = false;

    for (const QString &rawSentence : sentences) {
        QString sentence = rawSentence.trimmed();
        if (sentence.isEmpty()) {
            continue;
        }

        if (conciseSentences.size() >= kMaxSpokenSentences) {
            truncated = true;
            break;
        }

        sentence = ensureSentenceCase(ensureTerminalPunctuation(sentence));
        const int nextChars = totalChars + (conciseSentences.isEmpty() ? 0 : 1) + sentence.size();
        if (nextChars > kMaxSpokenChars) {
            if (conciseSentences.isEmpty()) {
                sentence = ensureTerminalPunctuation(sentence.left(kMaxSpokenChars).trimmed());
                if (!sentence.isEmpty()) {
                    conciseSentences.push_back(sentence);
                }
            }
            truncated = true;
            break;
        }

        conciseSentences.push_back(sentence);
        totalChars = nextChars;
    }

    QString result = conciseSentences.join(QStringLiteral(" "));
    if (result.isEmpty()) {
        result = ensureTerminalPunctuation(formatted.left(kMaxSpokenChars).trimmed());
        truncated = formatted.size() > result.size();
    }
    if (truncated && !result.isEmpty()) {
        result = ensureTerminalPunctuation(result);
        result += QStringLiteral(" The rest is on screen.");
    }

    return result;
}

QString injectNaturalPauses(const QString &text)
{
    QString withPauses = text;
    if (withPauses.isEmpty()) {
        return {};
    }

    withPauses.replace(QRegularExpression(QStringLiteral("(?<=[A-Za-z\\)])\\s*:\\s+")), QStringLiteral(", "));
    withPauses.replace(QRegularExpression(QStringLiteral("\\s+[\\-–—]\\s+")), QStringLiteral(", "));
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

int countRegexMatches(const QString &text, const QRegularExpression &pattern)
{
    int count = 0;
    auto it = pattern.globalMatch(text);
    while (it.hasNext()) {
        it.next();
        ++count;
    }
    return count;
}

VoicePipelineTrace analyzeVoicePipeline(const QString &aiResponse)
{
    VoicePipelineTrace trace;
    trace.styled = styleFormatJarvisResponse(aiResponse);
    trace.paused = injectNaturalPauses(trace.styled);
    trace.normalized = normalizeSpeechText(trace.paused);

    const QRegularExpression decimalTokenPattern(QStringLiteral("\\b\\d+(?:\\.\\d+)+\\b"));
    auto matchIt = decimalTokenPattern.globalMatch(aiResponse);
    while (matchIt.hasNext()) {
        const QString token = matchIt.next().captured(0).trimmed();
        if (!token.isEmpty()) {
            trace.decimalTokens.push_back(token);
        }
    }

    trace.normalizedPointCount = countRegexMatches(
        trace.normalized,
        QRegularExpression(QStringLiteral("\\b\\d+\\s+point\\s+\\d+\\b"),
                           QRegularExpression::CaseInsensitiveOption));
    trace.normalizedDotWordCount = countRegexMatches(
        trace.normalized,
        QRegularExpression(QStringLiteral("\\bdot\\b"), QRegularExpression::CaseInsensitiveOption));
    return trace;
}

QString clipForTtsLog(QString text, int maxChars = 1200)
{
    if (text.size() > maxChars) {
        text = text.left(maxChars) + QStringLiteral(" ...[truncated]");
    }
    return text;
}

QString summarizePauseHints(const QString &text)
{
    QStringList points;
    points.reserve(8);
    int count = 0;
    for (int i = 0; i < text.size() && points.size() < 8; ++i) {
        QString token;
        int width = 1;
        const QChar ch = text.at(i);
        if (ch == QChar::fromLatin1(',')) {
            token = QStringLiteral("comma");
        } else if (ch == QChar::fromLatin1(';')) {
            token = QStringLiteral("semicolon");
        } else if (ch == QChar::fromLatin1(':')) {
            token = QStringLiteral("colon");
        } else if (ch == QChar::fromLatin1('.')) {
            if (i + 2 < text.size()
                && text.at(i + 1) == QChar::fromLatin1('.')
                && text.at(i + 2) == QChar::fromLatin1('.')) {
                token = QStringLiteral("ellipsis");
                width = 3;
                i += 2;
            } else {
                token = QStringLiteral("period");
            }
        } else if (ch == QChar::fromLatin1('!')) {
            token = QStringLiteral("exclamation");
        } else if (ch == QChar::fromLatin1('?')) {
            token = QStringLiteral("question");
        }

        if (!token.isEmpty()) {
            ++count;
            const int start = std::max(0, i - 14);
            const int len = std::min(34, static_cast<int>(text.size()) - start);
            const QString context = text.mid(start, len).simplified();
            points.push_back(QStringLiteral("%1@%2\"%3\"").arg(token, QString::number(i), context));
        }
        i += width - 1;
    }

    return QStringLiteral("pause_points=%1 sample=[%2]")
        .arg(count)
        .arg(points.join(QStringLiteral(" | ")));
}

QString summarizeWaveSilence(const QByteArray &pcmData, const QAudioFormat &format)
{
    if (format.sampleFormat() != QAudioFormat::Int16 || format.bytesPerSample() != 2 || format.sampleRate() <= 0) {
        return QStringLiteral("silence_analysis=unsupported_format");
    }

    const int channelCount = std::max(1, format.channelCount());
    const int samplesPerChannel = pcmData.size() / (2 * channelCount);
    if (samplesPerChannel <= 0) {
        return QStringLiteral("silence_analysis=empty_audio");
    }

    const int frameSamples = std::max(1, format.sampleRate() / 100);
    const int frameCount = samplesPerChannel / frameSamples;
    if (frameCount <= 0) {
        return QStringLiteral("silence_analysis=short_audio");
    }

    const qint16 *samples = reinterpret_cast<const qint16 *>(pcmData.constData());
    const double silenceThreshold = 0.0035;
    const int minSilentFrames = 8;
    int silentRunStart = -1;
    QStringList segments;

    auto finalizeRun = [&](int endFrame) {
        if (silentRunStart < 0) {
            return;
        }
        const int runFrames = endFrame - silentRunStart;
        if (runFrames >= minSilentFrames) {
            const double startSec = static_cast<double>(silentRunStart * frameSamples) / static_cast<double>(format.sampleRate());
            const double durationSec = static_cast<double>(runFrames * frameSamples) / static_cast<double>(format.sampleRate());
            if (segments.size() < 6) {
                segments.push_back(QStringLiteral("%1s(+%2s)")
                                       .arg(QString::number(startSec, 'f', 2),
                                            QString::number(durationSec, 'f', 2)));
            }
        }
        silentRunStart = -1;
    };

    int silentSegmentCount = 0;
    for (int frame = 0; frame < frameCount; ++frame) {
        double absSum = 0.0;
        const int baseSample = frame * frameSamples;
        for (int i = 0; i < frameSamples; ++i) {
            const int sampleIndex = baseSample + i;
            if (sampleIndex >= samplesPerChannel) {
                break;
            }
            int mixed = 0;
            for (int c = 0; c < channelCount; ++c) {
                mixed += static_cast<int>(samples[sampleIndex * channelCount + c]);
            }
            const double mono = static_cast<double>(mixed) / static_cast<double>(channelCount * 32768.0);
            absSum += std::abs(mono);
        }

        const double avgAbs = absSum / static_cast<double>(frameSamples);
        const bool isSilent = avgAbs < silenceThreshold;
        if (isSilent && silentRunStart < 0) {
            silentRunStart = frame;
        }
        if (!isSilent && silentRunStart >= 0) {
            const int runFrames = frame - silentRunStart;
            if (runFrames >= minSilentFrames) {
                ++silentSegmentCount;
            }
            finalizeRun(frame);
        }
    }

    if (silentRunStart >= 0) {
        const int runFrames = frameCount - silentRunStart;
        if (runFrames >= minSilentFrames) {
            ++silentSegmentCount;
        }
        finalizeRun(frameCount);
    }

    return QStringLiteral("silence_segments=%1 sample=[%2]")
        .arg(QString::number(silentSegmentCount), segments.join(QStringLiteral(" | ")));
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

PiperTtsEngine::PiperTtsEngine(AppSettings *settings,
                               LoggingService *loggingService,
                               QObject *parent)
    : TtsEngine(parent)
    , m_settings(settings)
    , m_loggingService(loggingService)
{
    m_farEndTimer = new QTimer(this);
    connect(&m_synthesisWatcher, &QFutureWatcher<TtsSynthesisResult>::finished, this, [this]() {
        const TtsSynthesisResult result = m_synthesisWatcher.result();
        if (result.generation != m_activeGeneration || !m_processing) {
            return;
        }

        if (result.outputFile.isEmpty()) {
            m_processing = false;
            m_activeGeneration = 0;
            emit playbackFailed(result.errorText.trimmed().isEmpty()
                ? QStringLiteral("Failed to synthesize audio")
                : result.errorText.trimmed());
            return;
        }

        playFile(result.outputFile);
    });

    m_farEndTimer->setInterval(10);
    connect(m_farEndTimer, &QTimer::timeout, this, [this]() {
        if (m_playbackBuffer == nullptr || m_playbackPcm.isEmpty()) {
            return;
        }

        const qint64 currentOffset = std::clamp(m_playbackBuffer->pos(), static_cast<qint64>(0), static_cast<qint64>(m_playbackPcm.size()));
        if (m_playbackFormat.sampleRate() <= 0 || m_playbackFormat.bytesPerSample() <= 0) {
            return;
        }

        const int chunkSamples = std::clamp(
            (m_playbackFormat.sampleRate() * 30) / 1000,
            1,
            AudioFrame::kMaxSamples);
        const int channelCount = std::max(1, m_playbackFormat.channelCount());
        const qint64 bytesPerFrame = static_cast<qint64>(chunkSamples)
            * static_cast<qint64>(m_playbackFormat.bytesPerSample())
            * static_cast<qint64>(channelCount);
        while (m_lastFarEndOffset + bytesPerFrame <= currentOffset) {
            AudioFrame frame;
            frame.sampleRate = m_playbackFormat.sampleRate();
            frame.channels = 1;
            frame.sampleCount = chunkSamples;
            const auto *samples = reinterpret_cast<const qint16 *>(m_playbackPcm.constData() + m_lastFarEndOffset);
            for (int i = 0; i < frame.sampleCount; ++i) {
                int mixed = 0;
                for (int channel = 0; channel < channelCount; ++channel) {
                    mixed += samples[(i * channelCount) + channel];
                }
                frame.samples[static_cast<std::size_t>(i)] = static_cast<float>(mixed) / static_cast<float>(channelCount * 32768.0f);
            }
            emit farEndFrameReady(frame);
            m_lastFarEndOffset += bytesPerFrame;
        }
    });
}

void PiperTtsEngine::speakText(const QString &text)
{
    const VoicePipelineTrace trace = analyzeVoicePipeline(text);
    const QString prepared = trace.normalized;
    if (m_loggingService) {
        m_loggingService->infoFor(
            QStringLiteral("tts"),
            QStringLiteral("[tts_pipeline] rawChars=%1 styledChars=%2 pausedChars=%3 normalizedChars=%4 decimalTokens=%5 normalizedPointPhrases=%6 normalizedDotWords=%7")
                .arg(QString::number(text.size()),
                     QString::number(trace.styled.size()),
                     QString::number(trace.paused.size()),
                     QString::number(prepared.size()),
                     QString::number(trace.decimalTokens.size()),
                     QString::number(trace.normalizedPointCount),
                     QString::number(trace.normalizedDotWordCount)));
        m_loggingService->infoFor(
            QStringLiteral("tts"),
            QStringLiteral("[tts_text_raw] %1").arg(clipForTtsLog(text)));
        m_loggingService->infoFor(
            QStringLiteral("tts"),
            QStringLiteral("[tts_text_spoken] %1").arg(clipForTtsLog(prepared)));
        if (!trace.decimalTokens.isEmpty()) {
            m_loggingService->infoFor(
                QStringLiteral("tts"),
                QStringLiteral("[tts_decimal_tokens] %1").arg(trace.decimalTokens.join(QStringLiteral(", "))));
        }
        m_loggingService->infoFor(
            QStringLiteral("tts"),
            QStringLiteral("[tts_pause_hints] %1").arg(summarizePauseHints(prepared)));
    }

    if (prepared.isEmpty() || isStatusOnlyUtterance(prepared)) {
        if (m_loggingService) {
            m_loggingService->infoFor(
                QStringLiteral("tts"),
                QStringLiteral("[tts_pipeline_skipped] reason=%1")
                    .arg(prepared.isEmpty() ? QStringLiteral("empty_after_normalization") : QStringLiteral("status_only_utterance")));
        }
        emit playbackFinished();
        return;
    }

    m_pendingTexts.enqueue(prepared);
    if (m_loggingService) {
        m_loggingService->infoFor(
            QStringLiteral("tts"),
            QStringLiteral("[tts_queue] enqueued=true queueSize=%1")
                .arg(QString::number(m_pendingTexts.size())));
    }
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
    if (m_loggingService) {
        m_loggingService->infoFor(QStringLiteral("tts"), QStringLiteral("[tts_clear] queue_cleared=true"));
    }
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
        if (m_loggingService) {
            m_loggingService->infoFor(QStringLiteral("tts"), QStringLiteral("[tts_queue] drained=true"));
        }
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
    if (m_loggingService) {
        m_loggingService->infoFor(
            QStringLiteral("tts"),
            QStringLiteral("[tts_speak] generation=%1 remainingQueue=%2 text=%3")
                .arg(QString::number(generation),
                     QString::number(m_pendingTexts.size()),
                     clipForTtsLog(sentence)));
    }
    m_synthesisWatcher.setFuture(QtConcurrent::run([this, sentence, generation]() {
        return synthesizeAndProcess(sentence, generation);
    }));
}

TtsSynthesisResult PiperTtsEngine::synthesizeAndProcess(const QString &sentence, quint64 generation) const
{
    TtsSynthesisResult result;
    result.generation = generation;

    const QString piperExecutable = m_settings != nullptr ? m_settings->piperExecutable().trimmed() : QString{};
    const QString voiceModelPath = m_settings != nullptr ? m_settings->piperVoiceModel().trimmed() : QString{};

    if (piperExecutable.isEmpty() || voiceModelPath.isEmpty()) {
        result.errorText = QStringLiteral("Piper executable or voice model is not configured");
        if (m_loggingService) {
            m_loggingService->warnFor(QStringLiteral("tts"), QStringLiteral("[tts_synthesis_failed] generation=%1 reason=missing_configuration")
                                                            .arg(QString::number(generation)));
        }
        return result;
    }

    if (!QFileInfo::exists(piperExecutable)) {
        result.errorText = QStringLiteral("Piper executable was not found at %1").arg(piperExecutable);
        if (m_loggingService) {
            m_loggingService->warnFor(QStringLiteral("tts"), QStringLiteral("[tts_synthesis_failed] generation=%1 reason=missing_executable")
                                                            .arg(QString::number(generation)));
        }
        return result;
    }

    if (!QFileInfo::exists(voiceModelPath)) {
        result.errorText = QStringLiteral("Piper voice model was not found at %1").arg(voiceModelPath);
        if (m_loggingService) {
            m_loggingService->warnFor(QStringLiteral("tts"), QStringLiteral("[tts_synthesis_failed] generation=%1 reason=missing_voice_model")
                                                            .arg(QString::number(generation)));
        }
        return result;
    }

    const auto tempRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir().mkpath(tempRoot);
    const QString token = QStringLiteral("%1_%2")
        .arg(generation)
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString rawPath = tempRoot + QStringLiteral("/vaxil_tts_%1_raw.wav").arg(token);
    const QString processedPath = tempRoot + QStringLiteral("/vaxil_tts_%1_processed.wav").arg(token);

    {
        QProcess process;
        QStringList piperArgs{
            QStringLiteral("--model"), voiceModelPath,
            QStringLiteral("--output_file"), rawPath,
            QStringLiteral("--length_scale"), QString::number(1.0 / std::max(0.1, m_settings->voiceSpeed()), 'f', 3)
        };

        const QStringList configCandidates = {
            voiceModelPath + QStringLiteral(".json"),
            QFileInfo(voiceModelPath).absolutePath() + QStringLiteral("/") + QFileInfo(voiceModelPath).completeBaseName() + QStringLiteral(".onnx.json")
        };
        for (const QString &candidate : configCandidates) {
            if (QFileInfo::exists(candidate)) {
                piperArgs << QStringLiteral("--config") << candidate;
                break;
            }
        }

#if defined(Q_OS_LINUX)
        // Allow bundled Piper shared libraries to resolve when executable and libs live together.
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        const QString exeDir = QFileInfo(piperExecutable).absolutePath();
        const QString existingLdPath = env.value(QStringLiteral("LD_LIBRARY_PATH"));
        env.insert(QStringLiteral("LD_LIBRARY_PATH"), existingLdPath.isEmpty()
            ? exeDir
            : (exeDir + QStringLiteral(":") + existingLdPath));
        process.setProcessEnvironment(env);
#endif

        process.start(piperExecutable, piperArgs);
        process.write(sentence.toUtf8());
        process.closeWriteChannel();
        if (!process.waitForFinished(10000)) {
            process.kill();
            process.waitForFinished(1000);
            result.errorText = QStringLiteral("Piper synthesis timed out");
            if (m_loggingService) {
                m_loggingService->warnFor(QStringLiteral("tts"), QStringLiteral("[tts_synthesis_failed] generation=%1 reason=timeout")
                                                                .arg(QString::number(generation)));
            }
            return result;
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
            result.errorText = stderrText.isEmpty()
                ? QStringLiteral("Piper synthesis failed with exit code %1").arg(process.exitCode())
                : QStringLiteral("Piper synthesis failed: %1").arg(stderrText);
            if (m_loggingService) {
                m_loggingService->warnFor(
                    QStringLiteral("tts"),
                    QStringLiteral("[tts_synthesis_failed] generation=%1 reason=process_error detail=%2")
                        .arg(QString::number(generation), clipForTtsLog(result.errorText, 400)));
            }
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
        if (m_loggingService) {
            m_loggingService->warnFor(QStringLiteral("tts"), QStringLiteral("[tts_playback_failed] reason=parse_wave_failed path=%1").arg(path));
        }
        emit playbackFailed(QStringLiteral("Failed to parse synthesized audio"));
        return;
    }

    if (m_loggingService) {
        m_loggingService->infoFor(
            QStringLiteral("tts"),
            QStringLiteral("[tts_wave] sampleRate=%1 channels=%2 bytes=%3 %4")
                .arg(QString::number(format.sampleRate()),
                     QString::number(format.channelCount()),
                     QString::number(pcmData.size()),
                     summarizeWaveSilence(pcmData, format)));
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
    m_playbackFormat = format;
    m_playbackBuffer = new QBuffer(this);
    m_playbackBuffer->setData(m_playbackPcm);
    m_playbackBuffer->open(QIODevice::ReadOnly);
    m_lastFarEndOffset = 0;

    QPointer<QAudioSink> audioSink = m_audioSink;
    m_audioSinkStateConnection = connect(m_audioSink, &QAudioSink::stateChanged, this, [this, audioSink](QAudio::State state) {
        if (!audioSink || audioSink != m_audioSink) {
            return;
        }
        if (m_loggingService) {
            QString stateText = QStringLiteral("unknown");
            if (state == QAudio::IdleState) {
                stateText = QStringLiteral("idle");
            } else if (state == QAudio::ActiveState) {
                stateText = QStringLiteral("active");
            } else if (state == QAudio::SuspendedState) {
                stateText = QStringLiteral("suspended");
            } else if (state == QAudio::StoppedState) {
                stateText = QStringLiteral("stopped");
            }
            m_loggingService->infoFor(QStringLiteral("tts"), QStringLiteral("[tts_state] %1").arg(stateText));
        }
        if (state == QAudio::IdleState) {
            stopPlayback();
            processNext();
        } else if (state == QAudio::StoppedState && m_audioSink != nullptr && m_audioSink->error() != QAudio::NoError) {
            stopPlayback();
            m_processing = false;
            if (m_loggingService) {
                m_loggingService->warnFor(QStringLiteral("tts"), QStringLiteral("[tts_playback_failed] reason=audio_sink_error"));
            }
            emit playbackFailed(QStringLiteral("Audio playback failed"));
        }
    });

    m_audioSink->start(m_playbackBuffer);
    m_farEndTimer->start();
}

void PiperTtsEngine::applySelectedOutputDevice()
{
}

void PiperTtsEngine::stopPlayback()
{
    if (m_farEndTimer) {
        m_farEndTimer->stop();
    }
    m_lastFarEndOffset = 0;
    m_playbackFormat = QAudioFormat();
    QAudioSink *audioSink = m_audioSink;
    QBuffer *playbackBuffer = m_playbackBuffer;
    m_audioSink = nullptr;
    m_playbackBuffer = nullptr;
    if (m_audioSinkStateConnection) {
        disconnect(m_audioSinkStateConnection);
        m_audioSinkStateConnection = {};
    }
    if (audioSink != nullptr) {
        audioSink->stop();
    }
    if (playbackBuffer != nullptr) {
        QObject *bufferOwner = audioSink != nullptr
            ? static_cast<QObject *>(audioSink)
            : static_cast<QObject *>(this);
        playbackBuffer->setParent(bufferOwner);
        QTimer::singleShot(0, playbackBuffer, [playbackBuffer]() {
            playbackBuffer->close();
            playbackBuffer->deleteLater();
        });
    }
    if (audioSink != nullptr) {
        audioSink->deleteLater();
    }
    m_playbackPcm.clear();
}

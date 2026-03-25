#include "wakeword/SherpaWakeWordEngine.h"

#include <algorithm>
#include <array>
#include <vector>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMediaDevices>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>

#include "logging/LoggingService.h"
#include "settings/AppSettings.h"

#if JARVIS_HAS_SHERPA_ONNX
#include <sentencepiece_processor.h>
#include <sherpa-onnx/c-api/cxx-api.h>
#endif

namespace {
constexpr int kSampleRate = 16000;
constexpr int kMinCooldownMs = 700;
constexpr int kMaxCooldownMs = 1600;
constexpr int kChunkSamples = 320;
constexpr int kChunkBytes = kChunkSamples * static_cast<int>(sizeof(qint16));

QString firstExisting(const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        if (!candidate.isEmpty() && QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }

    return {};
}

QString normalizeWakePhrase(const QString &phrase)
{
    QString normalized = phrase.trimmed().toUpper();
    normalized.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    normalized.remove(QRegularExpression(QStringLiteral("[^A-Z0-9 ]")));
    return normalized.trimmed();
}

QStringList wakePhraseVariants(const QString &phrase)
{
    const QString normalized = normalizeWakePhrase(phrase);
    if (normalized.isEmpty()) {
        return {QStringLiteral("JARVIS"), QStringLiteral("HEY JARVIS")};
    }

    QStringList variants;
    variants << normalized;
    if (!normalized.startsWith(QStringLiteral("HEY "))) {
        variants << QStringLiteral("HEY %1").arg(normalized);
    }

    variants.removeDuplicates();
    return variants;
}
}

SherpaWakeWordEngine::SherpaWakeWordEngine(AppSettings *settings, LoggingService *loggingService, QObject *parent)
    : WakeWordEngine(parent)
    , m_settings(settings)
    , m_loggingService(loggingService)
{
    m_format.setSampleRate(kSampleRate);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);
}

SherpaWakeWordEngine::~SherpaWakeWordEngine()
{
    stop();
}

bool SherpaWakeWordEngine::start(
    const QString &enginePath,
    const QString &modelPath,
    float threshold,
    int cooldownMs,
    const QString &preferredDeviceId)
{
    stop();

#if !JARVIS_HAS_SHERPA_ONNX
    Q_UNUSED(enginePath);
    Q_UNUSED(modelPath);
    Q_UNUSED(threshold);
    Q_UNUSED(cooldownMs);
    Q_UNUSED(preferredDeviceId);
    emit errorOccurred(QStringLiteral("sherpa-onnx support is not compiled into this build"));
    return false;
#else
    if (!QFileInfo(enginePath).exists()) {
        emit errorOccurred(QStringLiteral("sherpa-onnx runtime package is missing"));
        return false;
    }
    if (!QFileInfo(modelPath).exists()) {
        emit errorOccurred(QStringLiteral("sherpa-onnx keyword spotting model is missing"));
        return false;
    }

    m_threshold = std::clamp(threshold, 0.10f, 0.85f);
    m_cooldownMs = std::clamp(cooldownMs, kMinCooldownMs, kMaxCooldownMs);
    m_lastActivationMs = 0;
    m_pendingAudio.clear();
    m_preferredDeviceId = preferredDeviceId;
    m_ignoreDetectionsUntilMs = 0;
    m_paused = false;

    if (!prepareKeywordSpotter(enginePath, modelPath, m_threshold)) {
        stop();
        return false;
    }

    if (!startAudioCapture()) {
        stop();
        return false;
    }

    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("sherpa-onnx wake engine started. runtime=\"%1\" model=\"%2\" threshold=%3 cooldownMs=%4 wakeWord=\"%5\"")
            .arg(enginePath, modelPath)
            .arg(m_threshold, 0, 'f', 2)
            .arg(m_cooldownMs)
            .arg(m_settings ? m_settings->wakeWordPhrase() : QStringLiteral("Jarvis")));
    }

    return true;
#endif
}

void SherpaWakeWordEngine::pause()
{
    if (!isActive() || m_paused) {
        return;
    }

    m_paused = true;
    stopAudioCapture();
    resetDetectorState();
    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("sherpa-onnx wake detection paused."));
    }
}

void SherpaWakeWordEngine::resume()
{
    if (!isActive() || !m_paused) {
        return;
    }

    if (!startAudioCapture()) {
        emit errorOccurred(QStringLiteral("Failed to resume microphone capture for sherpa-onnx wake detection"));
        return;
    }

    m_paused = false;
    resetDetectorState();
    m_ignoreDetectionsUntilMs = QDateTime::currentMSecsSinceEpoch() + m_activationWarmupMs;
    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("sherpa-onnx wake detection resumed. warmupMs=%1").arg(m_activationWarmupMs));
    }
}

void SherpaWakeWordEngine::stop()
{
    stopAudioCapture();
    m_pendingAudio.clear();
    m_ignoreDetectionsUntilMs = 0;
    m_paused = false;

#if JARVIS_HAS_SHERPA_ONNX
    m_stream.reset();
    m_keywordSpotter.reset();
    m_runtimeRoot.clear();
    m_modelRoot.clear();
#endif
}

bool SherpaWakeWordEngine::isActive() const
{
#if JARVIS_HAS_SHERPA_ONNX
    return m_keywordSpotter && m_keywordSpotter->Get() != nullptr;
#else
    return false;
#endif
}

bool SherpaWakeWordEngine::isPaused() const
{
    return m_paused;
}

bool SherpaWakeWordEngine::prepareKeywordSpotter(const QString &runtimePath, const QString &modelPath, float threshold)
{
#if !JARVIS_HAS_SHERPA_ONNX
    Q_UNUSED(runtimePath);
    Q_UNUSED(modelPath);
    Q_UNUSED(threshold);
    return false;
#else
    m_runtimeRoot = QFileInfo(runtimePath).absoluteFilePath();
    m_modelRoot = QFileInfo(modelPath).absoluteFilePath();

    const QString encoder = firstExisting({
        QDir(m_modelRoot).filePath(QStringLiteral("encoder-epoch-12-avg-2-chunk-16-left-64.int8.onnx")),
        QDir(m_modelRoot).filePath(QStringLiteral("encoder-epoch-12-avg-2-chunk-16-left-64.onnx"))
    });
    const QString decoder = firstExisting({
        QDir(m_modelRoot).filePath(QStringLiteral("decoder-epoch-12-avg-2-chunk-16-left-64.int8.onnx")),
        QDir(m_modelRoot).filePath(QStringLiteral("decoder-epoch-12-avg-2-chunk-16-left-64.onnx"))
    });
    const QString joiner = firstExisting({
        QDir(m_modelRoot).filePath(QStringLiteral("joiner-epoch-12-avg-2-chunk-16-left-64.int8.onnx")),
        QDir(m_modelRoot).filePath(QStringLiteral("joiner-epoch-12-avg-2-chunk-16-left-64.onnx"))
    });
    const QString tokens = QDir(m_modelRoot).filePath(QStringLiteral("tokens.txt"));

    QString keywordsFile;
    QString errorText;
    if (encoder.isEmpty() || decoder.isEmpty() || joiner.isEmpty() || !QFileInfo::exists(tokens)) {
        emit errorOccurred(QStringLiteral("sherpa-onnx keyword model files are incomplete"));
        return false;
    }

    if (!writeKeywordsFile(m_modelRoot, &keywordsFile, &errorText)) {
        emit errorOccurred(errorText);
        return false;
    }

    sherpa_onnx::cxx::KeywordSpotterConfig config;
    config.feat_config.sample_rate = kSampleRate;
    config.feat_config.feature_dim = 80;
    config.model_config.transducer.encoder = encoder.toStdString();
    config.model_config.transducer.decoder = decoder.toStdString();
    config.model_config.transducer.joiner = joiner.toStdString();
    config.model_config.tokens = tokens.toStdString();
    config.model_config.provider = "cpu";
    config.model_config.num_threads = 1;
    config.model_config.model_type = "zipformer";
    config.keywords_file = keywordsFile.toStdString();
    config.keywords_threshold = threshold;
    config.keywords_score = 1.0f;
    config.max_active_paths = 4;
    config.num_trailing_blanks = 1;

    auto keywordSpotter = std::make_unique<sherpa_onnx::cxx::KeywordSpotter>(
        sherpa_onnx::cxx::KeywordSpotter::Create(config));
    if (!keywordSpotter || keywordSpotter->Get() == nullptr) {
        emit errorOccurred(QStringLiteral("Failed to initialize sherpa-onnx keyword spotter"));
        return false;
    }

    auto stream = std::make_unique<sherpa_onnx::cxx::OnlineStream>(keywordSpotter->CreateStream());
    if (!stream || stream->Get() == nullptr) {
        emit errorOccurred(QStringLiteral("Failed to create sherpa-onnx keyword stream"));
        return false;
    }

    m_keywordSpotter = std::move(keywordSpotter);
    m_stream = std::move(stream);
    return true;
#endif
}

bool SherpaWakeWordEngine::writeKeywordsFile(const QString &modelPath, QString *keywordsPath, QString *errorText) const
{
#if !JARVIS_HAS_SHERPA_ONNX
    Q_UNUSED(modelPath);
    Q_UNUSED(keywordsPath);
    if (errorText) {
        *errorText = QStringLiteral("sentencepiece support is unavailable");
    }
    return false;
#else
    const QString bpeModel = QDir(modelPath).filePath(QStringLiteral("bpe.model"));
    if (!QFileInfo::exists(bpeModel)) {
        if (errorText) {
            *errorText = QStringLiteral("sherpa-onnx BPE model is missing");
        }
        return false;
    }

    sentencepiece::SentencePieceProcessor processor;
    const auto loadStatus = processor.Load(bpeModel.toStdString());
    if (!loadStatus.ok()) {
        if (errorText) {
            *errorText = QStringLiteral("Failed to load sentencepiece wake model: %1")
                .arg(QString::fromStdString(loadStatus.ToString()));
        }
        return false;
    }

    const QString displayPhrase = (m_settings && !m_settings->wakeWordPhrase().trimmed().isEmpty())
        ? m_settings->wakeWordPhrase().trimmed()
        : QStringLiteral("Jarvis");

    QStringList lines;
    for (const QString &variant : wakePhraseVariants(displayPhrase)) {
        std::vector<std::string> pieces;
        const auto encodeStatus = processor.Encode(variant.toStdString(), &pieces);
        if (!encodeStatus.ok() || pieces.empty()) {
            if (errorText) {
                *errorText = QStringLiteral("Failed to encode wake phrase \"%1\": %2")
                    .arg(variant, QString::fromStdString(encodeStatus.ToString()));
            }
            return false;
        }

        QStringList linePieces;
        linePieces.reserve(static_cast<qsizetype>(pieces.size()));
        for (const std::string &piece : pieces) {
            linePieces.push_back(QString::fromStdString(piece));
        }
        lines.push_back(QStringLiteral("%1 @%2").arg(linePieces.join(QChar::fromLatin1(' ')), displayPhrase));
    }

    const QString wakeDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/wake");
    QDir().mkpath(wakeDir);
    const QString outputPath = wakeDir + QStringLiteral("/sherpa_keywords.txt");
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorText) {
            *errorText = QStringLiteral("Failed to write sherpa-onnx keywords file");
        }
        return false;
    }

    file.write(lines.join(QChar::fromLatin1('\n')).toUtf8());
    file.write("\n");
    file.close();

    if (keywordsPath) {
        *keywordsPath = outputPath;
    }

    return true;
#endif
}

bool SherpaWakeWordEngine::startAudioCapture()
{
    QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (!m_preferredDeviceId.isEmpty()) {
        for (const QAudioDevice &candidate : QMediaDevices::audioInputs()) {
            if (QString::fromUtf8(candidate.id()) == m_preferredDeviceId) {
                device = candidate;
                break;
            }
        }
    }

    if (device.isNull()) {
        emit errorOccurred(QStringLiteral("No microphone available for sherpa-onnx wake detection"));
        return false;
    }

    if (!device.isFormatSupported(m_format)) {
        emit errorOccurred(QStringLiteral("Microphone does not support 16 kHz mono PCM for sherpa-onnx wake detection"));
        return false;
    }

    stopAudioCapture();
    m_audioSource = std::make_unique<QAudioSource>(device, m_format, this);
    m_audioSource->setBufferSize(kChunkBytes * 6);
    m_audioIoDevice = m_audioSource->start();
    if (!m_audioIoDevice) {
        emit errorOccurred(QStringLiteral("Failed to start microphone capture for sherpa-onnx wake detection"));
        return false;
    }

    m_audioReadyReadConnection = connect(m_audioIoDevice, &QIODevice::readyRead, this, &SherpaWakeWordEngine::processMicAudio);
    m_ignoreDetectionsUntilMs = QDateTime::currentMSecsSinceEpoch() + m_activationWarmupMs;
    return true;
}

void SherpaWakeWordEngine::stopAudioCapture()
{
    if (m_audioReadyReadConnection) {
        disconnect(m_audioReadyReadConnection);
        m_audioReadyReadConnection = {};
    }

    if (m_audioSource) {
        m_audioSource->stop();
        m_audioSource.reset();
    }
    m_audioIoDevice = nullptr;
}

void SherpaWakeWordEngine::resetDetectorState()
{
#if JARVIS_HAS_SHERPA_ONNX
    m_pendingAudio.clear();
    if (m_keywordSpotter && m_keywordSpotter->Get()) {
        auto stream = std::make_unique<sherpa_onnx::cxx::OnlineStream>(m_keywordSpotter->CreateStream());
        if (stream && stream->Get() != nullptr) {
            m_stream = std::move(stream);
        }
    }
#endif
}

void SherpaWakeWordEngine::processMicAudio()
{
#if !JARVIS_HAS_SHERPA_ONNX
    return;
#else
    if (!m_audioIoDevice || !m_keywordSpotter || !m_stream || m_paused) {
        return;
    }

    static constexpr float kInt16Scale = 1.0f / 32768.0f;
    static std::array<float, kChunkSamples> sampleBuffer {};

    m_pendingAudio.append(m_audioIoDevice->readAll());
    while (m_pendingAudio.size() >= kChunkBytes) {
        const char *raw = m_pendingAudio.constData();
        const auto *samples = reinterpret_cast<const qint16 *>(raw);
        for (int i = 0; i < kChunkSamples; ++i) {
            sampleBuffer[static_cast<size_t>(i)] = static_cast<float>(samples[i]) * kInt16Scale;
        }
        m_pendingAudio.remove(0, kChunkBytes);

        m_stream->AcceptWaveform(kSampleRate, sampleBuffer.data(), kChunkSamples);
        while (m_keywordSpotter->IsReady(m_stream.get())) {
            m_keywordSpotter->Decode(m_stream.get());
        }

        const sherpa_onnx::cxx::KeywordResult result = m_keywordSpotter->GetResult(m_stream.get());
        if (result.keyword.empty()) {
            continue;
        }

        emit probabilityUpdated(1.0f);
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (nowMs < m_ignoreDetectionsUntilMs) {
            m_keywordSpotter->Reset(m_stream.get());
            continue;
        }

        if ((nowMs - m_lastActivationMs) < m_cooldownMs) {
            m_keywordSpotter->Reset(m_stream.get());
            continue;
        }

        m_lastActivationMs = nowMs;
        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("sherpa-onnx wake word detected. keyword=\"%1\" threshold=%2 cooldownMs=%3")
                .arg(QString::fromStdString(result.keyword))
                .arg(m_threshold, 0, 'f', 2)
                .arg(m_cooldownMs));
        }
        m_keywordSpotter->Reset(m_stream.get());
        emit wakeWordDetected();
    }
#endif
}

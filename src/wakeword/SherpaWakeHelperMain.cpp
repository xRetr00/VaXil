#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QIODevice>
#include <QMediaDevices>
#include <QScopedPointer>
#include <QThread>
#include <QTextStream>

#include "diagnostics/CrashDiagnosticsService.h"
#include "diagnostics/StartupMilestones.h"
#include "diagnostics/VaxilErrorCodes.h"

#if JARVIS_HAS_SHERPA_ONNX
#include <sherpa-onnx/c-api/c-api.h>
#endif

namespace {
constexpr int kSampleRate = 16000;
constexpr int kFrameMs = 30;
constexpr int kFrameSamples = 480;
constexpr int kFrameBytes = kFrameSamples * static_cast<int>(sizeof(qint16));

class SherpaWakeHelper final : public QObject
{
public:
    struct Config {
        QString encoderPath;
        QString decoderPath;
        QString joinerPath;
        QString tokensPath;
        QString keywordsFilePath;
        QString deviceId;
        float threshold = 0.18f;
        int cooldownMs = 450;
        int warmupMs = 250;
    };

    explicit SherpaWakeHelper(const Config &config, QObject *parent = nullptr)
        : QObject(parent)
        , m_config(config)
    {
        m_format.setSampleRate(kSampleRate);
        m_format.setChannelCount(1);
        m_format.setSampleFormat(QAudioFormat::Int16);
    }

    int start()
    {
#if !JARVIS_HAS_SHERPA_ONNX
        QTextStream(stderr) << "ERROR: sherpa-onnx support is not compiled into this build" << Qt::endl;
        return 1;
#else
        const QByteArray encoderPathUtf8 = m_config.encoderPath.toUtf8();
        const QByteArray decoderPathUtf8 = m_config.decoderPath.toUtf8();
        const QByteArray joinerPathUtf8 = m_config.joinerPath.toUtf8();
        const QByteArray tokensPathUtf8 = m_config.tokensPath.toUtf8();
        const QByteArray keywordsPathUtf8 = m_config.keywordsFilePath.toUtf8();

        SherpaOnnxKeywordSpotterConfig config{};
        config.feat_config.sample_rate = kSampleRate;
        config.feat_config.feature_dim = 80;
        config.model_config.transducer.encoder = encoderPathUtf8.constData();
        config.model_config.transducer.decoder = decoderPathUtf8.constData();
        config.model_config.transducer.joiner = joinerPathUtf8.constData();
        config.model_config.tokens = tokensPathUtf8.constData();
        config.model_config.provider = "cpu";
        config.model_config.model_type = "zipformer2";
        config.model_config.debug = 0;
        int numThreads = QThread::idealThreadCount();
        if (numThreads < 1) {
            numThreads = 1;
        } else if (numThreads > 4) {
            numThreads = 4;
        }
        config.model_config.num_threads = numThreads;
        config.keywords_file = keywordsPathUtf8.constData();
        config.keywords_threshold = m_config.threshold;
        config.keywords_score = 1.0f;
        config.max_active_paths = 8;
        config.num_trailing_blanks = 1;

        try {
            m_keywordSpotter = SherpaOnnxCreateKeywordSpotter(&config);
            if (!m_keywordSpotter) {
                QTextStream(stderr) << "ERROR: Failed to initialize sherpa keyword spotter" << Qt::endl;
                return 1;
            }

            m_stream = SherpaOnnxCreateKeywordStream(m_keywordSpotter);
            if (!m_stream) {
                QTextStream(stderr) << "ERROR: Failed to create sherpa keyword stream" << Qt::endl;
                SherpaOnnxDestroyKeywordSpotter(m_keywordSpotter);
                m_keywordSpotter = nullptr;
                return 1;
            }
        } catch (...) {
            if (m_stream) {
                SherpaOnnxDestroyOnlineStream(m_stream);
                m_stream = nullptr;
            }
            if (m_keywordSpotter) {
                SherpaOnnxDestroyKeywordSpotter(m_keywordSpotter);
                m_keywordSpotter = nullptr;
            }
            QTextStream(stderr) << "ERROR: Failed to initialize sherpa keyword spotter" << Qt::endl;
            return 1;
        }

        QAudioDevice device = QMediaDevices::defaultAudioInput();
        if (!m_config.deviceId.isEmpty()) {
            for (const QAudioDevice &candidate : QMediaDevices::audioInputs()) {
                if (QString::fromUtf8(candidate.id()) == m_config.deviceId) {
                    device = candidate;
                    break;
                }
            }
        }

        if (device.isNull()) {
            QTextStream(stderr) << "ERROR: No microphone available for wake detection" << Qt::endl;
            return 1;
        }
        if (!device.isFormatSupported(m_format)) {
            QTextStream(stderr) << "ERROR: Selected microphone does not support 16 kHz mono PCM for wake detection" << Qt::endl;
            return 1;
        }

        m_audioSource.reset(new QAudioSource(device, m_format, this));
        m_audioSource->setBufferSize(kFrameBytes * 2);
        m_audioIoDevice = m_audioSource->start();
        if (!m_audioIoDevice) {
            QTextStream(stderr) << "ERROR: Failed to start microphone capture for wake detection" << Qt::endl;
            return 1;
        }

        m_ignoreDetectionsUntilMs = QDateTime::currentMSecsSinceEpoch() + m_config.warmupMs;
        connect(m_audioIoDevice, &QIODevice::readyRead, this, [this]() { processMicBuffer(); }, Qt::DirectConnection);
        QTextStream(stdout) << "MIC:" << device.description() << Qt::endl;
        QTextStream(stdout) << "READY" << Qt::endl;
        return 0;
#endif
    }

private:
    void processMicBuffer()
    {
#if JARVIS_HAS_SHERPA_ONNX
        if (!m_audioIoDevice || !m_keywordSpotter || !m_stream) {
            return;
        }

        m_pendingPcm.append(m_audioIoDevice->readAll());
        while (m_pendingPcm.size() >= kFrameBytes) {
            const auto *samples = reinterpret_cast<const qint16 *>(m_pendingPcm.constData());
            float floatSamples[kFrameSamples];
            for (int i = 0; i < kFrameSamples; ++i) {
                floatSamples[i] = static_cast<float>(samples[i]) / 32768.0f;
            }
            m_pendingPcm.remove(0, kFrameBytes);

            try {
                SherpaOnnxOnlineStreamAcceptWaveform(m_stream, kSampleRate, floatSamples, kFrameSamples);
                while (SherpaOnnxIsKeywordStreamReady(m_keywordSpotter, m_stream) != 0) {
                    SherpaOnnxDecodeKeywordStream(m_keywordSpotter, m_stream);
                }
            } catch (...) {
                QTextStream(stderr) << "ERROR: sherpa wake processing failed" << Qt::endl;
                QCoreApplication::exit(1);
                return;
            }

            const SherpaOnnxKeywordResult *result = SherpaOnnxGetKeywordResult(m_keywordSpotter, m_stream);
            if (!result) {
                QTextStream(stderr) << "ERROR: Failed to read sherpa wake result" << Qt::endl;
                QCoreApplication::exit(1);
                return;
            }
            const QString detectedKeyword = QString::fromUtf8(result->keyword ? result->keyword : "").trimmed();
            SherpaOnnxDestroyKeywordResult(result);

            if (detectedKeyword.isEmpty()) {
                continue;
            }

            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            if (nowMs < m_ignoreDetectionsUntilMs || (nowMs - m_lastActivationMs) < m_config.cooldownMs) {
                SherpaOnnxResetKeywordStream(m_keywordSpotter, m_stream);
                continue;
            }

            m_lastActivationMs = nowMs;
            QTextStream(stdout) << "DETECTED:" << detectedKeyword << Qt::endl;
            SherpaOnnxResetKeywordStream(m_keywordSpotter, m_stream);
        }
#endif
    }

    Config m_config;
    QAudioFormat m_format;
    QScopedPointer<QAudioSource> m_audioSource;
    QIODevice *m_audioIoDevice = nullptr;
    QByteArray m_pendingPcm;
    qint64 m_lastActivationMs = 0;
    qint64 m_ignoreDetectionsUntilMs = 0;

#if JARVIS_HAS_SHERPA_ONNX
    const SherpaOnnxKeywordSpotter *m_keywordSpotter = nullptr;
    const SherpaOnnxOnlineStream *m_stream = nullptr;
#endif
};
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    CrashDiagnosticsConfig diagnosticsConfig;
    diagnosticsConfig.applicationName = QStringLiteral("vaxil_wake_helper");
    diagnosticsConfig.applicationVersion = QStringLiteral("dev");
    diagnosticsConfig.buildInfo = QStringLiteral("wake_helper");
    diagnosticsConfig.logsRootPath = QCoreApplication::applicationDirPath() + QStringLiteral("/logs");
    diagnosticsConfig.breadcrumbCapacity = 120;
    CrashDiagnosticsService::instance().initialize(diagnosticsConfig);
    CrashDiagnosticsService::instance().installHandlers();
    CrashDiagnosticsService::instance().markStartupMilestone(StartupMilestones::startupBegin(),
                                                             QStringLiteral("wake helper bootstrap"),
                                                             true);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Vaxil sherpa wake helper"));
    parser.addHelpOption();

    QCommandLineOption encoder(QStringLiteral("encoder"), QStringLiteral("Path to encoder model"), QStringLiteral("path"));
    QCommandLineOption decoder(QStringLiteral("decoder"), QStringLiteral("Path to decoder model"), QStringLiteral("path"));
    QCommandLineOption joiner(QStringLiteral("joiner"), QStringLiteral("Path to joiner model"), QStringLiteral("path"));
    QCommandLineOption tokens(QStringLiteral("tokens"), QStringLiteral("Path to tokens.txt"), QStringLiteral("path"));
    QCommandLineOption keywords(QStringLiteral("keywords-file"), QStringLiteral("Path to sherpa keywords file"), QStringLiteral("path"));
    QCommandLineOption deviceId(QStringLiteral("device-id"), QStringLiteral("Preferred microphone device id"), QStringLiteral("id"));
    QCommandLineOption threshold(QStringLiteral("threshold"), QStringLiteral("Wake threshold"), QStringLiteral("value"), QStringLiteral("0.18"));
    QCommandLineOption cooldown(QStringLiteral("cooldown-ms"), QStringLiteral("Wake cooldown in milliseconds"), QStringLiteral("value"), QStringLiteral("450"));
    QCommandLineOption warmup(QStringLiteral("warmup-ms"), QStringLiteral("Wake warmup in milliseconds"), QStringLiteral("value"), QStringLiteral("250"));

    parser.addOption(encoder);
    parser.addOption(decoder);
    parser.addOption(joiner);
    parser.addOption(tokens);
    parser.addOption(keywords);
    parser.addOption(deviceId);
    parser.addOption(threshold);
    parser.addOption(cooldown);
    parser.addOption(warmup);
    parser.process(app);

    const QStringList requiredValues = {
        parser.value(encoder),
        parser.value(decoder),
        parser.value(joiner),
        parser.value(tokens),
        parser.value(keywords)
    };
    for (const QString &value : requiredValues) {
        if (value.trimmed().isEmpty()) {
            QTextStream(stderr) << "ERROR: Missing required sherpa wake helper arguments" << Qt::endl;
            return 1;
        }
    }

    SherpaWakeHelper helper({
        .encoderPath = parser.value(encoder),
        .decoderPath = parser.value(decoder),
        .joinerPath = parser.value(joiner),
        .tokensPath = parser.value(tokens),
        .keywordsFilePath = parser.value(keywords),
        .deviceId = parser.value(deviceId),
        .threshold = qBound(0.10f, parser.value(threshold).toFloat(), 0.85f),
        .cooldownMs = qMax(250, parser.value(cooldown).toInt()),
        .warmupMs = qMax(250, parser.value(warmup).toInt())
    });
    const int startCode = helper.start();
    if (startCode != 0) {
        CrashDiagnosticsService::instance().captureHandledException(
            QStringLiteral("wake_helper"),
            VaxilErrorCodes::forKey(VaxilErrorCodes::Key::CrashWakeHelperException),
            QStringLiteral("wake helper failed to start with code %1").arg(startCode));
        return startCode;
    }

    CrashDiagnosticsService::instance().markStartupMilestone(StartupMilestones::startupCompleted(),
                                                             QStringLiteral("wake helper ready"),
                                                             true);

    return app.exec();
}

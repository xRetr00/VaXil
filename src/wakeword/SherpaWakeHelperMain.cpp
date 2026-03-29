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

#if JARVIS_HAS_SHERPA_ONNX
#include <sherpa-onnx/c-api/cxx-api.h>
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
        QString bpeModelPath;
        QString deviceId;
        float sensitivity = 0.8f;
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
        Q_UNUSED(m_config.sensitivity);
        Q_UNUSED(m_config.cooldownMs);
        sherpa_onnx::cxx::OnlineRecognizerConfig config;
        config.feat_config.sample_rate = kSampleRate;
        config.feat_config.feature_dim = 80;
        config.model_config.transducer.encoder = m_config.encoderPath.toStdString();
        config.model_config.transducer.decoder = m_config.decoderPath.toStdString();
        config.model_config.transducer.joiner = m_config.joinerPath.toStdString();
        config.model_config.tokens = m_config.tokensPath.toStdString();
        config.model_config.provider = "cpu";
        config.model_config.model_type = "";
        if (!m_config.bpeModelPath.trimmed().isEmpty()) {
            config.model_config.modeling_unit = "bpe";
            config.model_config.bpe_vocab = m_config.bpeModelPath.toStdString();
        }
        int numThreads = QThread::idealThreadCount();
        if (numThreads < 1) {
            numThreads = 1;
        } else if (numThreads > 4) {
            numThreads = 4;
        }
        config.model_config.num_threads = numThreads;
        config.decoding_method = "greedy_search";
        config.max_active_paths = 8;
        config.enable_endpoint = true;
        config.rule1_min_trailing_silence = 1.0f;
        config.rule2_min_trailing_silence = 0.6f;
        config.rule3_min_utterance_length = 12.0f;

        try {
            m_recognizer.reset(new sherpa_onnx::cxx::OnlineRecognizer(
                sherpa_onnx::cxx::OnlineRecognizer::Create(config)));
            if (!m_recognizer || m_recognizer->Get() == nullptr) {
                QTextStream(stderr) << "ERROR: Failed to initialize sherpa online recognizer" << Qt::endl;
                return 1;
            }

            m_stream.reset(new sherpa_onnx::cxx::OnlineStream(m_recognizer->CreateStream()));
            if (!m_stream || m_stream->Get() == nullptr) {
                QTextStream(stderr) << "ERROR: Failed to create sherpa streaming recognizer" << Qt::endl;
                return 1;
            }
        } catch (...) {
            QTextStream(stderr) << "ERROR: Failed to initialize sherpa online recognizer" << Qt::endl;
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
        QTextStream(stdout) << "READY" << Qt::endl;
        return 0;
#endif
    }

private:
    void processMicBuffer()
    {
#if JARVIS_HAS_SHERPA_ONNX
        if (!m_audioIoDevice || !m_recognizer || !m_stream) {
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
                m_stream->AcceptWaveform(kSampleRate, floatSamples, kFrameSamples);
                while (m_recognizer->IsReady(m_stream.get())) {
                    m_recognizer->Decode(m_stream.get());
                }
            } catch (...) {
                QTextStream(stderr) << "ERROR: sherpa wake processing failed" << Qt::endl;
                QCoreApplication::exit(1);
                return;
            }

            emitTranscript(false);

            if (m_recognizer->IsEndpoint(m_stream.get())) {
                emitTranscript(true);
                m_recognizer->Reset(m_stream.get());
                m_lastTranscript.clear();
            }
        }
#endif
    }

    void emitTranscript(bool isFinal)
    {
#if JARVIS_HAS_SHERPA_ONNX
        if (!m_recognizer || !m_stream) {
            return;
        }

        const sherpa_onnx::cxx::OnlineRecognizerResult result = m_recognizer->GetResult(m_stream.get());
        const QString transcript = QString::fromStdString(result.text).trimmed();
        if (transcript.isEmpty()) {
            if (isFinal) {
                m_lastTranscript.clear();
            }
            return;
        }

        if (!isFinal && transcript == m_lastTranscript) {
            return;
        }

        if (QDateTime::currentMSecsSinceEpoch() < m_ignoreDetectionsUntilMs && !isFinal) {
            m_lastTranscript = transcript;
            return;
        }

        QTextStream(stdout) << (isFinal ? "FINAL:" : "PARTIAL:") << transcript << Qt::endl;

        if (isFinal) {
            m_lastTranscript.clear();
        } else {
            m_lastTranscript = transcript;
        }
#else
        Q_UNUSED(isFinal);
#endif
    }

    Config m_config;
    QAudioFormat m_format;
    QScopedPointer<QAudioSource> m_audioSource;
    QIODevice *m_audioIoDevice = nullptr;
    QByteArray m_pendingPcm;
    qint64 m_ignoreDetectionsUntilMs = 0;
    QString m_lastTranscript;

#if JARVIS_HAS_SHERPA_ONNX
    QScopedPointer<sherpa_onnx::cxx::OnlineRecognizer> m_recognizer;
    QScopedPointer<sherpa_onnx::cxx::OnlineStream> m_stream;
#endif
};
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Vaxil sherpa wake helper"));
    parser.addHelpOption();

    QCommandLineOption encoder(QStringLiteral("encoder"), QStringLiteral("Path to encoder model"), QStringLiteral("path"));
    QCommandLineOption decoder(QStringLiteral("decoder"), QStringLiteral("Path to decoder model"), QStringLiteral("path"));
    QCommandLineOption joiner(QStringLiteral("joiner"), QStringLiteral("Path to joiner model"), QStringLiteral("path"));
    QCommandLineOption tokens(QStringLiteral("tokens"), QStringLiteral("Path to tokens.txt"), QStringLiteral("path"));
    QCommandLineOption bpeModel(QStringLiteral("bpe-model"), QStringLiteral("Path to bpe.model"), QStringLiteral("path"));
    QCommandLineOption deviceId(QStringLiteral("device-id"), QStringLiteral("Preferred microphone device id"), QStringLiteral("id"));
    QCommandLineOption threshold(QStringLiteral("threshold"), QStringLiteral("Wake sensitivity"), QStringLiteral("value"), QStringLiteral("0.80"));
    QCommandLineOption cooldown(QStringLiteral("cooldown-ms"), QStringLiteral("Wake cooldown in milliseconds"), QStringLiteral("value"), QStringLiteral("450"));
    QCommandLineOption warmup(QStringLiteral("warmup-ms"), QStringLiteral("Wake warmup in milliseconds"), QStringLiteral("value"), QStringLiteral("250"));

    parser.addOption(encoder);
    parser.addOption(decoder);
    parser.addOption(joiner);
    parser.addOption(tokens);
    parser.addOption(bpeModel);
    parser.addOption(deviceId);
    parser.addOption(threshold);
    parser.addOption(cooldown);
    parser.addOption(warmup);
    parser.process(app);

    const QStringList requiredValues = {
        parser.value(encoder),
        parser.value(decoder),
        parser.value(joiner),
        parser.value(tokens)
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
        .bpeModelPath = parser.value(bpeModel),
        .deviceId = parser.value(deviceId),
        .sensitivity = qBound(0.50f, parser.value(threshold).toFloat(), 1.0f),
        .cooldownMs = qMax(250, parser.value(cooldown).toInt()),
        .warmupMs = qMax(250, parser.value(warmup).toInt())
    });
    const int startCode = helper.start();
    if (startCode != 0) {
        return startCode;
    }

    return app.exec();
}

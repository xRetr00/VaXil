#include "wakeword/SherpaWakeWordEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSet>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>

#include "logging/LoggingService.h"
#include "platform/PlatformRuntime.h"
#include "settings/AppSettings.h"

#if JARVIS_HAS_SHERPA_ONNX
#include <sentencepiece_processor.h>
#endif

namespace {
constexpr int kMinCooldownMs = 250;
constexpr int kMaxCooldownMs = 1600;
constexpr int kKeywordSegmentationCandidates = 6;

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
    if (!QFileInfo::exists(enginePath)) {
        emit errorOccurred(QStringLiteral("sherpa-onnx runtime package is missing"));
        return false;
    }
    if (!QFileInfo::exists(modelPath)) {
        emit errorOccurred(QStringLiteral("sherpa-onnx keyword spotting model is missing"));
        return false;
    }

    m_threshold = std::clamp(threshold, 0.10f, 0.85f);
    m_cooldownMs = std::clamp(cooldownMs, kMinCooldownMs, kMaxCooldownMs);
    m_preferredDeviceId = preferredDeviceId;
    m_runtimeRoot = QFileInfo(enginePath).absoluteFilePath();
    m_modelRoot = QFileInfo(modelPath).absoluteFilePath();
    m_helperPath = resolveHelperExecutablePath();
    m_encoderPath = resolveModelFile(m_modelRoot, {
        QStringLiteral("encoder-epoch-12-avg-2-chunk-16-left-64.int8.onnx"),
        QStringLiteral("encoder-epoch-12-avg-2-chunk-16-left-64.onnx")
    });
    m_decoderPath = resolveModelFile(m_modelRoot, {
        QStringLiteral("decoder-epoch-12-avg-2-chunk-16-left-64.int8.onnx"),
        QStringLiteral("decoder-epoch-12-avg-2-chunk-16-left-64.onnx")
    });
    m_joinerPath = resolveModelFile(m_modelRoot, {
        QStringLiteral("joiner-epoch-12-avg-2-chunk-16-left-64.int8.onnx"),
        QStringLiteral("joiner-epoch-12-avg-2-chunk-16-left-64.onnx")
    });
    m_tokensPath = QDir(m_modelRoot).filePath(QStringLiteral("tokens.txt"));

    if (m_helperPath.isEmpty()) {
        emit errorOccurred(QStringLiteral("Wake helper executable is missing"));
        return false;
    }
    if (m_encoderPath.isEmpty() || m_decoderPath.isEmpty() || m_joinerPath.isEmpty() || !QFileInfo::exists(m_tokensPath)) {
        emit errorOccurred(QStringLiteral("sherpa-onnx keyword model files are incomplete"));
        return false;
    }

    QString errorText;
    if (!writeKeywordsFile(m_modelRoot, &m_keywordsFilePath, &errorText)) {
        emit errorOccurred(errorText);
        return false;
    }

    m_paused = false;
    return startHelperProcess();
#endif
}

void SherpaWakeWordEngine::pause()
{
    if (!isActive() || m_paused) {
        return;
    }

    m_paused = true;
    m_ready = false;
    m_stopRequested = true;
    if (m_helperProcess) {
        m_helperProcess->terminate();
        if (!m_helperProcess->waitForFinished(500)) {
            m_helperProcess->kill();
        }
    }
    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("sherpa-onnx wake detection paused."));
    }
}

void SherpaWakeWordEngine::resume()
{
    if (!m_paused || m_runtimeRoot.isEmpty() || m_modelRoot.isEmpty()) {
        return;
    }

    m_paused = false;
    startHelperProcess();
}

void SherpaWakeWordEngine::stop()
{
    m_stopRequested = true;
    m_paused = false;
    m_ready = false;

    if (m_helperProcess) {
        m_helperProcess->terminate();
        if (!m_helperProcess->waitForFinished(500)) {
            m_helperProcess->kill();
            m_helperProcess->waitForFinished(500);
        }
        m_helperProcess->deleteLater();
    }

    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_runtimeRoot.clear();
    m_modelRoot.clear();
    m_keywordsFilePath.clear();
    m_encoderPath.clear();
    m_decoderPath.clear();
    m_joinerPath.clear();
    m_tokensPath.clear();
    m_helperPath.clear();
    m_helperProcess = nullptr;
    m_stopRequested = false;
}

bool SherpaWakeWordEngine::isActive() const
{
    if (m_paused && !m_runtimeRoot.isEmpty() && !m_modelRoot.isEmpty()) {
        return true;
    }

    return m_helperProcess != nullptr && m_helperProcess->state() != QProcess::NotRunning;
}

bool SherpaWakeWordEngine::isPaused() const
{
    return m_paused;
}

bool SherpaWakeWordEngine::startHelperProcess()
{
    if (m_helperPath.isEmpty()) {
        emit errorOccurred(QStringLiteral("Wake helper executable is missing"));
        return false;
    }

    if (m_helperProcess) {
        m_stopRequested = true;
        m_helperProcess->kill();
        m_helperProcess->deleteLater();
        m_helperProcess = nullptr;
    }

    m_ready = false;
    m_stopRequested = false;
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();

    m_helperProcess = new QProcess(this);
    connect(m_helperProcess, &QProcess::readyReadStandardOutput, this, &SherpaWakeWordEngine::consumeHelperStdout);
    connect(m_helperProcess, &QProcess::readyReadStandardError, this, &SherpaWakeWordEngine::consumeHelperStderr);
    connect(m_helperProcess, &QProcess::finished, this, &SherpaWakeWordEngine::handleHelperFinished);
    connect(m_helperProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!m_helperProcess) {
            return;
        }
        if (m_stopRequested || m_paused) {
            return;
        }
        if (error == QProcess::Crashed && !m_ready) {
            // Let finished() report the startup failure once with captured stderr.
            return;
        }

        const QString message = m_helperProcess->errorString().trimmed().isEmpty()
            ? QStringLiteral("Wake helper failed to start")
            : m_helperProcess->errorString().trimmed();
        m_ready = false;
        emit errorOccurred(message);
    });

    QStringList args{
        QStringLiteral("--encoder"), m_encoderPath,
        QStringLiteral("--decoder"), m_decoderPath,
        QStringLiteral("--joiner"), m_joinerPath,
        QStringLiteral("--tokens"), m_tokensPath,
        QStringLiteral("--keywords-file"), m_keywordsFilePath,
        QStringLiteral("--threshold"), QString::number(m_threshold, 'f', 2),
        QStringLiteral("--cooldown-ms"), QString::number(m_cooldownMs),
        QStringLiteral("--warmup-ms"), QString::number(m_activationWarmupMs)
    };
    if (!m_preferredDeviceId.trimmed().isEmpty()) {
        args << QStringLiteral("--device-id") << m_preferredDeviceId.trimmed();
    }

    m_helperProcess->setProgram(m_helperPath);
    m_helperProcess->setArguments(args);
    m_helperProcess->setWorkingDirectory(QFileInfo(m_helperPath).absolutePath());

    // Make runtime library resolution explicit for the helper process.
    QStringList runtimeSearchPaths;
    const QFileInfo runtimeInfo(m_runtimeRoot);
    const QString runtimeBaseDir = runtimeInfo.isFile()
        ? runtimeInfo.absolutePath()
        : runtimeInfo.absoluteFilePath();
    if (!runtimeBaseDir.isEmpty()) {
        runtimeSearchPaths << runtimeBaseDir
                           << (runtimeBaseDir + QStringLiteral("/lib"))
                           << (runtimeBaseDir + QStringLiteral("/bin"));
    }
    runtimeSearchPaths << QFileInfo(m_helperPath).absolutePath();

    QStringList existingRuntimeDirs;
    existingRuntimeDirs.reserve(runtimeSearchPaths.size());
    for (const QString &candidate : std::as_const(runtimeSearchPaths)) {
        if (!candidate.isEmpty() && QFileInfo(candidate).exists()) {
            existingRuntimeDirs.push_back(QFileInfo(candidate).absoluteFilePath());
        }
    }
    existingRuntimeDirs.removeDuplicates();

    if (!existingRuntimeDirs.isEmpty()) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
#if defined(Q_OS_WIN)
        const QString existingPath = env.value(QStringLiteral("PATH"));
        env.insert(
            QStringLiteral("PATH"),
            existingRuntimeDirs.join(QStringLiteral(";"))
                + (existingPath.isEmpty() ? QString() : QStringLiteral(";") + existingPath));
#else
        const QString existingLdLibraryPath = env.value(QStringLiteral("LD_LIBRARY_PATH"));
        env.insert(
            QStringLiteral("LD_LIBRARY_PATH"),
            existingRuntimeDirs.join(QStringLiteral(":"))
                + (existingLdLibraryPath.isEmpty() ? QString() : QStringLiteral(":") + existingLdLibraryPath));
#endif
        m_helperProcess->setProcessEnvironment(env);
    }

    m_helperProcess->start();
    if (!m_helperProcess->waitForStarted(3000)) {
        const QString message = m_helperProcess->errorString().trimmed().isEmpty()
            ? QStringLiteral("Failed to start the sherpa wake helper")
            : m_helperProcess->errorString().trimmed();
        emit errorOccurred(message);
        m_helperProcess->deleteLater();
        m_helperProcess = nullptr;
        return false;
    }

    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("Starting sherpa-onnx wake helper. helper=\"%1\" model=\"%2\" threshold=%3 cooldownMs=%4 wakeWord=\"%5\"")
            .arg(m_helperPath, m_modelRoot)
            .arg(m_threshold, 0, 'f', 2)
            .arg(m_cooldownMs)
            .arg(m_settings ? m_settings->wakeWordPhrase() : QStringLiteral("Jarvis")));
    }
    return true;
}

void SherpaWakeWordEngine::consumeHelperStdout()
{
    if (!m_helperProcess) {
        return;
    }

    m_stdoutBuffer.append(m_helperProcess->readAllStandardOutput());
    qsizetype newlineIndex = m_stdoutBuffer.indexOf('\n');
    while (newlineIndex >= 0) {
        QByteArray line = m_stdoutBuffer.left(newlineIndex).trimmed();
        m_stdoutBuffer.remove(0, newlineIndex + 1);

        const QString text = QString::fromUtf8(line);
        if (text == QStringLiteral("READY")) {
            m_ready = true;
            if (m_loggingService) {
                m_loggingService->info(QStringLiteral("sherpa-onnx wake engine started. runtime=\"%1\" model=\"%2\" threshold=%3 cooldownMs=%4 wakeWord=\"%5\"")
                    .arg(m_runtimeRoot, m_modelRoot)
                    .arg(m_threshold, 0, 'f', 2)
                    .arg(m_cooldownMs)
                    .arg(m_settings ? m_settings->wakeWordPhrase() : QStringLiteral("Jarvis")));
            }
            emit engineReady();
        } else if (text.startsWith(QStringLiteral("DETECTED:"), Qt::CaseInsensitive)) {
            if (!m_paused && m_ready) {
                emit probabilityUpdated(1.0f);
                emit wakeWordDetected();
            }
        } else if (text.startsWith(QStringLiteral("ERROR:"), Qt::CaseInsensitive)) {
            const QString message = text.mid(QStringLiteral("ERROR:").size()).trimmed();
            if (!message.isEmpty()) {
                emit errorOccurred(message);
            }
        } else if (!text.isEmpty() && m_loggingService) {
            m_loggingService->info(QStringLiteral("sherpa wake helper: %1").arg(text));
        }

        newlineIndex = m_stdoutBuffer.indexOf('\n');
    }
}

void SherpaWakeWordEngine::consumeHelperStderr()
{
    if (!m_helperProcess) {
        return;
    }

    m_stderrBuffer.append(m_helperProcess->readAllStandardError());
    qsizetype newlineIndex = m_stderrBuffer.indexOf('\n');
    while (newlineIndex >= 0) {
        QByteArray line = m_stderrBuffer.left(newlineIndex).trimmed();
        m_stderrBuffer.remove(0, newlineIndex + 1);
        const QString text = QString::fromUtf8(line);
        if (!text.isEmpty() && m_loggingService) {
            m_loggingService->warn(QStringLiteral("sherpa wake helper stderr: %1").arg(text));
        }
        newlineIndex = m_stderrBuffer.indexOf('\n');
    }
}

void SherpaWakeWordEngine::handleHelperFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    const bool intentionalStop = m_stopRequested || m_paused;
    const bool unexpected = !m_stopRequested && !m_paused;
    m_ready = false;

    if (unexpected) {
        QString message = QStringLiteral("Wake helper exited unexpectedly");
        if (!m_stderrBuffer.trimmed().isEmpty()) {
            message = QString::fromUtf8(m_stderrBuffer).trimmed();
        } else if (m_helperProcess && !m_helperProcess->errorString().trimmed().isEmpty()) {
            message = m_helperProcess->errorString().trimmed();
        } else {
            message += QStringLiteral(" (exitCode=%1, status=%2)")
                .arg(exitCode)
                .arg(exitStatus == QProcess::NormalExit ? QStringLiteral("normal") : QStringLiteral("crash"));
        }
        emit errorOccurred(message);
    }

    if (m_helperProcess) {
        m_helperProcess->deleteLater();
    }
    m_helperProcess = nullptr;
    if (intentionalStop) {
        m_stderrBuffer.clear();
    }
    m_stopRequested = false;
}

QString SherpaWakeWordEngine::resolveHelperExecutablePath() const
{
    const QString helperName = PlatformRuntime::helperExecutableName(QStringLiteral("jarvis_sherpa_wake_helper"));
    return firstExisting({
        QCoreApplication::applicationDirPath() + QStringLiteral("/") + helperName,
        QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/bin/") + helperName,
        QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/build-release/") + helperName,
        QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/build/") + helperName
    });
}

QString SherpaWakeWordEngine::resolveModelFile(const QString &rootPath, const QStringList &fileNames) const
{
    QStringList candidates;
    for (const QString &fileName : fileNames) {
        candidates.push_back(QDir(rootPath).filePath(fileName));
    }
    return firstExisting(candidates);
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
    QSet<QString> emittedLines;
    for (const QString &variant : wakePhraseVariants(displayPhrase)) {
        const std::string variantUtf8 = variant.toStdString();
        std::vector<std::string> pieces;
        const auto encodeStatus = processor.Encode(variantUtf8, &pieces);
        if (!encodeStatus.ok() || pieces.empty()) {
            if (errorText) {
                *errorText = QStringLiteral("Failed to encode wake phrase \"%1\": %2")
                    .arg(variant, QString::fromStdString(encodeStatus.ToString()));
            }
            return false;
        }

        auto addKeywordPieces = [&](const std::vector<std::string> &keywordPieces) {
            if (keywordPieces.empty()) {
                return;
            }

            QStringList linePieces;
            linePieces.reserve(static_cast<qsizetype>(keywordPieces.size()));
            for (const std::string &piece : keywordPieces) {
                linePieces.push_back(QString::fromStdString(piece));
            }

            const QString line = QStringLiteral("%1 @%2").arg(linePieces.join(QChar::fromLatin1(' ')), displayPhrase);
            if (!emittedLines.contains(line)) {
                emittedLines.insert(line);
                lines.push_back(line);
            }
        };

        addKeywordPieces(pieces);

        std::vector<std::vector<std::string>> nbestPieces;
        const auto nbestStatus = processor.NBestEncode(variantUtf8, kKeywordSegmentationCandidates, &nbestPieces);
        if (nbestStatus.ok()) {
            for (const auto &candidate : nbestPieces) {
                addKeywordPieces(candidate);
            }
        }
    }

    if (lines.isEmpty()) {
        if (errorText) {
            *errorText = QStringLiteral("No encoded keyword variants were generated for the wake phrase");
        }
        return false;
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

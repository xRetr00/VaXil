#include "wakeword/WakeWordEnginePrecise.h"

#include <algorithm>

#include <QFileInfo>
#include <QIODevice>
#include <QMediaDevices>

#include "logging/LoggingService.h"

namespace {
constexpr int kSampleRate = 16000;
constexpr int kMinCooldownMs = 600;
constexpr int kMaxCooldownMs = 900;
constexpr int kProbabilityLogEveryNFrames = 5;
}

WakeWordEnginePrecise::WakeWordEnginePrecise(LoggingService *loggingService, QObject *parent)
    : QObject(parent)
    , m_loggingService(loggingService)
{
    connect(&m_engineProcess, &QProcess::readyReadStandardOutput, this, &WakeWordEnginePrecise::consumeEngineOutput);
    connect(&m_engineProcess, &QProcess::readyReadStandardError, this, [this]() {
        const QString stderrText = QString::fromUtf8(m_engineProcess.readAllStandardError()).trimmed();
        if (!stderrText.isEmpty() && m_loggingService) {
            m_loggingService->warn(QStringLiteral("precise-engine stderr: %1").arg(stderrText));
        }
    });
    connect(&m_engineProcess, &QProcess::errorOccurred, this, &WakeWordEnginePrecise::handleProcessError);
}

WakeWordEnginePrecise::~WakeWordEnginePrecise()
{
    stop();
}

bool WakeWordEnginePrecise::start(
    const QString &enginePath,
    const QString &modelPath,
    float threshold,
    int cooldownMs,
    const QString &preferredDeviceId)
{
    stop();

    if (!QFileInfo::exists(enginePath)) {
        emit errorOccurred(QStringLiteral("Mycroft Precise engine is missing"));
        return false;
    }
    if (!QFileInfo::exists(modelPath)) {
        emit errorOccurred(QStringLiteral("Wake word model is not trained yet"));
        return false;
    }
    if (!QFileInfo::exists(modelPath + QStringLiteral(".params"))) {
        emit errorOccurred(QStringLiteral("Wake word model params are missing"));
        return false;
    }

    m_threshold = threshold;
    m_cooldownMs = std::clamp(cooldownMs, kMinCooldownMs, kMaxCooldownMs);
    m_lastActivationMs = 0;
    m_pendingAudio.clear();
    m_stdoutBuffer.clear();
    m_recentProbabilities.clear();
    m_probabilitySum = 0.0f;
    m_consecutiveAboveThreshold = 0;
    m_probabilityLogCounter = 0;
    m_preferredDeviceId = preferredDeviceId;
    m_paused = false;

    m_format.setSampleRate(kSampleRate);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);

    m_engineProcess.start(enginePath, {QFileInfo(modelPath).absoluteFilePath(), QString::number(m_chunkBytes)});
    if (!m_engineProcess.waitForStarted(5000)) {
        emit errorOccurred(QStringLiteral("Failed to start Mycroft Precise engine"));
        return false;
    }

    if (!startAudioCapture()) {
        stop();
        return false;
    }
    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("Mycroft Precise wake engine started. engine=\"%1\" model=\"%2\" threshold=%3 cooldownMs=%4")
            .arg(enginePath, modelPath)
            .arg(m_threshold, 0, 'f', 2)
            .arg(m_cooldownMs));
        m_loggingService->info(QStringLiteral("Wake detection stability config: movingAvgFrames=%1 consistentFrames=%2")
            .arg(m_movingAverageWindowFrames)
            .arg(m_consistentFramesRequired));
    }
    return true;
}

void WakeWordEnginePrecise::pause()
{
    if (!isActive() || m_paused) {
        return;
    }

    m_paused = true;
    stopAudioCapture();
    resetDetectionState();
    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("Mycroft Precise wake detection paused."));
    }
}

void WakeWordEnginePrecise::resume()
{
    if (m_engineProcess.state() != QProcess::Running || !m_paused) {
        return;
    }

    if (!startAudioCapture()) {
        emit errorOccurred(QStringLiteral("Failed to resume microphone capture for wake detection"));
        return;
    }

    m_paused = false;
    resetDetectionState();
    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("Mycroft Precise wake detection resumed."));
    }
}

void WakeWordEnginePrecise::stop()
{
    stopAudioCapture();
    resetDetectionState();
    m_paused = false;
    m_preferredDeviceId.clear();

    if (m_engineProcess.state() != QProcess::NotRunning) {
        m_engineProcess.closeWriteChannel();
        m_engineProcess.terminate();
        if (!m_engineProcess.waitForFinished(1000)) {
            m_engineProcess.kill();
            m_engineProcess.waitForFinished(1000);
        }
    }
}

bool WakeWordEnginePrecise::isActive() const
{
    return m_engineProcess.state() == QProcess::Running;
}

bool WakeWordEnginePrecise::isPaused() const
{
    return m_paused;
}

bool WakeWordEnginePrecise::startAudioCapture()
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
        emit errorOccurred(QStringLiteral("No microphone available for Mycroft Precise"));
        return false;
    }

    if (!device.isFormatSupported(m_format)) {
        emit errorOccurred(QStringLiteral("Microphone does not support 16 kHz mono PCM for wake detection"));
        return false;
    }

    stopAudioCapture();
    m_audioSource = std::make_unique<QAudioSource>(device, m_format, this);
    m_audioSource->setBufferSize(m_chunkBytes * 4);
    m_audioIoDevice = m_audioSource->start();
    if (!m_audioIoDevice) {
        emit errorOccurred(QStringLiteral("Failed to start microphone capture for wake detection"));
        return false;
    }

    m_audioReadyReadConnection = connect(m_audioIoDevice, &QIODevice::readyRead, this, &WakeWordEnginePrecise::flushAudioToEngine);
    return true;
}

void WakeWordEnginePrecise::stopAudioCapture()
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
    m_pendingAudio.clear();
}

void WakeWordEnginePrecise::resetDetectionState()
{
    m_stdoutBuffer.clear();
    m_recentProbabilities.clear();
    m_probabilitySum = 0.0f;
    m_consecutiveAboveThreshold = 0;
    m_probabilityLogCounter = 0;
}

void WakeWordEnginePrecise::flushAudioToEngine()
{
    if (!m_audioIoDevice || m_engineProcess.state() != QProcess::Running || m_paused) {
        return;
    }

    m_pendingAudio.append(m_audioIoDevice->readAll());
    while (m_pendingAudio.size() >= m_chunkBytes) {
        const QByteArray chunk = m_pendingAudio.left(m_chunkBytes);
        m_pendingAudio.remove(0, m_chunkBytes);
        if (m_engineProcess.write(chunk) != chunk.size()) {
            emit errorOccurred(QStringLiteral("Failed to stream audio into Mycroft Precise engine"));
            stop();
            return;
        }
    }
}

void WakeWordEnginePrecise::consumeEngineOutput()
{
    m_stdoutBuffer.append(m_engineProcess.readAllStandardOutput());
    while (true) {
        const int newlineIndex = m_stdoutBuffer.indexOf('\n');
        if (newlineIndex < 0) {
            break;
        }

        const QByteArray rawLine = m_stdoutBuffer.left(newlineIndex).trimmed();
        m_stdoutBuffer.remove(0, newlineIndex + 1);
        if (rawLine.isEmpty()) {
            continue;
        }

        bool ok = false;
        const float probability = rawLine.toFloat(&ok);
        if (!ok) {
            if (m_loggingService) {
                m_loggingService->warn(QStringLiteral("Unexpected precise-engine output: %1").arg(QString::fromUtf8(rawLine)));
            }
            continue;
        }

        emit probabilityUpdated(probability);
        if (m_paused) {
            continue;
        }
        m_recentProbabilities.enqueue(probability);
        m_probabilitySum += probability;
        if (m_recentProbabilities.size() > m_movingAverageWindowFrames) {
            m_probabilitySum -= m_recentProbabilities.dequeue();
        }

        const float movingAverage = m_recentProbabilities.isEmpty()
            ? 0.0f
            : (m_probabilitySum / static_cast<float>(m_recentProbabilities.size()));

        if (probability >= m_threshold) {
            ++m_consecutiveAboveThreshold;
        } else {
            m_consecutiveAboveThreshold = 0;
        }

        ++m_probabilityLogCounter;
        if (m_loggingService && (m_probabilityLogCounter % kProbabilityLogEveryNFrames) == 0) {
            m_loggingService->info(QStringLiteral("Wake probability raw=%1 avg=%2 consecutive=%3/%4 threshold=%5")
                .arg(probability, 0, 'f', 4)
                .arg(movingAverage, 0, 'f', 4)
                .arg(m_consecutiveAboveThreshold)
                .arg(m_consistentFramesRequired)
                .arg(m_threshold, 0, 'f', 2));
        }

        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const bool stableDetection = movingAverage >= m_threshold
            && m_consecutiveAboveThreshold >= m_consistentFramesRequired;
        if (stableDetection && (nowMs - m_lastActivationMs) >= m_cooldownMs) {
            m_lastActivationMs = nowMs;
            m_consecutiveAboveThreshold = 0;
            if (m_loggingService) {
                m_loggingService->info(QStringLiteral("Mycroft Precise wake word detected. raw=%1 avg=%2 threshold=%3 cooldownMs=%4")
                    .arg(probability, 0, 'f', 4)
                    .arg(movingAverage, 0, 'f', 4)
                    .arg(m_threshold, 0, 'f', 2)
                    .arg(m_cooldownMs));
            }
            emit wakeWordDetected();
        }
    }
}

void WakeWordEnginePrecise::handleProcessError(QProcess::ProcessError error)
{
    Q_UNUSED(error);
    const QString message = m_engineProcess.errorString().isEmpty()
        ? QStringLiteral("Mycroft Precise engine failed")
        : QStringLiteral("Mycroft Precise engine failed: %1").arg(m_engineProcess.errorString());
    if (m_loggingService) {
        m_loggingService->error(message);
    }
    emit errorOccurred(message);
    stop();
}

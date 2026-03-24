#include "stt/WhisperSttEngine.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>

#include "logging/LoggingService.h"
#include "settings/AppSettings.h"

namespace {
void writeWaveHeader(QFile &file, quint32 pcmSize)
{
    const quint32 sampleRate = 16000;
    const quint16 channels = 1;
    const quint16 bitsPerSample = 16;
    const quint32 byteRate = sampleRate * channels * bitsPerSample / 8;
    const quint16 blockAlign = channels * bitsPerSample / 8;
    const quint32 chunkSize = 36 + pcmSize;
    const quint32 subChunkSize = 16;
    const quint16 audioFormat = 1;

    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char *>(&chunkSize), 4);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    file.write(reinterpret_cast<const char *>(&subChunkSize), 4);
    file.write(reinterpret_cast<const char *>(&audioFormat), 2);
    file.write(reinterpret_cast<const char *>(&channels), 2);
    file.write(reinterpret_cast<const char *>(&sampleRate), 4);
    file.write(reinterpret_cast<const char *>(&byteRate), 4);
    file.write(reinterpret_cast<const char *>(&blockAlign), 2);
    file.write(reinterpret_cast<const char *>(&bitsPerSample), 2);
    file.write("data", 4);
    file.write(reinterpret_cast<const char *>(&pcmSize), 4);
}
}

WhisperSttEngine::WhisperSttEngine(AppSettings *settings, LoggingService *loggingService, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_loggingService(loggingService)
{
}

void WhisperSttEngine::transcribePcm(const QByteArray &pcmData)
{
    if (m_settings->whisperExecutable().isEmpty()) {
        const QString message = QStringLiteral("whisper.cpp executable is not configured");
        if (m_loggingService) {
            m_loggingService->error(message);
        }
        emit transcriptionFailed(message);
        return;
    }

    if (m_settings->whisperModelPath().isEmpty()) {
        const QString message = QStringLiteral("whisper.cpp model file is not configured");
        if (m_loggingService) {
            m_loggingService->error(message);
        }
        emit transcriptionFailed(message);
        return;
    }

    if (!QFileInfo::exists(m_settings->whisperModelPath())) {
        const QString message = QStringLiteral("whisper.cpp model file is missing");
        if (m_loggingService) {
            m_loggingService->error(QStringLiteral("%1: %2").arg(message, m_settings->whisperModelPath()));
        }
        emit transcriptionFailed(message);
        return;
    }

    const QString waveFile = writeWaveFile(pcmData);
    if (waveFile.isEmpty() || !QFileInfo::exists(waveFile)) {
        const QString message = QStringLiteral("whisper.cpp input WAV could not be created");
        if (m_loggingService) {
            m_loggingService->error(message);
        }
        emit transcriptionFailed(message);
        return;
    }

    if (m_loggingService) {
        m_loggingService->info(
            QStringLiteral("Starting whisper.cpp transcription. executable=\"%1\" model=\"%2\" input=\"%3\" bytes=%4")
                .arg(m_settings->whisperExecutable(), m_settings->whisperModelPath(), waveFile)
                .arg(pcmData.size()));
    }

    auto *process = new QProcess(this);
    connect(process, &QProcess::errorOccurred, this, [this, process, waveFile](QProcess::ProcessError error) {
            const QString message = QStringLiteral("whisper.cpp process error (%1). executable=\"%2\" model=\"%3\" input=\"%4\" stderr=\"%5\"")
                                    .arg(static_cast<int>(error))
                                    .arg(m_settings->whisperExecutable(),
                                         m_settings->whisperModelPath(),
                                         waveFile,
                                         QString::fromUtf8(process->readAllStandardError()).trimmed());
        if (m_loggingService) {
            m_loggingService->error(message);
        }
    });

    connect(process, &QProcess::finished, this, [this, process, waveFile](int exitCode, QProcess::ExitStatus status) {
        const auto cleanup = qScopeGuard([process]() { process->deleteLater(); });
        const QString stdoutText = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
        const QString stderrText = QString::fromUtf8(process->readAllStandardError()).trimmed();

        if (status != QProcess::NormalExit || exitCode != 0) {
            const QString uiMessage = QStringLiteral("whisper.cpp failed to transcribe input");
            if (m_loggingService) {
                m_loggingService->error(
                    QStringLiteral("whisper.cpp failed. exitCode=%1 status=%2 executable=\"%3\" model=\"%4\" input=\"%5\" stdout=\"%6\" stderr=\"%7\"")
                        .arg(exitCode)
                        .arg(status == QProcess::NormalExit ? QStringLiteral("normal") : QStringLiteral("crashed"))
                        .arg(m_settings->whisperExecutable(), m_settings->whisperModelPath(), waveFile, stdoutText, stderrText));
            }
            const QString uiDetailed = stderrText.isEmpty() ? uiMessage : QStringLiteral("%1: %2").arg(uiMessage, stderrText.left(180));
            emit transcriptionFailed(uiDetailed);
            return;
        }

        const QString text = stdoutText;
        if (m_loggingService) {
            m_loggingService->info(
                QStringLiteral("whisper.cpp transcription completed. input=\"%1\" output_chars=%2")
                    .arg(waveFile)
                    .arg(text.size()));
            if (!stderrText.isEmpty()) {
                m_loggingService->warn(QStringLiteral("whisper.cpp stderr: %1").arg(stderrText));
            }
        }
        emit transcriptionReady({text, text.isEmpty() ? 0.0f : 0.85f});
    });

    process->start(
        m_settings->whisperExecutable(),
        {
            QStringLiteral("-m"), m_settings->whisperModelPath(),
            QStringLiteral("-f"), waveFile,
            QStringLiteral("-nt")
        });
}

QString WhisperSttEngine::writeWaveFile(const QByteArray &pcmData) const
{
    const auto tempRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir().mkpath(tempRoot);
    const auto path = tempRoot + QStringLiteral("/jarvis_input_%1.wav").arg(QDateTime::currentMSecsSinceEpoch());

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        writeWaveHeader(file, static_cast<quint32>(pcmData.size()));
        file.write(pcmData);
        file.close();
        return path;
    }

    if (m_loggingService) {
        m_loggingService->error(QStringLiteral("Failed to write whisper.cpp input WAV: %1").arg(path));
    }
    return {};
}

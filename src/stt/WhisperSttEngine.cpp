#include "stt/WhisperSttEngine.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTimer>

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
    : SpeechRecognizer(parent)
    , m_settings(settings)
    , m_loggingService(loggingService)
{
}

WhisperSttEngine::~WhisperSttEngine()
{
    stopActiveProcesses(QStringLiteral("shutdown"));
}

quint64 WhisperSttEngine::transcribePcm(const QByteArray &pcmData, const QString &initialPrompt, bool suppressNonSpeechTokens)
{
    const quint64 requestId = ++m_requestCounter;
    if (m_settings->whisperExecutable().isEmpty()) {
        const QString message = QStringLiteral("whisper.cpp executable is not configured");
        if (m_loggingService) {
            m_loggingService->error(message);
        }
        emit transcriptionFailed(requestId, message);
        return requestId;
    }

    if (m_settings->whisperModelPath().isEmpty()) {
        const QString message = QStringLiteral("whisper.cpp model file is not configured");
        if (m_loggingService) {
            m_loggingService->error(message);
        }
        emit transcriptionFailed(requestId, message);
        return requestId;
    }

    if (!QFileInfo::exists(m_settings->whisperModelPath())) {
        const QString message = QStringLiteral("whisper.cpp model file is missing");
        if (m_loggingService) {
            m_loggingService->error(QStringLiteral("%1: %2").arg(message, m_settings->whisperModelPath()));
        }
        emit transcriptionFailed(requestId, message);
        return requestId;
    }

    const QString waveFile = writeWaveFile(pcmData);
    if (waveFile.isEmpty() || !QFileInfo::exists(waveFile)) {
        const QString message = QStringLiteral("whisper.cpp input WAV could not be created");
        if (m_loggingService) {
            m_loggingService->error(message);
        }
        emit transcriptionFailed(requestId, message);
        return requestId;
    }

    if (m_loggingService) {
        m_loggingService->info(
            QStringLiteral("Starting whisper.cpp transcription. executable=\"%1\" model=\"%2\" input=\"%3\" bytes=%4 prompt=\"%5\" suppressNonSpeech=%6")
                .arg(m_settings->whisperExecutable(), m_settings->whisperModelPath(), waveFile)
                .arg(pcmData.size())
                .arg(initialPrompt.left(120))
                .arg(suppressNonSpeechTokens ? QStringLiteral("true") : QStringLiteral("false")));
    }

    stopActiveProcesses(QStringLiteral("superseded_by_new_request"));

    auto *process = new QProcess(this);
    m_activeProcesses.insert(process);
    connect(process, &QObject::destroyed, this, [this, process]() {
        m_activeProcesses.remove(process);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, waveFile, requestId](QProcess::ProcessError error) {
        const bool superseded = process->property("jarvis_superseded").toBool();
        if (superseded) {
            return;
        }

        const QString stderrText = QString::fromUtf8(process->readAllStandardError()).trimmed();
        const QString message = QStringLiteral("whisper.cpp process error (%1). executable=\"%2\" model=\"%3\" input=\"%4\" stderr=\"%5\"")
                                    .arg(static_cast<int>(error))
                                    .arg(m_settings->whisperExecutable(),
                                         m_settings->whisperModelPath(),
                                         waveFile,
                                         stderrText);
        if (m_loggingService) {
            m_loggingService->error(message);
        }

        if (error == QProcess::FailedToStart && !process->property("jarvis_failure_notified").toBool()) {
            process->setProperty("jarvis_failure_notified", true);
            const QString uiDetailed = stderrText.isEmpty()
                ? QStringLiteral("whisper.cpp failed to start")
                : QStringLiteral("whisper.cpp failed to start: %1").arg(stderrText.left(180));
            emit transcriptionFailed(requestId, uiDetailed);
            QFile::remove(waveFile);
        }
    });

    connect(process, &QProcess::finished, this, [this, process, waveFile, requestId](int exitCode, QProcess::ExitStatus status) {
        const auto cleanup = qScopeGuard([this, process]() {
            m_activeProcesses.remove(process);
            process->deleteLater();
        });
        const QString stdoutText = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
        const QString stderrText = QString::fromUtf8(process->readAllStandardError()).trimmed();
        const bool superseded = process->property("jarvis_superseded").toBool();
        const bool failureAlreadyNotified = process->property("jarvis_failure_notified").toBool();
        const auto removeWaveFile = qScopeGuard([&waveFile]() {
            QFile::remove(waveFile);
        });

        if (superseded) {
            if (m_loggingService) {
                m_loggingService->info(QStringLiteral("whisper.cpp transcription superseded before completion. input=\"%1\"")
                                           .arg(waveFile));
            }
            return;
        }

        if (failureAlreadyNotified) {
            return;
        }

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
            emit transcriptionFailed(requestId, uiDetailed);
            return;
        }

        const QString text = stdoutText;
        if (m_loggingService) {
            m_loggingService->info(
                QStringLiteral("whisper.cpp transcription completed. input=\"%1\" output_chars=%2")
                    .arg(waveFile)
                    .arg(text.size()));
            if (!stderrText.isEmpty()) {
                const QString lowered = stderrText.toLower();
                const bool looksLikeFailure = lowered.contains(QStringLiteral("error"))
                    || lowered.contains(QStringLiteral("failed"))
                    || lowered.contains(QStringLiteral("exception"));
                if (looksLikeFailure) {
                    m_loggingService->warn(QStringLiteral("whisper.cpp stderr: %1").arg(stderrText));
                } else {
                    m_loggingService->info(QStringLiteral("whisper.cpp stderr: %1").arg(stderrText));
                }
            }
        }
        emit transcriptionReady(requestId, {text, text.isEmpty() ? 0.0f : 0.85f});
    });

    QStringList arguments{
        QStringLiteral("-m"), m_settings->whisperModelPath(),
        QStringLiteral("-f"), waveFile,
        QStringLiteral("-nt"),
        QStringLiteral("-l"), QStringLiteral("en"),
        QStringLiteral("-ng")
    };

    if (suppressNonSpeechTokens) {
        arguments << QStringLiteral("-sns");
    }

    if (!initialPrompt.trimmed().isEmpty()) {
        arguments << QStringLiteral("--prompt") << initialPrompt.trimmed();
    }

    process->start(m_settings->whisperExecutable(), arguments);
    return requestId;
}

void WhisperSttEngine::stopActiveProcesses(const QString &reason)
{
    const auto activeProcesses = m_activeProcesses;
    for (QProcess *process : activeProcesses) {
        if (!process || process->state() == QProcess::NotRunning) {
            continue;
        }

        process->setProperty("jarvis_superseded", true);
        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("Stopping prior whisper.cpp transcription. reason=\"%1\"").arg(reason));
        }
        process->terminate();
        QTimer::singleShot(400, process, [process]() {
            if (process->state() != QProcess::NotRunning) {
                process->kill();
            }
        });
    }
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

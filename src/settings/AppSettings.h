#pragma once

#include <QObject>
#include <QString>

#include "core/AssistantTypes.h"

class AppSettings : public QObject
{
    Q_OBJECT

public:
    explicit AppSettings(QObject *parent = nullptr);

    bool load();
    bool save() const;

    QString lmStudioEndpoint() const;
    void setLmStudioEndpoint(const QString &endpoint);

    QString selectedModel() const;
    void setSelectedModel(const QString &modelId);

    ReasoningMode defaultReasoningMode() const;
    void setDefaultReasoningMode(ReasoningMode mode);

    bool autoRoutingEnabled() const;
    void setAutoRoutingEnabled(bool enabled);

    bool streamingEnabled() const;
    void setStreamingEnabled(bool enabled);

    int requestTimeoutMs() const;
    void setRequestTimeoutMs(int timeoutMs);

    QString whisperExecutable() const;
    void setWhisperExecutable(const QString &path);

    QString whisperModelPath() const;
    void setWhisperModelPath(const QString &path);

    QString piperExecutable() const;
    void setPiperExecutable(const QString &path);

    QString piperVoiceModel() const;
    void setPiperVoiceModel(const QString &path);

    QString selectedVoicePresetId() const;
    void setSelectedVoicePresetId(const QString &voicePresetId);

    QString preciseEngineExecutable() const;
    void setPreciseEngineExecutable(const QString &path);

    QString preciseModelPath() const;
    void setPreciseModelPath(const QString &path);

    double preciseTriggerThreshold() const;
    void setPreciseTriggerThreshold(double threshold);

    int preciseTriggerCooldownMs() const;
    void setPreciseTriggerCooldownMs(int cooldownMs);

    QString ffmpegExecutable() const;
    void setFfmpegExecutable(const QString &path);

    double voiceSpeed() const;
    void setVoiceSpeed(double speed);

    double voicePitch() const;
    void setVoicePitch(double pitch);

    double micSensitivity() const;
    void setMicSensitivity(double sensitivity);

    QString selectedAudioInputDeviceId() const;
    void setSelectedAudioInputDeviceId(const QString &deviceId);

    QString selectedAudioOutputDeviceId() const;
    void setSelectedAudioOutputDeviceId(const QString &deviceId);

    bool clickThroughEnabled() const;
    void setClickThroughEnabled(bool enabled);

    bool initialSetupCompleted() const;
    void setInitialSetupCompleted(bool completed);

    QString wakeWordPhrase() const;
    void setWakeWordPhrase(const QString &wakeWordPhrase);

    QString storagePath() const;

signals:
    void settingsChanged();

private:
    QString m_lmStudioEndpoint;
    QString m_selectedModel;
    ReasoningMode m_defaultReasoningMode = ReasoningMode::Balanced;
    bool m_autoRoutingEnabled = true;
    bool m_streamingEnabled = true;
    int m_requestTimeoutMs = 12000;
    QString m_whisperExecutable;
    QString m_whisperModelPath;
    QString m_piperExecutable;
    QString m_piperVoiceModel;
    QString m_selectedVoicePresetId = QStringLiteral("en_GB-alba-medium");
    QString m_preciseEngineExecutable;
    QString m_preciseModelPath;
    double m_preciseTriggerThreshold = 0.30;
    int m_preciseTriggerCooldownMs = 750;
    QString m_ffmpegExecutable;
    double m_voiceSpeed = 0.89;
    double m_voicePitch = 0.93;
    double m_micSensitivity = 0.02;
    QString m_selectedAudioInputDeviceId;
    QString m_selectedAudioOutputDeviceId;
    bool m_clickThroughEnabled = true;
    bool m_initialSetupCompleted = false;
    QString m_wakeWordPhrase = QStringLiteral("Jarvis");
};

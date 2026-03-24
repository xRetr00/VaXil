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

    QString piperExecutable() const;
    void setPiperExecutable(const QString &path);

    QString piperVoiceModel() const;
    void setPiperVoiceModel(const QString &path);

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
    QString m_piperExecutable;
    QString m_piperVoiceModel;
    QString m_ffmpegExecutable;
    double m_voiceSpeed = 0.88;
    double m_voicePitch = 0.94;
    double m_micSensitivity = 0.02;
    QString m_selectedAudioInputDeviceId;
    QString m_selectedAudioOutputDeviceId;
    bool m_clickThroughEnabled = false;
    bool m_initialSetupCompleted = false;
};

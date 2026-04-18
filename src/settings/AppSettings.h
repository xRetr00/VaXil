#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include "core/AssistantTypes.h"

class AppSettings : public QObject
{
    Q_OBJECT

public:
    explicit AppSettings(QObject *parent = nullptr);

    bool load();
    bool save() const;

    QString chatBackendKind() const;
    void setChatBackendKind(const QString &kind);

    QString chatBackendEndpoint() const;
    void setChatBackendEndpoint(const QString &endpoint);

    QString chatBackendApiKey() const;
    void setChatBackendApiKey(const QString &apiKey);

    QString chatBackendModel() const;
    void setChatBackendModel(const QString &modelId);

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

    bool agentEnabled() const;
    void setAgentEnabled(bool enabled);

    QString agentProviderMode() const;
    void setAgentProviderMode(const QString &mode);

    double conversationTemperature() const;
    void setConversationTemperature(double temperature);

    std::optional<double> conversationTopP() const;
    void setConversationTopP(const std::optional<double> &topP);

    double toolUseTemperature() const;
    void setToolUseTemperature(double temperature);

    std::optional<int> providerTopK() const;
    void setProviderTopK(const std::optional<int> &topK);

    int maxOutputTokens() const;
    void setMaxOutputTokens(int maxTokens);

    bool memoryAutoWrite() const;
    void setMemoryAutoWrite(bool enabled);

    bool learningDataCollectionEnabled() const;
    void setLearningDataCollectionEnabled(bool enabled);

    bool learningAudioCollectionEnabled() const;
    void setLearningAudioCollectionEnabled(bool enabled);

    bool learningTranscriptCollectionEnabled() const;
    void setLearningTranscriptCollectionEnabled(bool enabled);

    bool learningToolLoggingEnabled() const;
    void setLearningToolLoggingEnabled(bool enabled);

    bool learningBehaviorLoggingEnabled() const;
    void setLearningBehaviorLoggingEnabled(bool enabled);

    bool learningMemoryLoggingEnabled() const;
    void setLearningMemoryLoggingEnabled(bool enabled);

    double learningMaxAudioStorageGb() const;
    void setLearningMaxAudioStorageGb(double valueGb);

    int learningMaxDaysToKeepAudio() const;
    void setLearningMaxDaysToKeepAudio(int days);

    int learningMaxDaysToKeepStructuredLogs() const;
    void setLearningMaxDaysToKeepStructuredLogs(int days);

    bool learningAllowPreparedDatasetExport() const;
    void setLearningAllowPreparedDatasetExport(bool enabled);

    QString webSearchProvider() const;
    void setWebSearchProvider(const QString &provider);

    QString braveSearchApiKey() const;
    void setBraveSearchApiKey(const QString &apiKey);

    bool mcpEnabled() const;
    void setMcpEnabled(bool enabled);

    QString mcpCatalogUrl() const;
    void setMcpCatalogUrl(const QString &url);

    QString mcpServerUrl() const;
    void setMcpServerUrl(const QString &url);

    bool visionEnabled() const;
    void setVisionEnabled(bool enabled);

    QString visionEndpoint() const;
    void setVisionEndpoint(const QString &endpoint);

    int visionTimeoutMs() const;
    void setVisionTimeoutMs(int timeoutMs);

    int visionStaleThresholdMs() const;
    void setVisionStaleThresholdMs(int thresholdMs);

    bool visionContextAlwaysOn() const;
    void setVisionContextAlwaysOn(bool enabled);

    double visionObjectsMinConfidence() const;
    void setVisionObjectsMinConfidence(double confidence);

    double visionGesturesMinConfidence() const;
    void setVisionGesturesMinConfidence(double confidence);

    bool gestureEnabled() const;
    void setGestureEnabled(bool enabled);

    int gestureStabilityMs() const;
    void setGestureStabilityMs(int stabilityMs);

    int gestureCooldownMs() const;
    void setGestureCooldownMs(int cooldownMs);

    bool tracePanelEnabled() const;
    void setTracePanelEnabled(bool enabled);

    bool focusModeEnabled() const;
    void setFocusModeEnabled(bool enabled);

    bool focusModeAllowCriticalAlerts() const;
    void setFocusModeAllowCriticalAlerts(bool enabled);

    int focusModeDurationMinutes() const;
    void setFocusModeDurationMinutes(int minutes);

    qint64 focusModeUntilEpochMs() const;
    void setFocusModeUntilEpochMs(qint64 epochMs);

    bool privateModeEnabled() const;
    void setPrivateModeEnabled(bool enabled);

    QVariantList permissionOverrides() const;
    void setPermissionOverrides(const QVariantList &overrides);

    QString whisperExecutable() const;
    void setWhisperExecutable(const QString &path);

    QString whisperModelPath() const;
    void setWhisperModelPath(const QString &path);

    QString intentModelPath() const;
    void setIntentModelPath(const QString &path);

    QString selectedIntentModelId() const;
    void setSelectedIntentModelId(const QString &modelId);

    QString piperExecutable() const;
    void setPiperExecutable(const QString &path);

    QString piperVoiceModel() const;
    void setPiperVoiceModel(const QString &path);

    QString selectedVoicePresetId() const;
    void setSelectedVoicePresetId(const QString &voicePresetId);

    bool aecEnabled() const;
    void setAecEnabled(bool enabled);

    bool rnnoiseEnabled() const;
    void setRnnoiseEnabled(bool enabled);

    double vadSensitivity() const;
    void setVadSensitivity(double sensitivity);

    double wakeTriggerThreshold() const;
    void setWakeTriggerThreshold(double threshold);

    int wakeTriggerCooldownMs() const;
    void setWakeTriggerCooldownMs(int cooldownMs);

    QString ffmpegExecutable() const;
    void setFfmpegExecutable(const QString &path);

    QString ttsEngineKind() const;
    void setTtsEngineKind(const QString &kind);

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

    QString uiMode() const;
    void setUiMode(const QString &mode);

    bool initialSetupCompleted() const;
    void setInitialSetupCompleted(bool completed);

    QString wakeWordPhrase() const;
    void setWakeWordPhrase(const QString &wakeWordPhrase);
    bool wakeWordEnabled() const;
    void setWakeWordEnabled(bool enabled);
    double wakeWordSensitivity() const;
    void setWakeWordSensitivity(double sensitivity);

    QString wakeEngineKind() const;
    void setWakeEngineKind(const QString &kind);

    QString storagePath() const;

signals:
    void settingsChanged();

private:
    QString m_chatBackendKind = QStringLiteral("openai_compatible_local");
    QString m_chatBackendEndpoint;
    QString m_chatBackendApiKey;
    QString m_chatBackendModel;
    QString m_lmStudioEndpoint;
    QString m_selectedModel;
    ReasoningMode m_defaultReasoningMode = ReasoningMode::Balanced;
    bool m_autoRoutingEnabled = true;
    bool m_streamingEnabled = true;
    int m_requestTimeoutMs = 12000;
    bool m_agentEnabled = true;
    QString m_agentProviderMode = QStringLiteral("auto");
    double m_conversationTemperature = 0.7;
    std::optional<double> m_conversationTopP = 0.9;
    double m_toolUseTemperature = 0.2;
    std::optional<int> m_providerTopK;
    int m_maxOutputTokens = 1024;
    bool m_memoryAutoWrite = true;
    bool m_learningDataCollectionEnabled = false;
    bool m_learningAudioCollectionEnabled = false;
    bool m_learningTranscriptCollectionEnabled = false;
    bool m_learningToolLoggingEnabled = false;
    bool m_learningBehaviorLoggingEnabled = false;
    bool m_learningMemoryLoggingEnabled = false;
    double m_learningMaxAudioStorageGb = 4.0;
    int m_learningMaxDaysToKeepAudio = 30;
    int m_learningMaxDaysToKeepStructuredLogs = 90;
    bool m_learningAllowPreparedDatasetExport = false;
    QString m_webSearchProvider = QStringLiteral("brave");
    QString m_braveSearchApiKey;
    bool m_mcpEnabled = false;
    QString m_mcpCatalogUrl;
    QString m_mcpServerUrl;
    bool m_visionEnabled = false;
    QString m_visionEndpoint = QStringLiteral("ws://0.0.0.0:8765/vision");
    int m_visionTimeoutMs = 5000;
    int m_visionStaleThresholdMs = 2000;
    bool m_visionContextAlwaysOn = false;
    double m_visionObjectsMinConfidence = 0.60;
    double m_visionGesturesMinConfidence = 0.70;
    bool m_gestureEnabled = false;
    int m_gestureStabilityMs = 180;
    int m_gestureCooldownMs = 500;
    bool m_tracePanelEnabled = true;
    bool m_focusModeEnabled = false;
    bool m_focusModeAllowCriticalAlerts = true;
    int m_focusModeDurationMinutes = 0;
    qint64 m_focusModeUntilEpochMs = 0;
    bool m_privateModeEnabled = false;
    QVariantList m_permissionOverrides;
    QString m_whisperExecutable;
    QString m_whisperModelPath;
    QString m_intentModelPath;
    QString m_selectedIntentModelId = QStringLiteral("intent-minilm-int8");
    QString m_piperExecutable;
    QString m_piperVoiceModel;
    QString m_selectedVoicePresetId = QStringLiteral("en_GB-alba-medium");
    bool m_aecEnabled = true;
    bool m_rnnoiseEnabled = false;
    double m_vadSensitivity = 0.55;
    double m_wakeTriggerThreshold = 0.80;
    int m_wakeTriggerCooldownMs = 450;
    QString m_ffmpegExecutable;
    QString m_ttsEngineKind = QStringLiteral("piper");
    double m_voiceSpeed = 0.89;
    double m_voicePitch = 0.93;
    double m_micSensitivity = 0.02;
    QString m_selectedAudioInputDeviceId;
    QString m_selectedAudioOutputDeviceId;
    bool m_clickThroughEnabled = true;
    QString m_uiMode = QStringLiteral("full");
    bool m_initialSetupCompleted = false;
    QString m_wakeWordPhrase = QStringLiteral("Hey Vaxil");
    bool m_wakeWordEnabled = true;
    double m_wakeWordSensitivity = 0.80;
    QString m_wakeEngineKind = QStringLiteral("sherpa-onnx");
};

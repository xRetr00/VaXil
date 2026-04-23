#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class BackendFacade;

class SettingsViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString assistantName READ assistantName NOTIFY profileChanged)
    Q_PROPERTY(QString userName READ userName NOTIFY profileChanged)
    Q_PROPERTY(QStringList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(QString selectedModel READ selectedModel NOTIFY selectedModelChanged)
    Q_PROPERTY(QString lmStudioEndpoint READ lmStudioEndpoint NOTIFY settingsChanged)
    Q_PROPERTY(QString chatProviderKind READ chatProviderKind NOTIFY settingsChanged)
    Q_PROPERTY(QString chatProviderApiKey READ chatProviderApiKey NOTIFY settingsChanged)
    Q_PROPERTY(int defaultReasoningMode READ defaultReasoningMode NOTIFY settingsChanged)
    Q_PROPERTY(bool autoRoutingEnabled READ autoRoutingEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool streamingEnabled READ streamingEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int requestTimeoutMs READ requestTimeoutMs NOTIFY settingsChanged)
    Q_PROPERTY(bool visionEnabled READ visionEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QString visionEndpoint READ visionEndpoint NOTIFY settingsChanged)
    Q_PROPERTY(int visionTimeoutMs READ visionTimeoutMs NOTIFY settingsChanged)
    Q_PROPERTY(int visionStaleThresholdMs READ visionStaleThresholdMs NOTIFY settingsChanged)
    Q_PROPERTY(bool visionContextAlwaysOn READ visionContextAlwaysOn NOTIFY settingsChanged)
    Q_PROPERTY(double visionObjectsMinConfidence READ visionObjectsMinConfidence NOTIFY settingsChanged)
    Q_PROPERTY(double visionGesturesMinConfidence READ visionGesturesMinConfidence NOTIFY settingsChanged)
    Q_PROPERTY(bool clickThroughEnabled READ clickThroughEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QStringList voicePresetNames READ voicePresetNames NOTIFY settingsChanged)
    Q_PROPERTY(QStringList voicePresetIds READ voicePresetIds NOTIFY settingsChanged)
    Q_PROPERTY(QString selectedVoicePresetId READ selectedVoicePresetId NOTIFY settingsChanged)
    Q_PROPERTY(QStringList whisperModelPresetNames READ whisperModelPresetNames NOTIFY settingsChanged)
    Q_PROPERTY(QStringList whisperModelPresetIds READ whisperModelPresetIds NOTIFY settingsChanged)
    Q_PROPERTY(QString selectedWhisperModelPresetId READ selectedWhisperModelPresetId NOTIFY settingsChanged)
    Q_PROPERTY(QStringList intentModelPresetNames READ intentModelPresetNames NOTIFY settingsChanged)
    Q_PROPERTY(QStringList intentModelPresetIds READ intentModelPresetIds NOTIFY settingsChanged)
    Q_PROPERTY(QString selectedIntentModelId READ selectedIntentModelId NOTIFY settingsChanged)
    Q_PROPERTY(QString intentModelPath READ intentModelPath NOTIFY settingsChanged)
    Q_PROPERTY(QString recommendedIntentModelLabel READ recommendedIntentModelLabel NOTIFY settingsChanged)
    Q_PROPERTY(QString intentHardwareSummary READ intentHardwareSummary NOTIFY settingsChanged)
    Q_PROPERTY(QString wakeEngineKind READ wakeEngineKind NOTIFY settingsChanged)
    Q_PROPERTY(QString whisperExecutable READ whisperExecutable NOTIFY settingsChanged)
    Q_PROPERTY(QString whisperModelPath READ whisperModelPath NOTIFY settingsChanged)
    Q_PROPERTY(QString ttsEngineKind READ ttsEngineKind NOTIFY settingsChanged)
    Q_PROPERTY(QString piperExecutable READ piperExecutable NOTIFY settingsChanged)
    Q_PROPERTY(QString piperVoiceModel READ piperVoiceModel NOTIFY settingsChanged)
    Q_PROPERTY(QString qwenTtsExecutable READ qwenTtsExecutable NOTIFY settingsChanged)
    Q_PROPERTY(QString qwenTtsModelDir READ qwenTtsModelDir NOTIFY settingsChanged)
    Q_PROPERTY(QString qwenTtsLanguage READ qwenTtsLanguage NOTIFY settingsChanged)
    Q_PROPERTY(int qwenTtsThreads READ qwenTtsThreads NOTIFY settingsChanged)
    Q_PROPERTY(double wakeTriggerThreshold READ wakeTriggerThreshold NOTIFY settingsChanged)
    Q_PROPERTY(int wakeTriggerCooldownMs READ wakeTriggerCooldownMs NOTIFY settingsChanged)
    Q_PROPERTY(QString ffmpegExecutable READ ffmpegExecutable NOTIFY settingsChanged)
    Q_PROPERTY(double voiceSpeed READ voiceSpeed NOTIFY settingsChanged)
    Q_PROPERTY(double voicePitch READ voicePitch NOTIFY settingsChanged)
    Q_PROPERTY(double piperNoiseScale READ piperNoiseScale NOTIFY settingsChanged)
    Q_PROPERTY(double piperNoiseW READ piperNoiseW NOTIFY settingsChanged)
    Q_PROPERTY(double piperSentenceSilence READ piperSentenceSilence NOTIFY settingsChanged)
    Q_PROPERTY(QString ttsPostProcessMode READ ttsPostProcessMode NOTIFY settingsChanged)
    Q_PROPERTY(QStringList ttsPostProcessModes READ ttsPostProcessModes NOTIFY settingsChanged)
    Q_PROPERTY(QStringList ttsVoiceProfileNames READ ttsVoiceProfileNames NOTIFY settingsChanged)
    Q_PROPERTY(QStringList ttsVoiceProfileIds READ ttsVoiceProfileIds NOTIFY settingsChanged)
    Q_PROPERTY(QString ttsVoiceProfileId READ ttsVoiceProfileId NOTIFY settingsChanged)
    Q_PROPERTY(double micSensitivity READ micSensitivity NOTIFY settingsChanged)
    Q_PROPERTY(bool aecEnabled READ aecEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool rnnoiseEnabled READ rnnoiseEnabled NOTIFY settingsChanged)
    Q_PROPERTY(double vadSensitivity READ vadSensitivity NOTIFY settingsChanged)
    Q_PROPERTY(QStringList audioInputDeviceNames READ audioInputDeviceNames NOTIFY audioDevicesChanged)
    Q_PROPERTY(QStringList audioInputDeviceIds READ audioInputDeviceIds NOTIFY audioDevicesChanged)
    Q_PROPERTY(QStringList audioOutputDeviceNames READ audioOutputDeviceNames NOTIFY audioDevicesChanged)
    Q_PROPERTY(QStringList audioOutputDeviceIds READ audioOutputDeviceIds NOTIFY audioDevicesChanged)
    Q_PROPERTY(QString selectedAudioInputDeviceId READ selectedAudioInputDeviceId NOTIFY settingsChanged)
    Q_PROPERTY(QString selectedAudioOutputDeviceId READ selectedAudioOutputDeviceId NOTIFY settingsChanged)
    Q_PROPERTY(QString uiMode READ uiMode NOTIFY settingsChanged)
    Q_PROPERTY(QString toolInstallStatus READ toolInstallStatus NOTIFY toolInstallStatusChanged)
    Q_PROPERTY(QVariantList toolStatuses READ toolStatuses NOTIFY toolStatusesChanged)
    Q_PROPERTY(int toolDownloadPercent READ toolDownloadPercent NOTIFY toolInstallStatusChanged)
    Q_PROPERTY(QString toolsRoot READ toolsRoot NOTIFY toolStatusesChanged)
    Q_PROPERTY(QString wakeWordPhrase READ wakeWordPhrase NOTIFY settingsChanged)
    Q_PROPERTY(bool agentEnabled READ agentEnabled NOTIFY agentStateChanged)
    Q_PROPERTY(QString agentProviderMode READ agentProviderMode NOTIFY agentStateChanged)
    Q_PROPERTY(double conversationTemperature READ conversationTemperature NOTIFY agentStateChanged)
    Q_PROPERTY(double conversationTopP READ conversationTopP NOTIFY agentStateChanged)
    Q_PROPERTY(double toolUseTemperature READ toolUseTemperature NOTIFY agentStateChanged)
    Q_PROPERTY(int providerTopK READ providerTopK NOTIFY agentStateChanged)
    Q_PROPERTY(int maxOutputTokens READ maxOutputTokens NOTIFY agentStateChanged)
    Q_PROPERTY(bool budgetEnforcementDisabled READ budgetEnforcementDisabled NOTIFY agentStateChanged)
    Q_PROPERTY(bool memoryAutoWrite READ memoryAutoWrite NOTIFY agentStateChanged)
    Q_PROPERTY(QString webSearchProvider READ webSearchProvider NOTIFY agentStateChanged)
    Q_PROPERTY(QString braveSearchApiKey READ braveSearchApiKey NOTIFY agentStateChanged)
    Q_PROPERTY(bool mcpEnabled READ mcpEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QString mcpCatalogUrl READ mcpCatalogUrl NOTIFY settingsChanged)
    Q_PROPERTY(QString mcpServerUrl READ mcpServerUrl NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList mcpQuickServers READ mcpQuickServers NOTIFY settingsChanged)
    Q_PROPERTY(bool focusModeEnabled READ focusModeEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool focusModeAllowCriticalAlerts READ focusModeAllowCriticalAlerts NOTIFY settingsChanged)
    Q_PROPERTY(int focusModeDurationMinutes READ focusModeDurationMinutes NOTIFY settingsChanged)
    Q_PROPERTY(qlonglong focusModeUntilEpochMs READ focusModeUntilEpochMs NOTIFY settingsChanged)
    Q_PROPERTY(bool privateModeEnabled READ privateModeEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList permissionOverrides READ permissionOverrides NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList permissionCapabilityOptions READ permissionCapabilityOptions NOTIFY settingsChanged)
    Q_PROPERTY(bool tracePanelEnabled READ tracePanelEnabled NOTIFY agentStateChanged)
    Q_PROPERTY(QString agentStatus READ agentStatus NOTIFY agentStateChanged)
    Q_PROPERTY(bool agentAvailable READ agentAvailable NOTIFY agentStateChanged)
    Q_PROPERTY(QVariantList agentTraceEntries READ agentTraceEntries NOTIFY agentTraceChanged)
    Q_PROPERTY(QVariantList availableAgentTools READ availableAgentTools NOTIFY toolStatusesChanged)
    Q_PROPERTY(QVariantList installedSkills READ installedSkills NOTIFY toolStatusesChanged)
    Q_PROPERTY(QString platformName READ platformName NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap platformCapabilities READ platformCapabilities NOTIFY settingsChanged)
    Q_PROPERTY(bool supportsAutoToolInstall READ supportsAutoToolInstall NOTIFY settingsChanged)
    Q_PROPERTY(QString skillsRoot READ skillsRoot NOTIFY toolStatusesChanged)

public:
    explicit SettingsViewModel(BackendFacade *backend, QObject *parent = nullptr);

    QString assistantName() const;
    QString userName() const;
    QStringList models() const;
    QString selectedModel() const;
    QString lmStudioEndpoint() const;
    QString chatProviderKind() const;
    QString chatProviderApiKey() const;
    int defaultReasoningMode() const;
    bool autoRoutingEnabled() const;
    bool streamingEnabled() const;
    int requestTimeoutMs() const;
    bool visionEnabled() const;
    QString visionEndpoint() const;
    int visionTimeoutMs() const;
    int visionStaleThresholdMs() const;
    bool visionContextAlwaysOn() const;
    double visionObjectsMinConfidence() const;
    double visionGesturesMinConfidence() const;
    bool clickThroughEnabled() const;
    QStringList voicePresetNames() const;
    QStringList voicePresetIds() const;
    QString selectedVoicePresetId() const;
    QStringList whisperModelPresetNames() const;
    QStringList whisperModelPresetIds() const;
    QString selectedWhisperModelPresetId() const;
    QStringList intentModelPresetNames() const;
    QStringList intentModelPresetIds() const;
    QString selectedIntentModelId() const;
    QString intentModelPath() const;
    QString recommendedIntentModelLabel() const;
    QString intentHardwareSummary() const;
    QString wakeEngineKind() const;
    QString whisperExecutable() const;
    QString whisperModelPath() const;
    QString ttsEngineKind() const;
    QString piperExecutable() const;
    QString piperVoiceModel() const;
    QString qwenTtsExecutable() const;
    QString qwenTtsModelDir() const;
    QString qwenTtsLanguage() const;
    int qwenTtsThreads() const;
    double wakeTriggerThreshold() const;
    int wakeTriggerCooldownMs() const;
    QString ffmpegExecutable() const;
    double voiceSpeed() const;
    double voicePitch() const;
    double piperNoiseScale() const;
    double piperNoiseW() const;
    double piperSentenceSilence() const;
    QString ttsPostProcessMode() const;
    QStringList ttsPostProcessModes() const;
    QStringList ttsVoiceProfileNames() const;
    QStringList ttsVoiceProfileIds() const;
    QString ttsVoiceProfileId() const;
    double micSensitivity() const;
    bool aecEnabled() const;
    bool rnnoiseEnabled() const;
    double vadSensitivity() const;
    QStringList audioInputDeviceNames() const;
    QStringList audioInputDeviceIds() const;
    QStringList audioOutputDeviceNames() const;
    QStringList audioOutputDeviceIds() const;
    QString selectedAudioInputDeviceId() const;
    QString selectedAudioOutputDeviceId() const;
    QString uiMode() const;
    QString toolInstallStatus() const;
    QVariantList toolStatuses() const;
    int toolDownloadPercent() const;
    QString toolsRoot() const;
    QString wakeWordPhrase() const;
    bool agentEnabled() const;
    QString agentProviderMode() const;
    double conversationTemperature() const;
    double conversationTopP() const;
    double toolUseTemperature() const;
    int providerTopK() const;
    int maxOutputTokens() const;
    bool budgetEnforcementDisabled() const;
    bool memoryAutoWrite() const;
    QString webSearchProvider() const;
    QString braveSearchApiKey() const;
    bool mcpEnabled() const;
    QString mcpCatalogUrl() const;
    QString mcpServerUrl() const;
    QVariantList mcpQuickServers() const;
    bool focusModeEnabled() const;
    bool focusModeAllowCriticalAlerts() const;
    int focusModeDurationMinutes() const;
    qlonglong focusModeUntilEpochMs() const;
    bool privateModeEnabled() const;
    QVariantList permissionOverrides() const;
    QVariantList permissionCapabilityOptions() const;
    bool tracePanelEnabled() const;
    QString agentStatus() const;
    bool agentAvailable() const;
    QVariantList agentTraceEntries() const;
    QVariantList availableAgentTools() const;
    QVariantList installedSkills() const;
    QString platformName() const;
    QVariantMap platformCapabilities() const;
    bool supportsAutoToolInstall() const;
    QString skillsRoot() const;

    Q_INVOKABLE QVariantMap evaluateSetupRequirements(const QString &endpoint,
                                                      const QString &modelId,
                                                      const QString &whisperPath,
                                                      const QString &whisperModelPath,
                                                      const QString &piperPath,
                                                      const QString &voicePath,
                                                      const QString &ffmpegPath);
    Q_INVOKABLE void refreshModels();
    Q_INVOKABLE void refreshAudioDevices();
    Q_INVOKABLE void rescanTools();
    Q_INVOKABLE void setSelectedModel(const QString &modelId);
    Q_INVOKABLE void setSelectedIntentModelId(const QString &modelId);
    Q_INVOKABLE void setSelectedVoicePresetId(const QString &voiceId);
    Q_INVOKABLE void setTtsVoiceProfileId(const QString &profileId);
    Q_INVOKABLE void setWakeEngineKind(const QString &kind);
    Q_INVOKABLE void setTtsEngineKind(const QString &kind);
    Q_INVOKABLE void saveAudioProcessing(bool aecEnabled, bool rnnoiseEnabled, double vadSensitivity);
    Q_INVOKABLE void saveVisionSettings(bool enabled,
                                        const QString &endpoint,
                                        int timeoutMs,
                                        int staleThresholdMs,
                                        bool contextAlwaysOn,
                                        double objectsMinConfidence,
                                        double gesturesMinConfidence);
    Q_INVOKABLE void saveAgentSettings(bool enabled,
                                       const QString &providerMode,
                                       double conversationTemperature,
                                       double conversationTopP,
                                       double toolUseTemperature,
                                       int providerTopK,
                                       int maxOutputTokens,
                                       bool memoryAutoWrite,
                                       const QString &webSearchProvider,
                                       const QString &braveSearchApiKey,
                                       bool tracePanelEnabled,
                                       bool budgetEnforcementDisabled);
    Q_INVOKABLE void saveProviderSettings(const QString &providerKind,
                                          const QString &apiKey,
                                          const QString &endpoint);
    Q_INVOKABLE void saveSettings(const QString &endpoint,
                                  const QString &providerKind,
                                  const QString &apiKey,
                                  const QString &modelId,
                                  int defaultMode,
                                  bool autoRouting,
                                  bool streaming,
                                  int timeoutMs,
                                  bool aecEnabled,
                                  bool rnnoiseEnabled,
                                  double vadSensitivity,
                                  const QString &wakeEngineKind,
                                  const QString &whisperPath,
                                  const QString &whisperModelPath,
                                  double wakeThreshold,
                                  int wakeCooldownMs,
                                  const QString &ttsEngineKind,
                                  const QString &piperPath,
                                  const QString &voicePath,
                                  const QString &qwenTtsExecutable,
                                  const QString &qwenTtsModelDir,
                                  const QString &qwenTtsLanguage,
                                  int qwenTtsThreads,
                                  const QString &ffmpegPath,
                                  double voiceSpeed,
                                  double voicePitch,
                                  double piperNoiseScale,
                                  double piperNoiseW,
                                  double piperSentenceSilence,
                                  const QString &ttsPostProcessMode,
                                  const QString &ttsVoiceProfileId,
                                  double micSensitivity,
                                  const QString &audioInputDeviceId,
                                  const QString &audioOutputDeviceId,
                                  bool clickThrough);
    Q_INVOKABLE void setUiMode(const QString &mode);
    Q_INVOKABLE bool downloadVoiceModel(const QString &voiceId);
    Q_INVOKABLE bool downloadQwenTtsModel();
    Q_INVOKABLE bool downloadWhisperModel(const QString &modelId);
    Q_INVOKABLE void downloadModel(const QString &name);
    Q_INVOKABLE void downloadTool(const QString &name);
    Q_INVOKABLE void installAllTools();
    Q_INVOKABLE bool autoDetectVoiceTools();
    Q_INVOKABLE bool setUserName(const QString &userName);
    Q_INVOKABLE void openContainingDirectory(const QString &path);
    Q_INVOKABLE void openToolsHub();
    Q_INVOKABLE bool installSkill(const QString &url);
    Q_INVOKABLE bool createSkill(const QString &id, const QString &name, const QString &description);
    Q_INVOKABLE QVariantMap validateBraveSearchConnection(const QString &apiKey);
    Q_INVOKABLE bool saveToolsStoreSettings(const QString &webSearchProvider,
                                            bool mcpEnabled,
                                            const QString &mcpCatalogUrl,
                                            const QString &mcpServerUrl);
    Q_INVOKABLE bool installMcpQuickServer(const QString &presetId);
    Q_INVOKABLE bool installMcpPackage(const QString &packageSpec, const QString &serverIdHint);
    Q_INVOKABLE QVariantList recentBehaviorEvents(int limit = 50) const;
    Q_INVOKABLE QString behaviorLedgerDatabasePath() const;
    Q_INVOKABLE QString behaviorLedgerNdjsonPath() const;
    Q_INVOKABLE void activateFocusMode(int durationMinutes = 0, bool allowCriticalAlerts = true);
    Q_INVOKABLE void deactivateFocusMode();
    Q_INVOKABLE void setPrivateModeEnabled(bool enabled);
    Q_INVOKABLE bool savePermissionOverrides(const QVariantList &overrides);
    Q_INVOKABLE void startListening();
    Q_INVOKABLE bool runSetupScenario(const QString &userName,
                                      const QString &providerKind,
                                      const QString &apiKey,
                                      const QString &endpoint,
                                      const QString &modelId,
                                      const QString &whisperPath,
                                      const QString &whisperModelPath,
                                      double wakeThreshold,
                                      int wakeCooldownMs,
                                      const QString &piperPath,
                                      const QString &voicePath,
                                      const QString &ffmpegPath,
                                      const QString &audioInputDeviceId,
                                      const QString &audioOutputDeviceId,
                                      bool clickThrough,
                                      const QString &scenarioId);
    Q_INVOKABLE bool completeInitialSetup(const QString &userName,
                                          const QString &providerKind,
                                          const QString &apiKey,
                                          const QString &endpoint,
                                          const QString &modelId,
                                          const QString &whisperPath,
                                          const QString &whisperModelPath,
                                          double wakeThreshold,
                                          int wakeCooldownMs,
                                          const QString &piperPath,
                                          const QString &voicePath,
                                          const QString &ffmpegPath,
                                          const QString &audioInputDeviceId,
                                          const QString &audioOutputDeviceId,
                                          bool clickThrough);

signals:
    void modelsChanged();
    void selectedModelChanged();
    void settingsChanged();
    void profileChanged();
    void audioDevicesChanged();
    void toolInstallStatusChanged();
    void toolStatusesChanged();
    void agentStateChanged();
    void agentTraceChanged();
    void initialSetupFinished();

private:
    BackendFacade *m_backend = nullptr;
};

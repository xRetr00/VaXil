#pragma once

#include <QObject>
#include <QVariantMap>

class AssistantController;
class AppSettings;
class IdentityProfileService;
class OverlayController;
class ToolManager;

class BackendFacade : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString stateName READ stateName NOTIFY stateNameChanged)
    Q_PROPERTY(QString transcript READ transcript NOTIFY transcriptChanged)
    Q_PROPERTY(QString responseText READ responseText NOTIFY responseTextChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(double audioLevel READ audioLevel NOTIFY audioLevelChanged)
    Q_PROPERTY(QStringList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(QString selectedModel READ selectedModel NOTIFY selectedModelChanged)
    Q_PROPERTY(QStringList voicePresetNames READ voicePresetNames CONSTANT)
    Q_PROPERTY(QStringList voicePresetIds READ voicePresetIds CONSTANT)
    Q_PROPERTY(QString selectedVoicePresetId READ selectedVoicePresetId NOTIFY settingsChanged)
    Q_PROPERTY(QStringList whisperModelPresetNames READ whisperModelPresetNames CONSTANT)
    Q_PROPERTY(QStringList whisperModelPresetIds READ whisperModelPresetIds CONSTANT)
    Q_PROPERTY(QString selectedWhisperModelPresetId READ selectedWhisperModelPresetId NOTIFY settingsChanged)
    Q_PROPERTY(QStringList intentModelPresetNames READ intentModelPresetNames CONSTANT)
    Q_PROPERTY(QStringList intentModelPresetIds READ intentModelPresetIds CONSTANT)
    Q_PROPERTY(QString selectedIntentModelId READ selectedIntentModelId NOTIFY settingsChanged)
    Q_PROPERTY(QString intentModelPath READ intentModelPath NOTIFY settingsChanged)
    Q_PROPERTY(QString recommendedIntentModelId READ recommendedIntentModelId CONSTANT)
    Q_PROPERTY(QString recommendedIntentModelLabel READ recommendedIntentModelLabel CONSTANT)
    Q_PROPERTY(QString intentHardwareSummary READ intentHardwareSummary CONSTANT)
    Q_PROPERTY(bool overlayVisible READ overlayVisible NOTIFY overlayVisibleChanged)
    Q_PROPERTY(double presenceOffsetX READ presenceOffsetX NOTIFY presenceOffsetChanged)
    Q_PROPERTY(double presenceOffsetY READ presenceOffsetY NOTIFY presenceOffsetChanged)
    Q_PROPERTY(QString lmStudioEndpoint READ lmStudioEndpoint NOTIFY settingsChanged)
    Q_PROPERTY(QString chatProviderKind READ chatProviderKind NOTIFY settingsChanged)
    Q_PROPERTY(QString chatProviderApiKey READ chatProviderApiKey NOTIFY settingsChanged)
    Q_PROPERTY(int defaultReasoningMode READ defaultReasoningMode NOTIFY settingsChanged)
    Q_PROPERTY(bool autoRoutingEnabled READ autoRoutingEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool streamingEnabled READ streamingEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int requestTimeoutMs READ requestTimeoutMs NOTIFY settingsChanged)
    Q_PROPERTY(bool aecEnabled READ aecEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool rnnoiseEnabled READ rnnoiseEnabled NOTIFY settingsChanged)
    Q_PROPERTY(double vadSensitivity READ vadSensitivity NOTIFY settingsChanged)
    Q_PROPERTY(QString wakeEngineKind READ wakeEngineKind NOTIFY settingsChanged)
    Q_PROPERTY(QString whisperExecutable READ whisperExecutable NOTIFY settingsChanged)
    Q_PROPERTY(QString whisperModelPath READ whisperModelPath NOTIFY settingsChanged)
    Q_PROPERTY(QString ttsEngineKind READ ttsEngineKind NOTIFY settingsChanged)
    Q_PROPERTY(QString piperExecutable READ piperExecutable NOTIFY settingsChanged)
    Q_PROPERTY(QString piperVoiceModel READ piperVoiceModel NOTIFY settingsChanged)
    Q_PROPERTY(double wakeTriggerThreshold READ wakeTriggerThreshold NOTIFY settingsChanged)
    Q_PROPERTY(int wakeTriggerCooldownMs READ wakeTriggerCooldownMs NOTIFY settingsChanged)
    Q_PROPERTY(QString ffmpegExecutable READ ffmpegExecutable NOTIFY settingsChanged)
    Q_PROPERTY(double voiceSpeed READ voiceSpeed NOTIFY settingsChanged)
    Q_PROPERTY(double voicePitch READ voicePitch NOTIFY settingsChanged)
    Q_PROPERTY(double micSensitivity READ micSensitivity NOTIFY settingsChanged)
    Q_PROPERTY(QStringList audioInputDeviceNames READ audioInputDeviceNames NOTIFY audioDevicesChanged)
    Q_PROPERTY(QStringList audioInputDeviceIds READ audioInputDeviceIds NOTIFY audioDevicesChanged)
    Q_PROPERTY(QStringList audioOutputDeviceNames READ audioOutputDeviceNames NOTIFY audioDevicesChanged)
    Q_PROPERTY(QStringList audioOutputDeviceIds READ audioOutputDeviceIds NOTIFY audioDevicesChanged)
    Q_PROPERTY(QString selectedAudioInputDeviceId READ selectedAudioInputDeviceId NOTIFY settingsChanged)
    Q_PROPERTY(QString selectedAudioOutputDeviceId READ selectedAudioOutputDeviceId NOTIFY settingsChanged)
    Q_PROPERTY(bool clickThroughEnabled READ clickThroughEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QString assistantName READ assistantName NOTIFY profileChanged)
    Q_PROPERTY(QString userName READ userName NOTIFY profileChanged)
    Q_PROPERTY(bool initialSetupCompleted READ initialSetupCompleted NOTIFY settingsChanged)
    Q_PROPERTY(QString toolInstallStatus READ toolInstallStatus NOTIFY toolInstallStatusChanged)
    Q_PROPERTY(QVariantList toolStatuses READ toolStatuses NOTIFY toolStatusesChanged)
    Q_PROPERTY(int toolDownloadPercent READ toolDownloadPercent NOTIFY toolInstallStatusChanged)
    Q_PROPERTY(QString activeToolDownloadName READ activeToolDownloadName NOTIFY toolInstallStatusChanged)
    Q_PROPERTY(QString toolsRoot READ toolsRoot CONSTANT)
    Q_PROPERTY(QString wakeWordPhrase READ wakeWordPhrase NOTIFY settingsChanged)
    Q_PROPERTY(bool agentEnabled READ agentEnabled NOTIFY agentStateChanged)
    Q_PROPERTY(QString agentProviderMode READ agentProviderMode NOTIFY agentStateChanged)
    Q_PROPERTY(double conversationTemperature READ conversationTemperature NOTIFY agentStateChanged)
    Q_PROPERTY(double conversationTopP READ conversationTopP NOTIFY agentStateChanged)
    Q_PROPERTY(double toolUseTemperature READ toolUseTemperature NOTIFY agentStateChanged)
    Q_PROPERTY(int providerTopK READ providerTopK NOTIFY agentStateChanged)
    Q_PROPERTY(int maxOutputTokens READ maxOutputTokens NOTIFY agentStateChanged)
    Q_PROPERTY(bool memoryAutoWrite READ memoryAutoWrite NOTIFY agentStateChanged)
    Q_PROPERTY(QString webSearchProvider READ webSearchProvider NOTIFY agentStateChanged)
    Q_PROPERTY(QString braveSearchApiKey READ braveSearchApiKey NOTIFY agentStateChanged)
    Q_PROPERTY(bool mcpEnabled READ mcpEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QString mcpCatalogUrl READ mcpCatalogUrl NOTIFY settingsChanged)
    Q_PROPERTY(QString mcpServerUrl READ mcpServerUrl NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList mcpQuickServers READ mcpQuickServers NOTIFY settingsChanged)
    Q_PROPERTY(bool tracePanelEnabled READ tracePanelEnabled NOTIFY agentStateChanged)
    Q_PROPERTY(QString agentStatus READ agentStatus NOTIFY agentStateChanged)
    Q_PROPERTY(bool agentAvailable READ agentAvailable NOTIFY agentStateChanged)
    Q_PROPERTY(QVariantList agentTraceEntries READ agentTraceEntries NOTIFY agentTraceChanged)
    Q_PROPERTY(QVariantList availableAgentTools READ availableAgentTools NOTIFY toolStatusesChanged)
    Q_PROPERTY(QVariantList installedSkills READ installedSkills NOTIFY toolStatusesChanged)
    Q_PROPERTY(QVariantList backgroundTaskResults READ backgroundTaskResults NOTIFY backgroundTaskResultsChanged)
    Q_PROPERTY(bool backgroundPanelVisible READ backgroundPanelVisible NOTIFY backgroundPanelVisibleChanged)
    Q_PROPERTY(QString latestTaskToast READ latestTaskToast NOTIFY latestTaskToastChanged)
    Q_PROPERTY(QString latestTaskToastTone READ latestTaskToastTone NOTIFY latestTaskToastChanged)
    Q_PROPERTY(int latestTaskToastTaskId READ latestTaskToastTaskId NOTIFY latestTaskToastChanged)
    Q_PROPERTY(QString latestTaskToastType READ latestTaskToastType NOTIFY latestTaskToastChanged)
    Q_PROPERTY(QString skillsRoot READ skillsRoot CONSTANT)

public:
    BackendFacade(
        AppSettings *settings,
        IdentityProfileService *identityProfileService,
        AssistantController *assistantController,
        OverlayController *overlayController,
        QObject *parent = nullptr);

    QString stateName() const;
    QString transcript() const;
    QString responseText() const;
    QString statusText() const;
    double audioLevel() const;
    QStringList models() const;
    QString selectedModel() const;
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
    QString recommendedIntentModelId() const;
    QString recommendedIntentModelLabel() const;
    QString intentHardwareSummary() const;
    bool overlayVisible() const;
    double presenceOffsetX() const;
    double presenceOffsetY() const;
    QString lmStudioEndpoint() const;
    QString chatProviderKind() const;
    QString chatProviderApiKey() const;
    int defaultReasoningMode() const;
    bool autoRoutingEnabled() const;
    bool streamingEnabled() const;
    int requestTimeoutMs() const;
    bool aecEnabled() const;
    bool rnnoiseEnabled() const;
    double vadSensitivity() const;
    QString wakeEngineKind() const;
    QString whisperExecutable() const;
    QString whisperModelPath() const;
    QString ttsEngineKind() const;
    QString piperExecutable() const;
    QString piperVoiceModel() const;
    double wakeTriggerThreshold() const;
    int wakeTriggerCooldownMs() const;
    QString ffmpegExecutable() const;
    double voiceSpeed() const;
    double voicePitch() const;
    double micSensitivity() const;
    QStringList audioInputDeviceNames() const;
    QStringList audioInputDeviceIds() const;
    QStringList audioOutputDeviceNames() const;
    QStringList audioOutputDeviceIds() const;
    QString selectedAudioInputDeviceId() const;
    QString selectedAudioOutputDeviceId() const;
    bool clickThroughEnabled() const;
    QString assistantName() const;
    QString userName() const;
    bool initialSetupCompleted() const;
    QString toolInstallStatus() const;
    QVariantList toolStatuses() const;
    int toolDownloadPercent() const;
    QString activeToolDownloadName() const;
    QString toolsRoot() const;
    QString wakeWordPhrase() const;
    bool agentEnabled() const;
    QString agentProviderMode() const;
    double conversationTemperature() const;
    double conversationTopP() const;
    double toolUseTemperature() const;
    int providerTopK() const;
    int maxOutputTokens() const;
    bool memoryAutoWrite() const;
    QString webSearchProvider() const;
    QString braveSearchApiKey() const;
    bool mcpEnabled() const;
    QString mcpCatalogUrl() const;
    QString mcpServerUrl() const;
    QVariantList mcpQuickServers() const;
    bool tracePanelEnabled() const;
    QString agentStatus() const;
    bool agentAvailable() const;
    QVariantList agentTraceEntries() const;
    QVariantList availableAgentTools() const;
    QVariantList installedSkills() const;
    QVariantList backgroundTaskResults() const;
    bool backgroundPanelVisible() const;
    QString latestTaskToast() const;
    QString latestTaskToastTone() const;
    int latestTaskToastTaskId() const;
    QString latestTaskToastType() const;
    QString skillsRoot() const;

    Q_INVOKABLE void toggleOverlay();
    Q_INVOKABLE void refreshModels();
    Q_INVOKABLE void submitText(const QString &text);
    Q_INVOKABLE void startListening();
    Q_INVOKABLE void cancelRequest();
    Q_INVOKABLE void setSelectedModel(const QString &modelId);
    Q_INVOKABLE void setSelectedIntentModelId(const QString &modelId);
    Q_INVOKABLE void setAgentEnabled(bool enabled);
    Q_INVOKABLE void openToolsHub();
    Q_INVOKABLE void setBackgroundPanelVisible(bool visible);
    Q_INVOKABLE void notifyTaskToastShown(int taskId);
    Q_INVOKABLE void notifyTaskPanelRendered();
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
                                       bool tracePanelEnabled);
    Q_INVOKABLE void setSelectedVoicePresetId(const QString &voiceId);
    Q_INVOKABLE void setWakeEngineKind(const QString &kind);
    Q_INVOKABLE void setTtsEngineKind(const QString &kind);
    Q_INVOKABLE void saveAudioProcessing(bool aecEnabled, bool rnnoiseEnabled, double vadSensitivity);
    Q_INVOKABLE void saveSettings(
        const QString &endpoint,
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
        const QString &ffmpegPath,
        double voiceSpeed,
        double voicePitch,
        double micSensitivity,
        const QString &audioInputDeviceId,
        const QString &audioOutputDeviceId,
        bool clickThrough);
    Q_INVOKABLE bool downloadVoiceModel(const QString &voiceId);
    Q_INVOKABLE bool downloadWhisperModel(const QString &modelId);
    Q_INVOKABLE bool completeInitialSetup(
        const QString &userName,
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
    Q_INVOKABLE bool runSetupScenario(
        const QString &userName,
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
    Q_INVOKABLE QVariantMap evaluateSetupRequirements(
        const QString &endpoint,
        const QString &modelId,
        const QString &whisperPath,
        const QString &whisperModelPath,
        const QString &piperPath,
        const QString &voicePath,
        const QString &ffmpegPath);
    Q_INVOKABLE void openContainingDirectory(const QString &path);
    Q_INVOKABLE void rescanTools();
    Q_INVOKABLE void downloadTool(const QString &name);
    Q_INVOKABLE void downloadModel(const QString &name);
    Q_INVOKABLE void installAllTools();
    Q_INVOKABLE bool autoDetectVoiceTools();
    Q_INVOKABLE bool setUserName(const QString &userName);
    Q_INVOKABLE void refreshAudioDevices();
    Q_INVOKABLE bool installSkill(const QString &url);
    Q_INVOKABLE bool createSkill(const QString &id, const QString &name, const QString &description);
    Q_INVOKABLE bool saveToolsStoreSettings(const QString &webSearchProvider,
                                            bool mcpEnabled,
                                            const QString &mcpCatalogUrl,
                                            const QString &mcpServerUrl);
    Q_INVOKABLE bool installMcpQuickServer(const QString &presetId);
    Q_INVOKABLE bool installMcpPackage(const QString &packageSpec, const QString &serverIdHint);

signals:
    void stateNameChanged();
    void transcriptChanged();
    void responseTextChanged();
    void statusTextChanged();
    void audioLevelChanged();
    void modelsChanged();
    void selectedModelChanged();
    void overlayVisibleChanged();
    void presenceOffsetChanged();
    void audioDevicesChanged();
    void settingsChanged();
    void profileChanged();
    void initialSetupFinished();
    void toolInstallStatusChanged();
    void toolStatusesChanged();
    void agentStateChanged();
    void agentTraceChanged();
    void backgroundTaskResultsChanged();
    void backgroundPanelVisibleChanged();
    void latestTaskToastChanged();
    void toolsWindowRequested();

private:
    void setToolInstallStatus(const QString &status);

    AppSettings *m_settings = nullptr;
    IdentityProfileService *m_identityProfileService = nullptr;
    AssistantController *m_assistantController = nullptr;
    OverlayController *m_overlayController = nullptr;
    ToolManager *m_toolManager = nullptr;
    QString m_toolInstallStatus;
};

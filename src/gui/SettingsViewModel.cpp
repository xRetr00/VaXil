#include "gui/SettingsViewModel.h"

#include "gui/BackendFacade.h"

SettingsViewModel::SettingsViewModel(BackendFacade *backend, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
{
    if (!m_backend) {
        return;
    }

    connect(m_backend, &BackendFacade::modelsChanged, this, &SettingsViewModel::modelsChanged);
    connect(m_backend, &BackendFacade::selectedModelChanged, this, &SettingsViewModel::selectedModelChanged);
    connect(m_backend, &BackendFacade::settingsChanged, this, &SettingsViewModel::settingsChanged);
    connect(m_backend, &BackendFacade::profileChanged, this, &SettingsViewModel::profileChanged);
    connect(m_backend, &BackendFacade::audioDevicesChanged, this, &SettingsViewModel::audioDevicesChanged);
    connect(m_backend, &BackendFacade::toolInstallStatusChanged, this, &SettingsViewModel::toolInstallStatusChanged);
    connect(m_backend, &BackendFacade::toolStatusesChanged, this, &SettingsViewModel::toolStatusesChanged);
    connect(m_backend, &BackendFacade::agentStateChanged, this, &SettingsViewModel::agentStateChanged);
    connect(m_backend, &BackendFacade::agentTraceChanged, this, &SettingsViewModel::agentTraceChanged);
    connect(m_backend, &BackendFacade::initialSetupFinished, this, &SettingsViewModel::initialSetupFinished);
}

QString SettingsViewModel::assistantName() const
{
    return m_backend ? m_backend->assistantName() : QStringLiteral("J.A.R.V.I.S");
}

QString SettingsViewModel::userName() const
{
    return m_backend ? m_backend->userName() : QString();
}

QStringList SettingsViewModel::models() const
{
    return m_backend ? m_backend->models() : QStringList();
}

QString SettingsViewModel::selectedModel() const
{
    return m_backend ? m_backend->selectedModel() : QString();
}

QString SettingsViewModel::lmStudioEndpoint() const
{
    return m_backend ? m_backend->lmStudioEndpoint() : QString();
}

int SettingsViewModel::defaultReasoningMode() const
{
    return m_backend ? m_backend->defaultReasoningMode() : 1;
}

bool SettingsViewModel::autoRoutingEnabled() const
{
    return m_backend && m_backend->autoRoutingEnabled();
}

bool SettingsViewModel::streamingEnabled() const
{
    return m_backend && m_backend->streamingEnabled();
}

int SettingsViewModel::requestTimeoutMs() const
{
    return m_backend ? m_backend->requestTimeoutMs() : 120000;
}

bool SettingsViewModel::clickThroughEnabled() const
{
    return m_backend && m_backend->clickThroughEnabled();
}

QStringList SettingsViewModel::voicePresetNames() const
{
    return m_backend ? m_backend->voicePresetNames() : QStringList();
}

QStringList SettingsViewModel::voicePresetIds() const
{
    return m_backend ? m_backend->voicePresetIds() : QStringList();
}

QString SettingsViewModel::selectedVoicePresetId() const
{
    return m_backend ? m_backend->selectedVoicePresetId() : QString();
}

QStringList SettingsViewModel::whisperModelPresetNames() const
{
    return m_backend ? m_backend->whisperModelPresetNames() : QStringList();
}

QStringList SettingsViewModel::whisperModelPresetIds() const
{
    return m_backend ? m_backend->whisperModelPresetIds() : QStringList();
}

QString SettingsViewModel::selectedWhisperModelPresetId() const
{
    return m_backend ? m_backend->selectedWhisperModelPresetId() : QString();
}

QStringList SettingsViewModel::intentModelPresetNames() const
{
    return m_backend ? m_backend->intentModelPresetNames() : QStringList();
}

QStringList SettingsViewModel::intentModelPresetIds() const
{
    return m_backend ? m_backend->intentModelPresetIds() : QStringList();
}

QString SettingsViewModel::selectedIntentModelId() const
{
    return m_backend ? m_backend->selectedIntentModelId() : QString();
}

QString SettingsViewModel::intentModelPath() const
{
    return m_backend ? m_backend->intentModelPath() : QString();
}

QString SettingsViewModel::recommendedIntentModelLabel() const
{
    return m_backend ? m_backend->recommendedIntentModelLabel() : QString();
}

QString SettingsViewModel::intentHardwareSummary() const
{
    return m_backend ? m_backend->intentHardwareSummary() : QString();
}

QString SettingsViewModel::wakeEngineKind() const
{
    return m_backend ? m_backend->wakeEngineKind() : QStringLiteral("sherpa-onnx");
}

QString SettingsViewModel::whisperExecutable() const
{
    return m_backend ? m_backend->whisperExecutable() : QString();
}

QString SettingsViewModel::whisperModelPath() const
{
    return m_backend ? m_backend->whisperModelPath() : QString();
}

QString SettingsViewModel::ttsEngineKind() const
{
    return m_backend ? m_backend->ttsEngineKind() : QStringLiteral("piper");
}

QString SettingsViewModel::piperExecutable() const
{
    return m_backend ? m_backend->piperExecutable() : QString();
}

QString SettingsViewModel::piperVoiceModel() const
{
    return m_backend ? m_backend->piperVoiceModel() : QString();
}

double SettingsViewModel::preciseTriggerThreshold() const
{
    return m_backend ? m_backend->preciseTriggerThreshold() : 0.0;
}

int SettingsViewModel::preciseTriggerCooldownMs() const
{
    return m_backend ? m_backend->preciseTriggerCooldownMs() : 0;
}

QString SettingsViewModel::ffmpegExecutable() const
{
    return m_backend ? m_backend->ffmpegExecutable() : QString();
}

double SettingsViewModel::voiceSpeed() const
{
    return m_backend ? m_backend->voiceSpeed() : 1.0;
}

double SettingsViewModel::voicePitch() const
{
    return m_backend ? m_backend->voicePitch() : 1.0;
}

double SettingsViewModel::micSensitivity() const
{
    return m_backend ? m_backend->micSensitivity() : 0.02;
}

bool SettingsViewModel::aecEnabled() const
{
    return m_backend && m_backend->aecEnabled();
}

bool SettingsViewModel::rnnoiseEnabled() const
{
    return m_backend && m_backend->rnnoiseEnabled();
}

double SettingsViewModel::vadSensitivity() const
{
    return m_backend ? m_backend->vadSensitivity() : 0.5;
}

QStringList SettingsViewModel::audioInputDeviceNames() const
{
    return m_backend ? m_backend->audioInputDeviceNames() : QStringList();
}

QStringList SettingsViewModel::audioInputDeviceIds() const
{
    return m_backend ? m_backend->audioInputDeviceIds() : QStringList();
}

QStringList SettingsViewModel::audioOutputDeviceNames() const
{
    return m_backend ? m_backend->audioOutputDeviceNames() : QStringList();
}

QStringList SettingsViewModel::audioOutputDeviceIds() const
{
    return m_backend ? m_backend->audioOutputDeviceIds() : QStringList();
}

QString SettingsViewModel::selectedAudioInputDeviceId() const
{
    return m_backend ? m_backend->selectedAudioInputDeviceId() : QString();
}

QString SettingsViewModel::selectedAudioOutputDeviceId() const
{
    return m_backend ? m_backend->selectedAudioOutputDeviceId() : QString();
}

QString SettingsViewModel::toolInstallStatus() const
{
    return m_backend ? m_backend->toolInstallStatus() : QString();
}

QVariantList SettingsViewModel::toolStatuses() const
{
    return m_backend ? m_backend->toolStatuses() : QVariantList();
}

int SettingsViewModel::toolDownloadPercent() const
{
    return m_backend ? m_backend->toolDownloadPercent() : -1;
}

QString SettingsViewModel::toolsRoot() const
{
    return m_backend ? m_backend->toolsRoot() : QString();
}

QString SettingsViewModel::wakeWordPhrase() const
{
    return m_backend ? m_backend->wakeWordPhrase() : QStringLiteral("Jarvis");
}

bool SettingsViewModel::agentEnabled() const
{
    return m_backend && m_backend->agentEnabled();
}

QString SettingsViewModel::agentProviderMode() const
{
    return m_backend ? m_backend->agentProviderMode() : QString();
}

double SettingsViewModel::conversationTemperature() const
{
    return m_backend ? m_backend->conversationTemperature() : 0.0;
}

double SettingsViewModel::conversationTopP() const
{
    return m_backend ? m_backend->conversationTopP() : 0.0;
}

double SettingsViewModel::toolUseTemperature() const
{
    return m_backend ? m_backend->toolUseTemperature() : 0.0;
}

int SettingsViewModel::providerTopK() const
{
    return m_backend ? m_backend->providerTopK() : 0;
}

int SettingsViewModel::maxOutputTokens() const
{
    return m_backend ? m_backend->maxOutputTokens() : 0;
}

bool SettingsViewModel::memoryAutoWrite() const
{
    return m_backend && m_backend->memoryAutoWrite();
}

QString SettingsViewModel::webSearchProvider() const
{
    return m_backend ? m_backend->webSearchProvider() : QString();
}

bool SettingsViewModel::mcpEnabled() const
{
    return m_backend && m_backend->mcpEnabled();
}

QString SettingsViewModel::mcpCatalogUrl() const
{
    return m_backend ? m_backend->mcpCatalogUrl() : QString();
}

QString SettingsViewModel::mcpServerUrl() const
{
    return m_backend ? m_backend->mcpServerUrl() : QString();
}

bool SettingsViewModel::tracePanelEnabled() const
{
    return m_backend && m_backend->tracePanelEnabled();
}

QString SettingsViewModel::agentStatus() const
{
    return m_backend ? m_backend->agentStatus() : QString();
}

bool SettingsViewModel::agentAvailable() const
{
    return m_backend && m_backend->agentAvailable();
}

QVariantList SettingsViewModel::agentTraceEntries() const
{
    return m_backend ? m_backend->agentTraceEntries() : QVariantList();
}

QVariantList SettingsViewModel::availableAgentTools() const
{
    return m_backend ? m_backend->availableAgentTools() : QVariantList();
}

QVariantList SettingsViewModel::installedSkills() const
{
    return m_backend ? m_backend->installedSkills() : QVariantList();
}

QString SettingsViewModel::skillsRoot() const
{
    return m_backend ? m_backend->skillsRoot() : QString();
}

QVariantMap SettingsViewModel::evaluateSetupRequirements(const QString &endpoint,
                                                         const QString &modelId,
                                                         const QString &whisperPath,
                                                         const QString &whisperModelPath,
                                                         const QString &preciseEnginePath,
                                                         const QString &preciseModelPath,
                                                         const QString &piperPath,
                                                         const QString &voicePath,
                                                         const QString &ffmpegPath)
{
    return m_backend
        ? m_backend->evaluateSetupRequirements(endpoint,
                                              modelId,
                                              whisperPath,
                                              whisperModelPath,
                                              preciseEnginePath,
                                              preciseModelPath,
                                              piperPath,
                                              voicePath,
                                              ffmpegPath)
        : QVariantMap();
}

void SettingsViewModel::refreshModels()
{
    if (m_backend) {
        m_backend->refreshModels();
    }
}

void SettingsViewModel::refreshAudioDevices()
{
    if (m_backend) {
        m_backend->refreshAudioDevices();
    }
}

void SettingsViewModel::rescanTools()
{
    if (m_backend) {
        m_backend->rescanTools();
    }
}

void SettingsViewModel::setSelectedModel(const QString &modelId)
{
    if (m_backend) {
        m_backend->setSelectedModel(modelId);
    }
}

void SettingsViewModel::setSelectedIntentModelId(const QString &modelId)
{
    if (m_backend) {
        m_backend->setSelectedIntentModelId(modelId);
    }
}

void SettingsViewModel::setSelectedVoicePresetId(const QString &voiceId)
{
    if (m_backend) {
        m_backend->setSelectedVoicePresetId(voiceId);
    }
}

void SettingsViewModel::setWakeEngineKind(const QString &kind)
{
    if (m_backend) {
        m_backend->setWakeEngineKind(kind);
    }
}

void SettingsViewModel::setTtsEngineKind(const QString &kind)
{
    if (m_backend) {
        m_backend->setTtsEngineKind(kind);
    }
}

void SettingsViewModel::saveAudioProcessing(bool aecEnabled, bool rnnoiseEnabled, double vadSensitivity)
{
    if (m_backend) {
        m_backend->saveAudioProcessing(aecEnabled, rnnoiseEnabled, vadSensitivity);
    }
}

void SettingsViewModel::saveAgentSettings(bool enabled,
                                          const QString &providerMode,
                                          double conversationTemperature,
                                          double conversationTopP,
                                          double toolUseTemperature,
                                          int providerTopK,
                                          int maxOutputTokens,
                                          bool memoryAutoWrite,
                                          const QString &webSearchProvider,
                                          bool tracePanelEnabled)
{
    if (m_backend) {
        m_backend->saveAgentSettings(enabled,
                                     providerMode,
                                     conversationTemperature,
                                     conversationTopP,
                                     toolUseTemperature,
                                     providerTopK,
                                     maxOutputTokens,
                                     memoryAutoWrite,
                                     webSearchProvider,
                                     tracePanelEnabled);
    }
}

void SettingsViewModel::saveSettings(const QString &endpoint,
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
                                     const QString &preciseEnginePath,
                                     const QString &preciseModelPath,
                                     double preciseThreshold,
                                     int preciseCooldownMs,
                                     const QString &ttsEngineKind,
                                     const QString &piperPath,
                                     const QString &voicePath,
                                     const QString &ffmpegPath,
                                     double voiceSpeed,
                                     double voicePitch,
                                     double micSensitivity,
                                     const QString &audioInputDeviceId,
                                     const QString &audioOutputDeviceId,
                                     bool clickThrough)
{
    if (m_backend) {
        m_backend->saveSettings(endpoint,
                                modelId,
                                defaultMode,
                                autoRouting,
                                streaming,
                                timeoutMs,
                                aecEnabled,
                                rnnoiseEnabled,
                                vadSensitivity,
                                wakeEngineKind,
                                whisperPath,
                                whisperModelPath,
                                preciseEnginePath,
                                preciseModelPath,
                                preciseThreshold,
                                preciseCooldownMs,
                                ttsEngineKind,
                                piperPath,
                                voicePath,
                                ffmpegPath,
                                voiceSpeed,
                                voicePitch,
                                micSensitivity,
                                audioInputDeviceId,
                                audioOutputDeviceId,
                                clickThrough);
    }
}

bool SettingsViewModel::downloadVoiceModel(const QString &voiceId)
{
    return m_backend && m_backend->downloadVoiceModel(voiceId);
}

bool SettingsViewModel::downloadWhisperModel(const QString &modelId)
{
    return m_backend && m_backend->downloadWhisperModel(modelId);
}

void SettingsViewModel::downloadModel(const QString &name)
{
    if (m_backend) {
        m_backend->downloadModel(name);
    }
}

void SettingsViewModel::downloadTool(const QString &name)
{
    if (m_backend) {
        m_backend->downloadTool(name);
    }
}

void SettingsViewModel::installAllTools()
{
    if (m_backend) {
        m_backend->installAllTools();
    }
}

bool SettingsViewModel::autoDetectVoiceTools()
{
    return m_backend && m_backend->autoDetectVoiceTools();
}

bool SettingsViewModel::setUserName(const QString &userName)
{
    return m_backend && m_backend->setUserName(userName);
}

void SettingsViewModel::openContainingDirectory(const QString &path)
{
    if (m_backend) {
        m_backend->openContainingDirectory(path);
    }
}

void SettingsViewModel::openToolsHub()
{
    if (m_backend) {
        m_backend->openToolsHub();
    }
}

bool SettingsViewModel::installSkill(const QString &url)
{
    return m_backend && m_backend->installSkill(url);
}

bool SettingsViewModel::createSkill(const QString &id, const QString &name, const QString &description)
{
    return m_backend && m_backend->createSkill(id, name, description);
}

bool SettingsViewModel::saveToolsStoreSettings(const QString &webSearchProvider,
                                               bool mcpEnabled,
                                               const QString &mcpCatalogUrl,
                                               const QString &mcpServerUrl)
{
    return m_backend && m_backend->saveToolsStoreSettings(webSearchProvider, mcpEnabled, mcpCatalogUrl, mcpServerUrl);
}

void SettingsViewModel::startListening()
{
    if (m_backend) {
        m_backend->startListening();
    }
}

bool SettingsViewModel::runSetupScenario(const QString &userName,
                                         const QString &endpoint,
                                         const QString &modelId,
                                         const QString &whisperPath,
                                         const QString &whisperModelPath,
                                         const QString &preciseEnginePath,
                                         const QString &preciseModelPath,
                                         double preciseThreshold,
                                         int preciseCooldownMs,
                                         const QString &piperPath,
                                         const QString &voicePath,
                                         const QString &ffmpegPath,
                                         const QString &audioInputDeviceId,
                                         const QString &audioOutputDeviceId,
                                         bool clickThrough,
                                         const QString &scenarioId)
{
    return m_backend && m_backend->runSetupScenario(userName,
                                                    endpoint,
                                                    modelId,
                                                    whisperPath,
                                                    whisperModelPath,
                                                    preciseEnginePath,
                                                    preciseModelPath,
                                                    preciseThreshold,
                                                    preciseCooldownMs,
                                                    piperPath,
                                                    voicePath,
                                                    ffmpegPath,
                                                    audioInputDeviceId,
                                                    audioOutputDeviceId,
                                                    clickThrough,
                                                    scenarioId);
}

bool SettingsViewModel::completeInitialSetup(const QString &userName,
                                             const QString &endpoint,
                                             const QString &modelId,
                                             const QString &whisperPath,
                                             const QString &whisperModelPath,
                                             const QString &preciseEnginePath,
                                             const QString &preciseModelPath,
                                             double preciseThreshold,
                                             int preciseCooldownMs,
                                             const QString &piperPath,
                                             const QString &voicePath,
                                             const QString &ffmpegPath,
                                             const QString &audioInputDeviceId,
                                             const QString &audioOutputDeviceId,
                                             bool clickThrough)
{
    return m_backend && m_backend->completeInitialSetup(userName,
                                                        endpoint,
                                                        modelId,
                                                        whisperPath,
                                                        whisperModelPath,
                                                        preciseEnginePath,
                                                        preciseModelPath,
                                                        preciseThreshold,
                                                        preciseCooldownMs,
                                                        piperPath,
                                                        voicePath,
                                                        ffmpegPath,
                                                        audioInputDeviceId,
                                                        audioOutputDeviceId,
                                                        clickThrough);
}

#pragma once

#include <QHash>
#include <QObject>
#include <QThread>

#include "core/AssistantTypes.h"

class AppSettings;
class AgentToolbox;
class DeviceManager;
class IntentDetector;
class IntentEngine;
class IntentRouter;
class AiBackendClient;
class LocalResponseEngine;
class LoggingService;
class MemoryStore;
class ModelCatalogService;
class PromptAdapter;
class ReasoningRouter;
class IdentityProfileService;
class SkillStore;
class StreamAssembler;
class TaskDispatcher;
class SpeechRecognizer;
class TtsEngine;
class ToolWorker;
class WakeWordEngine;
class VoicePipelineRuntime;

class AssistantController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString stateName READ stateName NOTIFY stateChanged)
    Q_PROPERTY(QString transcript READ transcript NOTIFY transcriptChanged)
    Q_PROPERTY(QString responseText READ responseText NOTIFY responseTextChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(float audioLevel READ audioLevel NOTIFY audioLevelChanged)

public:
    AssistantController(
        AppSettings *settings,
        IdentityProfileService *identityProfileService,
        LoggingService *loggingService,
        QObject *parent = nullptr);
    ~AssistantController() override;

    void initialize();

    QString stateName() const;
    QString transcript() const;
    QString responseText() const;
    QString statusText() const;
    float audioLevel() const;
    bool startupReady() const;
    bool startupBlocked() const;
    QString startupBlockingIssue() const;
    QList<ModelInfo> availableModels() const;
    QStringList availableModelIds() const;
    QString selectedModel() const;
    QList<SkillManifest> installedSkills() const;
    QList<AgentToolSpec> availableAgentTools() const;
    AgentCapabilitySet agentCapabilities() const;
    QList<AgentTraceEntry> agentTrace() const;
    SamplingProfile samplingProfile() const;
    QList<BackgroundTaskResult> backgroundTaskResults() const;
    bool backgroundPanelVisible() const;
    QString latestTaskToast() const;
    QString latestTaskToastTone() const;
    int latestTaskToastTaskId() const;
    QString latestTaskToastType() const;
    bool installSkill(const QString &url, QString *error = nullptr);
    bool createSkill(const QString &id, const QString &name, const QString &description, QString *error = nullptr);

public slots:
    void refreshModels();
    void submitText(const QString &text);
    void startListening();
    void startWakeMonitor();
    void stopWakeMonitor();
    void stopListening();
    void cancelActiveRequest();
    void setSelectedModel(const QString &modelId);
    void setAgentEnabled(bool enabled);
    void setBackgroundPanelVisible(bool visible);
    void noteTaskToastShown(int taskId);
    void noteTaskPanelRendered();
    void saveAgentSettings(bool enabled,
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
    void saveSettings(
        const QString &providerKind,
        const QString &apiKey,
        const QString &endpoint,
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

signals:
    void stateChanged();
    void transcriptChanged();
    void responseTextChanged();
    void statusTextChanged();
    void audioLevelChanged();
    void modelsChanged();
    void agentStateChanged();
    void agentTraceChanged();
    void backgroundTaskResultsChanged();
    void backgroundPanelVisibleChanged();
    void latestTaskToastChanged();
    void startupStateChanged();
    void listeningRequested();
    void processingRequested();
    void speakingRequested();
    void idleRequested();

private:
    enum class AudioCaptureMode {
        None,
        Direct,
        WakeMonitor
    };

    enum class DuplexState {
        Open,
        WakeOnly,
        Listening,
        Processing,
        TtsExclusive,
        Cooldown
    };

    void setupStateMachine();
    void createWakeWordEngine();
    void bindWakeWordEngineSignals();
    void transitionToState(AssistantState state);
    void setStatus(const QString &status);
    void setDuplexState(DuplexState state);
    void invalidateWakeMonitorResume();
    void invalidateActiveTranscription();
    void clearActiveSpeechCapture();
    void beginTtsExclusiveMode();
    void enterPostSpeechCooldown();
    bool isMicrophoneBlocked() const;
    void activateConversationSession();
    void refreshConversationSession();
    void endConversationSession();
    bool conversationSessionShouldContinue() const;
    bool scheduleConversationSessionListening(int delayMs = 0);
    void pauseWakeMonitor();
    void resumeWakeMonitor(int delayMs = 0);
    void ignoreWakeTriggersFor(int delayMs);
    int shortWakeResumeDelayMs() const;
    int postSpeechWakeResumeDelayMs() const;
    int postSpeechWakeEngineStartDelayMs() const;
    int followUpListeningDelayMs() const;
    int conversationSessionTimeoutMs() const;
    int conversationSessionRestartDelayMs() const;
    int maxConversationSessionMisses() const;
    QString buildSttPrompt() const;
    bool shouldIgnoreAmbiguousTranscript(const QString &transcript) const;
    bool shouldEndConversationSession(const QString &input) const;
    void handleConversationSessionMiss(const QString &statusText);
    void updateUserProfileFromInput(const QString &input);
    LocalResponseContext buildLocalResponseContext() const;
    void deliverLocalResponse(const QString &text, const QString &status, bool speak = true);
    void scheduleWakeMonitorRestart(int delayMs = 250);
    bool canStartWakeMonitor() const;
    bool startAudioCapture(AudioCaptureMode mode, bool announceListening);
    void startConversationRequest(const QString &input);
    void startAgentConversationRequest(const QString &input, IntentType expectedIntent);
    void continueAgentConversation(const QList<AgentToolResult> &results);
    void startCommandRequest(const QString &input);
    void handleConversationFinished(const QString &text);
    void handleHybridAgentFinished(const QString &payload);
    void handleAgentResponse(const AgentResponse &response);
    void handleCommandFinished(const QString &text);
    void dispatchBackgroundTasks(const QList<AgentTask> &tasks);
    void recordTaskResult(const QJsonObject &resultObject);
    void startWebSearchSummaryRequest(const BackgroundTaskResult &result);
    QStringList backgroundAllowedRoots() const;
    void logPromptResponsePair(const QString &response, const QString &source, const QString &status = QString());
    void appendAgentTrace(const QString &kind, const QString &title, const QString &detail, bool success = true);
    CommandEnvelope parseCommand(const QString &payload) const;
    void updateStartupState();
    QString resolveStartupBlockingIssue(bool *blocked = nullptr) const;
    QString resolveWakeEngineRuntimePath() const;
    QString resolveWakeEngineModelPath() const;
    QString wakeEngineDisplayName() const;

    AppSettings *m_settings = nullptr;
    IdentityProfileService *m_identityProfileService = nullptr;
    LoggingService *m_loggingService = nullptr;
    AiBackendClient *m_aiBackendClient = nullptr;
    ModelCatalogService *m_modelCatalogService = nullptr;
    ReasoningRouter *m_reasoningRouter = nullptr;
    PromptAdapter *m_promptAdapter = nullptr;
    StreamAssembler *m_streamAssembler = nullptr;
    MemoryStore *m_memoryStore = nullptr;
    SkillStore *m_skillStore = nullptr;
    AgentToolbox *m_agentToolbox = nullptr;
    DeviceManager *m_deviceManager = nullptr;
    IntentEngine *m_intentEngine = nullptr;
    IntentDetector *m_backgroundIntentDetector = nullptr;
    IntentRouter *m_intentRouter = nullptr;
    LocalResponseEngine *m_localResponseEngine = nullptr;
    TaskDispatcher *m_taskDispatcher = nullptr;
    ToolWorker *m_toolWorker = nullptr;
    SpeechRecognizer *m_whisperSttEngine = nullptr;
    WakeWordEngine *m_wakeWordEngine = nullptr;
    TtsEngine *m_ttsEngine = nullptr;
    VoicePipelineRuntime *m_voicePipelineRuntime = nullptr;
    AssistantState m_currentState = AssistantState::Idle;
    DuplexState m_duplexState = DuplexState::Open;
    QString m_transcript;
    QString m_responseText;
    QString m_statusText = QStringLiteral("Initializing");
    float m_audioLevel = 0.0f;
    quint64 m_activeRequestId = 0;
    quint64 m_activeInputCaptureId = 0;
    quint64 m_activeSttRequestId = 0;
    RequestKind m_activeRequestKind = RequestKind::Conversation;
    QString m_lastPromptForAiLog;
    QString m_lastAgentInput;
    IntentType m_lastAgentIntent = IntentType::GENERAL_CHAT;
    QString m_previousAgentResponseId;
    int m_activeAgentIteration = 0;
    AgentCapabilitySet m_agentCapabilities;
    QList<AgentTraceEntry> m_agentTrace;
    QList<BackgroundTaskResult> m_backgroundTaskResults;
    bool m_backgroundPanelVisible = false;
    QString m_latestTaskToast;
    QString m_latestTaskToastTone = QStringLiteral("status");
    int m_latestTaskToastTaskId = -1;
    QString m_latestTaskToastType = QStringLiteral("background");
    int m_nextTaskId = 1;
    QHash<QString, int> m_activeBackgroundTaskIds;
    AudioCaptureMode m_audioCaptureMode = AudioCaptureMode::None;
    AudioCaptureMode m_lastCompletedCaptureMode = AudioCaptureMode::None;
    bool m_wakeMonitorEnabled = false;
    bool m_followUpListeningAfterWakeAck = false;
    bool m_conversationSessionActive = false;
    int m_consecutiveSessionMisses = 0;
    qint64 m_conversationSessionExpiresAtMs = 0;
    quint64 m_wakeResumeSequence = 0;
    qint64 m_ignoreWakeUntilMs = 0;
    bool m_startupReady = false;
    bool m_startupBlocked = false;
    bool m_modelCatalogResolved = false;
    bool m_wakeEngineReady = false;
    bool m_wakeStartRequested = false;
    QString m_lastWakeError;
    QString m_startupBlockingIssue = QStringLiteral("Loading services...");
    QThread m_toolWorkerThread;
};

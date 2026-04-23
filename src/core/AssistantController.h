#pragma once

#include <QHash>
#include <QObject>
#include <QPair>
#include <QThread>
#include <optional>
#include <memory>

#include "companion/contracts/ActionProposal.h"
#include "companion/contracts/CooldownState.h"
#include "companion/contracts/FocusModeState.h"
#include "core/AssistantTypes.h"
#include "core/tools/AgentToolLoopGuard.h"

class AppSettings;
class AgentToolbox;
class BrowserBookmarksMonitor;
class CalendarIcsMonitor;
class ConnectorSnapshotMonitor;
class DeviceManager;
class InboxMaildropMonitor;
class NotesDirectoryMonitor;
class ConnectorHistoryTracker;
class IntentDetector;
class IntentEngine;
class IntentRouter;
class AiBackendClient;
class GestureActionRouter;
class GestureInterpreter;
class GestureStateMachine;
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
class VisionIngestService;
class WakeWordEngine;
class WakeWordDataCapture;
class VoicePipelineRuntime;
class WorldStateCache;
class AiRequestCoordinator;
class ActionThreadTracker;
class AssistantBehaviorPolicy;
class ExecutionNarrator;
class MemoryPolicyHandler;
class ResponseFinalizer;
class SpeechTranscriptGuard;
class ToolCoordinator;
class TurnSignalExtractor;
class TurnStateAnalyzer;
class UserGoalInferer;
class ExecutionIntentPlanner;
class RouteArbitrator;
class RoutingTraceEmitter;
class IntentConfidenceCalculator;
class LocalIntentAdvisor;
namespace LearningData {
class LearningDataCollector;
}
class TurnOrchestrationRuntime;
namespace LearningData {
class LearningDataCollector;
}
struct ProactiveSuggestionPlan;
struct SelectionContextCompilation;
struct InputRouterContext;
struct IntentResult;
struct SpokenReply;

class AssistantController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString stateName READ stateName NOTIFY stateChanged)
    Q_PROPERTY(QString transcript READ transcript NOTIFY transcriptChanged)
    Q_PROPERTY(QString responseText READ responseText NOTIFY responseTextChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(float audioLevel READ audioLevel NOTIFY audioLevelChanged)
    Q_PROPERTY(int wakeTriggerToken READ wakeTriggerToken NOTIFY wakeTriggerTokenChanged)

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
    int wakeTriggerToken() const;
    AssistantSurfaceState assistantSurfaceState() const;
    QString assistantSurfaceActivityPrimary() const;
    QString assistantSurfaceActivitySecondary() const;
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
    QString latestProactiveSuggestion() const;
    QString latestProactiveSuggestionTone() const;
    QString latestProactiveSuggestionType() const;
    bool installSkill(const QString &url, QString *error = nullptr);
    bool createSkill(const QString &id, const QString &name, const QString &description, QString *error = nullptr);

public slots:
    void refreshModels();
    void submitText(const QString &text);
    void startListening();
    void interruptSpeechAndListen();
    void startWakeMonitor();
    void stopWakeMonitor();
    void stopSpeaking();
    void stopListening();
    void cancelActiveRequest();
    void cancelCurrentRequest();
    void captureMissedWakeWordSample(const QString &notes = QString());
    void setSelectedModel(const QString &modelId);
    void setAgentEnabled(bool enabled);
    void setBackgroundPanelVisible(bool visible);
    void noteTaskToastShown(int taskId);
    void noteProactiveSuggestionFeedback(const QString &signalType, const QString &suggestionType);
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
                           bool tracePanelEnabled,
                           bool budgetEnforcementDisabled);
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
    void updateDesktopContext(const QString &summary, const QVariantMap &context);

signals:
    void stateChanged();
    void transcriptChanged();
    void responseTextChanged();
    void statusTextChanged();
    void audioLevelChanged();
    void wakeTriggerTokenChanged();
    void modelsChanged();
    void agentStateChanged();
    void agentTraceChanged();
    void backgroundTaskResultsChanged();
    void backgroundPanelVisibleChanged();
    void latestTaskToastChanged();
    void latestProactiveSuggestionChanged();
    void assistantSurfaceChanged();
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
    void noteWakeTrigger();
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
    QString prepareSubmitInput(const QString &trimmed, bool *wakeDetected) const;
    InputRouterContext buildInputRouteContext(const QString &routedInput,
                                              const IntentResult &detectedIntent,
                                              const IntentResult &effectiveIntent,
                                              LocalIntent localIntent,
                                              const AiAvailability &availability,
                                              bool visionRelevantQuery,
                                              bool wakeOnly,
                                              bool shouldEndConversation,
                                              bool isTimeQuery,
                                              bool isDateQuery,
                                              bool hasDeterministicTask,
                                              const AgentTask &deterministicTask,
                                              const QString &deterministicSpoken,
                                              qint64 nowMs) const;
    bool executeRouteDecision(const InputRouteDecision &decision,
                              const QString &routedInput,
                              LocalIntent localIntent,
                              bool confirmationGranted,
                              qint64 nowMs,
                              QString *executedRoute = nullptr);
    bool shouldIgnoreAmbiguousTranscript(const QString &transcript) const;
    bool shouldEndConversationSession(const QString &input) const;
    void handleConversationSessionMiss(const QString &statusText);
    LocalResponseContext buildLocalResponseContext() const;
    bool finalizeReply(const QString &source,
                       const SpokenReply &reply,
                       const QString &status,
                       int restartDelayMs,
                       bool logAgentExchange = false,
                       bool allowFollowUpWakeDelay = false);
    void deliverLocalResponse(const QString &text, const QString &status, bool speak = true);
    void scheduleWakeMonitorRestart(int delayMs = 250);
    bool canStartWakeMonitor() const;
    void reconfigureGestureActionRouter();
    bool startAudioCapture(AudioCaptureMode mode, bool announceListening);
    void startConversationRequest(const QString &input);
    void startAgentConversationRequest(const QString &input, IntentType expectedIntent);
    void continueAgentConversation(const QList<AgentToolResult> &results);
    QList<AgentToolResult> executeAgentToolCalls(const QList<AgentToolCall> &toolCalls);
    void startCommandRequest(const QString &input);
    void handleVisionSnapshot(const VisionSnapshot &snapshot);
    QString buildDirectVisionResponse(const QString &input) const;
    QString buildVisionPromptContext(const QString &input, IntentType intent) const;
    QString buildDesktopPromptContext(const QString &input, IntentType intent) const;
    QString buildAssistantPromptContext(const QString &input, IntentType intent) const;
    QString buildDesktopSelectionInput(const QString &input,
                                       IntentType intent,
                                       const QString &purpose) const;
    bool shouldUseVisionContext(const QString &input, IntentType intent) const;
    void applyVisionGestureTriggers(const VisionSnapshot &snapshot);
    void handleGestureFarewell();
    void handleGestureConfirm();
    void handleGestureReject();
    void handleConversationFinished(const QString &text);
    void handleHybridAgentFinished(const QString &payload);
    void handleAgentResponse(const AgentResponse &response);
    void handleCommandFinished(const QString &text);
    void dispatchBackgroundTasks(const QList<AgentTask> &tasks);
    void recordConnectorEvent(const ConnectorEvent &event);
    void recordTaskResult(const QJsonObject &resultObject);
    void setSurfaceError(const QString &source, const QString &primary, const QString &secondary = QString());
    void clearSurfaceError(const QString &source = QString());
    void startActionThreadCompletionRequest(const ActionThread &thread);
    QString buildActionThreadContinuationInput(const QString &userInput) const;
    QString buildActionThreadCompletionInput(const ActionThread &thread) const;
    void beginActionThread(const QList<AgentTask> &tasks, qint64 nowMs);
    void rememberActionThreadResult(const BackgroundTaskResult &result, qint64 nowMs);
    void rememberCompletedActionReply(const QString &taskType,
                                      const QString &summary,
                                      bool success,
                                      qint64 nowMs);
    void clearActionThread();
    bool handlePendingConfirmationInput(const QString &input);
    void storePendingConfirmation(const InputRouteDecision &decision,
                                  const QString &input,
                                  LocalIntent localIntent);
    void clearPendingConfirmation();
    bool requiresConfirmationFor(const InputRouteDecision &decision) const;
    QStringList backgroundAllowedRoots() const;
    void logPromptResponsePair(const QString &response, const QString &source, const QString &status = QString());
    void appendAgentTrace(const QString &kind, const QString &title, const QString &detail, bool success = true);
    CommandEnvelope parseCommand(const QString &payload) const;
    void updateStartupState();
    QString resolveStartupBlockingIssue(bool *blocked = nullptr) const;
    QString resolveWakeEngineRuntimePath() const;
    QString resolveWakeEngineModelPath() const;
    QString wakeEngineDisplayName() const;
    FocusModeState currentFocusModeState() const;
    QVariantMap buildConnectorPlannerMetadata(const QString &sourceKind,
                                              const QString &connectorKind,
                                              const QString &historyKey,
                                              const QVariantMap &baseMetadata,
                                              qint64 nowMs);
    ProactiveSuggestionPlan planNextStepSuggestion(const QString &sourceKind,
                                                   const QString &taskType,
                                                   const QString &resultSummary,
                                                   const QStringList &sourceUrls,
                                                   const QVariantMap &sourceMetadata,
                                                   const QString &presentationKey,
                                                   bool success) const;
    ActionProposal buildNextStepProposal(const QString &hint,
                                         const QString &sourceKind,
                                         const QString &taskType,
                                         bool success) const;
    QString gateNextStepHint(const QString &hint,
                             const QString &sourceKind,
                             const QString &taskType,
                             bool success) const;
    void logPlannedSuggestion(const ProactiveSuggestionPlan &plan,
                              const QString &sourceKind,
                              const QString &taskType) const;
    bool speakProactiveSuggestion(const ProactiveSuggestionPlan &plan,
                                  const QString &surfaceKind,
                                  const QString &taskType,
                                  const QString &priority,
                                  qint64 nowMs);
    void commitProactivePresentation(const QString &surfaceKind,
                                     const QString &taskType,
                                     const QString &priority,
                                     const QString &reasonCode);
    void logCompiledContextDelta(const QString &purpose,
                                 const QString &input,
                                 const SelectionContextCompilation &selectionContext);
    MemoryRecord buildCompiledContextStabilityMemory(const QString &purpose) const;
    QList<MemoryRecord> buildCompiledContextHistoryMemory() const;
    QString selectTemporalPromptContext(const QString &purpose,
                                        const SelectionContextCompilation &selectionContext,
                                        QList<MemoryRecord> *selectedPromptRecords,
                                        QStringList *suppressedPromptKeys,
                                        int *stablePromptCycles,
                                        qint64 *stablePromptDurationMs,
                                        QString *reasonCode);
    void considerConnectorEvent(const ConnectorEvent &event);
    void considerDesktopContextSuggestion(const QString &summary, const QVariantMap &context);
    void considerTaskResultSuggestion(const BackgroundTaskResult &result);
    void setLatestProactiveSuggestion(const QString &message,
                                      const QString &tone,
                                      const QString &type);
    void logProactiveSuggestionFeedback(const QString &signalType, const QString &suggestionType);
    void ensureLearningSession();
    void closeLearningSession();
    QString allocateLearningTurnId();
    void recordFeedbackSignalForLearning(const QString &feedbackType,
                                         const QString &freeformText = QString(),
                                         const QStringList &linkedEventIds = {},
                                         const QString &severity = QStringLiteral("normal"));

    AppSettings *m_settings = nullptr;
    IdentityProfileService *m_identityProfileService = nullptr;
    LoggingService *m_loggingService = nullptr;
    AiBackendClient *m_aiBackendClient = nullptr;
    ModelCatalogService *m_modelCatalogService = nullptr;
    ReasoningRouter *m_reasoningRouter = nullptr;
    PromptAdapter *m_promptAdapter = nullptr;
    StreamAssembler *m_streamAssembler = nullptr;
    MemoryStore *m_memoryStore = nullptr;
    std::unique_ptr<AiRequestCoordinator> m_aiRequestCoordinator;
    std::unique_ptr<ActionThreadTracker> m_actionThreadTracker;
    std::unique_ptr<SpeechTranscriptGuard> m_speechTranscriptGuard;
    std::unique_ptr<AssistantBehaviorPolicy> m_assistantBehaviorPolicy;
    std::unique_ptr<ExecutionNarrator> m_executionNarrator;
    std::unique_ptr<MemoryPolicyHandler> m_memoryPolicyHandler;
    std::unique_ptr<ToolCoordinator> m_toolCoordinator;
    std::unique_ptr<TurnSignalExtractor> m_turnSignalExtractor;
    std::unique_ptr<TurnStateAnalyzer> m_turnStateAnalyzer;
    std::unique_ptr<UserGoalInferer> m_userGoalInferer;
    std::unique_ptr<ExecutionIntentPlanner> m_executionIntentPlanner;
    std::unique_ptr<RouteArbitrator> m_routeArbitrator;
    std::unique_ptr<RoutingTraceEmitter> m_routingTraceEmitter;
    std::unique_ptr<IntentConfidenceCalculator> m_intentConfidenceCalculator;
    std::unique_ptr<LocalIntentAdvisor> m_localIntentAdvisor;
    std::unique_ptr<TurnOrchestrationRuntime> m_turnOrchestrationRuntime;
    std::unique_ptr<LearningData::LearningDataCollector> m_learningDataCollector;
    std::unique_ptr<WakeWordDataCapture> m_wakeWordDataCapture;
  AgentToolbox *m_agentToolbox = nullptr;
    SkillStore *m_skillStore = nullptr;
    DeviceManager *m_deviceManager = nullptr;
    IntentEngine *m_intentEngine = nullptr;
    IntentDetector *m_backgroundIntentDetector = nullptr;
    IntentRouter *m_intentRouter = nullptr;
    LocalResponseEngine *m_localResponseEngine = nullptr;
    TaskDispatcher *m_taskDispatcher = nullptr;
    ToolWorker *m_toolWorker = nullptr;
    BrowserBookmarksMonitor *m_browserBookmarksMonitor = nullptr;
    CalendarIcsMonitor *m_calendarIcsMonitor = nullptr;
    ConnectorSnapshotMonitor *m_connectorSnapshotMonitor = nullptr;
    InboxMaildropMonitor *m_inboxMaildropMonitor = nullptr;
    NotesDirectoryMonitor *m_notesDirectoryMonitor = nullptr;
    SpeechRecognizer *m_whisperSttEngine = nullptr;
    WakeWordEngine *m_wakeWordEngine = nullptr;
    TtsEngine *m_ttsEngine = nullptr;
    VisionIngestService *m_visionIngestService = nullptr;
    VoicePipelineRuntime *m_voicePipelineRuntime = nullptr;
    WorldStateCache *m_worldStateCache = nullptr;
    GestureInterpreter *m_gestureInterpreter = nullptr;
    GestureStateMachine *m_gestureStateMachine = nullptr;
    GestureActionRouter *m_gestureActionRouter = nullptr;
    AssistantState m_currentState = AssistantState::Idle;
    DuplexState m_duplexState = DuplexState::Open;
    QString m_transcript;
    QString m_responseText;
    QString m_statusText = QStringLiteral("Initializing");
    float m_audioLevel = 0.0f;
    int m_wakeTriggerToken = 0;
    quint64 m_activeRequestId = 0;
    quint64 m_activeInputCaptureId = 0;
    quint64 m_activeSttRequestId = 0;
    RequestKind m_activeRequestKind = RequestKind::Conversation;
    QString m_lastPromptForAiLog;
    QString m_lastAgentInput;
    IntentType m_lastAgentIntent = IntentType::GENERAL_CHAT;
    ReasoningMode m_activeReasoningMode = ReasoningMode::Balanced;
    bool m_activeAgentUsesResponses = false;
    ActionSession m_activeActionSession;
    bool m_hasPendingConfirmation = false;
    ActionSession m_pendingActionSession;
    InputRouteDecision m_pendingRouteDecision;
    QString m_pendingRouteInput;
    LocalIntent m_pendingLocalIntent = LocalIntent::Unknown;
    std::optional<ActionThread> m_recentActionThread;
    QString m_previousAgentResponseId;
    int m_activeAgentIteration = 0;
    AgentToolLoopGuardState m_agentToolLoopGuardState;
    AgentCapabilitySet m_agentCapabilities;
    QList<AgentTraceEntry> m_agentTrace;
    QList<BackgroundTaskResult> m_backgroundTaskResults;
    qint64 m_lastVisionGestureTriggerMs = 0;
    QString m_lastVisionGestureAction;
    qint64 m_lastVisionQueryMs = 0;
    qint64 m_learningTurnCounter = 0;
    bool m_backgroundPanelVisible = false;
    QString m_latestProactiveSuggestion;
    QString m_latestProactiveSuggestionTone = QStringLiteral("response");
    QString m_latestProactiveSuggestionType = QStringLiteral("proactive");
    std::unique_ptr<ConnectorHistoryTracker> m_connectorHistoryTracker;
    QString m_lastProactiveSuggestionThreadId;
    qint64 m_lastProactiveSuggestionMs = 0;
    CooldownState m_proactiveCooldownState;
    QString m_surfaceErrorSource;
    QString m_surfaceErrorPrimary;
    QString m_surfaceErrorSecondary;
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
    bool m_learningSessionStarted = false;
    bool m_lastInputFromVoice = false;
    QString m_lastWakeError;
    QString m_learningSessionId;
    QString m_learningSessionStartedAt;
    QString m_activeTurnId;
    QString m_nextTurnId;
    QString m_lastAudioEventId;
    QString m_lastToolDecisionEventId;
    QString m_lastToolExecutionEventId;
    QString m_lastBehaviorEventId;
    QString m_lastMemoryEventId;
    QString m_startupBlockingIssue = QStringLiteral("Loading services...");
    QString m_latestDesktopContextSummary;
    QVariantMap m_latestDesktopContext;
    qint64 m_latestDesktopContextAtMs = 0;
    QHash<QString, QString> m_lastCompiledContextSummaryByPurpose;
    QHash<QString, QStringList> m_lastCompiledContextKeysByPurpose;
    QHash<QString, qint64> m_lastCompiledContextChangedAtMsByPurpose;
    QHash<QString, int> m_compiledContextStableCyclesByPurpose;
    QHash<QString, QString> m_lastCompiledContextStabilitySummaryByPurpose;
    QHash<QString, QStringList> m_lastCompiledContextStableKeysByPurpose;
    QHash<QString, qint64> m_lastCompiledContextStableDurationMsByPurpose;
    QHash<QString, QString> m_lastPromptContextByPurpose;
    QHash<QString, QStringList> m_lastPromptContextKeysByPurpose;
    QHash<QString, qint64> m_lastPromptContextChangedAtMsByPurpose;
    QHash<QString, int> m_promptContextStableCyclesByPurpose;
    std::unique_ptr<ResponseFinalizer> m_responseFinalizer;
    QThread m_toolWorkerThread;
    QThread m_gestureActionRouterThread;
};

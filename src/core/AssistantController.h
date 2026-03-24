#pragma once

#include <QObject>

#include "core/AssistantTypes.h"

class AppSettings;
class AudioInputService;
class DeviceManager;
class IntentRouter;
class LmStudioClient;
class LocalResponseEngine;
class LoggingService;
class MemoryStore;
class ModelCatalogService;
class PiperTtsEngine;
class PromptAdapter;
class ReasoningRouter;
class IdentityProfileService;
class StreamAssembler;
class WhisperSttEngine;

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

    void initialize();

    QString stateName() const;
    QString transcript() const;
    QString responseText() const;
    QString statusText() const;
    float audioLevel() const;
    QList<ModelInfo> availableModels() const;
    QStringList availableModelIds() const;
    QString selectedModel() const;

public slots:
    void refreshModels();
    void submitText(const QString &text);
    void startListening();
    void startWakeMonitor();
    void stopWakeMonitor();
    void stopListening();
    void cancelActiveRequest();
    void setSelectedModel(const QString &modelId);
    void saveSettings(
        const QString &endpoint,
        const QString &modelId,
        int defaultMode,
        bool autoRouting,
        bool streaming,
        int timeoutMs,
        const QString &whisperPath,
        const QString &whisperModelPath,
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

    void setupStateMachine();
    void transitionToState(AssistantState state);
    void setStatus(const QString &status);
    void updateUserProfileFromInput(const QString &input);
    LocalResponseContext buildLocalResponseContext() const;
    void deliverLocalResponse(const QString &text, const QString &status, bool speak = true);
    void scheduleWakeMonitorRestart(int delayMs = 250);
    bool canStartWakeMonitor() const;
    bool startAudioCapture(AudioCaptureMode mode, bool announceListening);
    void startConversationRequest(const QString &input);
    void startCommandRequest(const QString &input);
    void handleConversationFinished(const QString &text);
    void handleCommandFinished(const QString &text);
    CommandEnvelope parseCommand(const QString &payload) const;

    AppSettings *m_settings = nullptr;
    IdentityProfileService *m_identityProfileService = nullptr;
    LoggingService *m_loggingService = nullptr;
    LmStudioClient *m_lmStudioClient = nullptr;
    ModelCatalogService *m_modelCatalogService = nullptr;
    ReasoningRouter *m_reasoningRouter = nullptr;
    PromptAdapter *m_promptAdapter = nullptr;
    StreamAssembler *m_streamAssembler = nullptr;
    MemoryStore *m_memoryStore = nullptr;
    DeviceManager *m_deviceManager = nullptr;
    IntentRouter *m_intentRouter = nullptr;
    LocalResponseEngine *m_localResponseEngine = nullptr;
    AudioInputService *m_audioInputService = nullptr;
    WhisperSttEngine *m_whisperSttEngine = nullptr;
    PiperTtsEngine *m_piperTtsEngine = nullptr;
    AssistantState m_currentState = AssistantState::Idle;
    QString m_transcript;
    QString m_responseText;
    QString m_statusText = QStringLiteral("Initializing");
    float m_audioLevel = 0.0f;
    quint64 m_activeRequestId = 0;
    RequestKind m_activeRequestKind = RequestKind::Conversation;
    AudioCaptureMode m_audioCaptureMode = AudioCaptureMode::None;
    AudioCaptureMode m_lastCompletedCaptureMode = AudioCaptureMode::None;
    bool m_wakeMonitorEnabled = false;
    bool m_followUpListeningAfterWakeAck = false;
};

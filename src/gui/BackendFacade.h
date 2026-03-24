#pragma once

#include <QObject>

class AssistantController;
class AppSettings;
class IdentityProfileService;
class OverlayController;

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
    Q_PROPERTY(bool overlayVisible READ overlayVisible NOTIFY overlayVisibleChanged)
    Q_PROPERTY(QString lmStudioEndpoint READ lmStudioEndpoint NOTIFY settingsChanged)
    Q_PROPERTY(int defaultReasoningMode READ defaultReasoningMode NOTIFY settingsChanged)
    Q_PROPERTY(bool autoRoutingEnabled READ autoRoutingEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool streamingEnabled READ streamingEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int requestTimeoutMs READ requestTimeoutMs NOTIFY settingsChanged)
    Q_PROPERTY(QString whisperExecutable READ whisperExecutable NOTIFY settingsChanged)
    Q_PROPERTY(QString piperExecutable READ piperExecutable NOTIFY settingsChanged)
    Q_PROPERTY(QString piperVoiceModel READ piperVoiceModel NOTIFY settingsChanged)
    Q_PROPERTY(QString ffmpegExecutable READ ffmpegExecutable NOTIFY settingsChanged)
    Q_PROPERTY(double voiceSpeed READ voiceSpeed NOTIFY settingsChanged)
    Q_PROPERTY(double voicePitch READ voicePitch NOTIFY settingsChanged)
    Q_PROPERTY(double micSensitivity READ micSensitivity NOTIFY settingsChanged)
    Q_PROPERTY(bool clickThroughEnabled READ clickThroughEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QString assistantName READ assistantName NOTIFY profileChanged)
    Q_PROPERTY(QString userName READ userName NOTIFY profileChanged)
    Q_PROPERTY(bool initialSetupCompleted READ initialSetupCompleted NOTIFY settingsChanged)
    Q_PROPERTY(QString toolInstallStatus READ toolInstallStatus NOTIFY toolInstallStatusChanged)

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
    bool overlayVisible() const;
    QString lmStudioEndpoint() const;
    int defaultReasoningMode() const;
    bool autoRoutingEnabled() const;
    bool streamingEnabled() const;
    int requestTimeoutMs() const;
    QString whisperExecutable() const;
    QString piperExecutable() const;
    QString piperVoiceModel() const;
    QString ffmpegExecutable() const;
    double voiceSpeed() const;
    double voicePitch() const;
    double micSensitivity() const;
    bool clickThroughEnabled() const;
    QString assistantName() const;
    QString userName() const;
    bool initialSetupCompleted() const;
    QString toolInstallStatus() const;

    Q_INVOKABLE void toggleOverlay();
    Q_INVOKABLE void refreshModels();
    Q_INVOKABLE void submitText(const QString &text);
    Q_INVOKABLE void startListening();
    Q_INVOKABLE void cancelRequest();
    Q_INVOKABLE void setSelectedModel(const QString &modelId);
    Q_INVOKABLE void saveSettings(
        const QString &endpoint,
        const QString &modelId,
        int defaultMode,
        bool autoRouting,
        bool streaming,
        int timeoutMs,
        const QString &whisperPath,
        const QString &piperPath,
        const QString &voicePath,
        const QString &ffmpegPath,
        double voiceSpeed,
        double voicePitch,
        double micSensitivity,
        bool clickThrough);
    Q_INVOKABLE void completeInitialSetup(
        const QString &userName,
        const QString &endpoint,
        const QString &modelId,
        const QString &whisperPath,
        const QString &piperPath,
        const QString &voicePath,
        const QString &ffmpegPath,
        bool clickThrough);
    Q_INVOKABLE bool autoDetectVoiceTools();
    Q_INVOKABLE bool installAndDetectVoiceTools();

signals:
    void stateNameChanged();
    void transcriptChanged();
    void responseTextChanged();
    void statusTextChanged();
    void audioLevelChanged();
    void modelsChanged();
    void selectedModelChanged();
    void overlayVisibleChanged();
    void settingsChanged();
    void profileChanged();
    void initialSetupFinished();
    void toolInstallStatusChanged();

private:
    void setToolInstallStatus(const QString &status);

    AppSettings *m_settings = nullptr;
    IdentityProfileService *m_identityProfileService = nullptr;
    AssistantController *m_assistantController = nullptr;
    OverlayController *m_overlayController = nullptr;
    QString m_toolInstallStatus;
};

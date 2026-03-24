#pragma once

#include <QObject>
#include <QVariantMap>

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
    Q_PROPERTY(QStringList voicePresetNames READ voicePresetNames CONSTANT)
    Q_PROPERTY(QStringList voicePresetIds READ voicePresetIds CONSTANT)
    Q_PROPERTY(QString selectedVoicePresetId READ selectedVoicePresetId NOTIFY settingsChanged)
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
    Q_PROPERTY(QStringList audioInputDeviceNames READ audioInputDeviceNames NOTIFY audioDevicesChanged)
    Q_PROPERTY(QStringList audioInputDeviceIds READ audioInputDeviceIds NOTIFY audioDevicesChanged)
    Q_PROPERTY(QStringList audioOutputDeviceNames READ audioOutputDeviceNames NOTIFY audioDevicesChanged)
    Q_PROPERTY(QStringList audioOutputDeviceIds READ audioOutputDeviceIds NOTIFY audioDevicesChanged)
    Q_PROPERTY(QString selectedAudioInputDeviceId READ selectedAudioInputDeviceId NOTIFY settingsChanged)
    Q_PROPERTY(QString selectedAudioOutputDeviceId READ selectedAudioOutputDeviceId NOTIFY settingsChanged)
    Q_PROPERTY(bool clickThroughEnabled READ clickThroughEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QString assistantName READ assistantName NOTIFY profileChanged)
    Q_PROPERTY(QString userName READ userName NOTIFY profileChanged)
    Q_PROPERTY(QString spokenUserName READ spokenUserName NOTIFY profileChanged)
    Q_PROPERTY(bool initialSetupCompleted READ initialSetupCompleted NOTIFY settingsChanged)
    Q_PROPERTY(QString toolInstallStatus READ toolInstallStatus NOTIFY toolInstallStatusChanged)
    Q_PROPERTY(QString wakeWordPhrase READ wakeWordPhrase NOTIFY settingsChanged)

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
    QStringList audioInputDeviceNames() const;
    QStringList audioInputDeviceIds() const;
    QStringList audioOutputDeviceNames() const;
    QStringList audioOutputDeviceIds() const;
    QString selectedAudioInputDeviceId() const;
    QString selectedAudioOutputDeviceId() const;
    bool clickThroughEnabled() const;
    QString assistantName() const;
    QString userName() const;
    QString spokenUserName() const;
    bool initialSetupCompleted() const;
    QString toolInstallStatus() const;
    QString wakeWordPhrase() const;

    Q_INVOKABLE void toggleOverlay();
    Q_INVOKABLE void refreshModels();
    Q_INVOKABLE void submitText(const QString &text);
    Q_INVOKABLE void startListening();
    Q_INVOKABLE void cancelRequest();
    Q_INVOKABLE void setSelectedModel(const QString &modelId);
    Q_INVOKABLE void setSelectedVoicePresetId(const QString &voiceId);
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
        const QString &audioInputDeviceId,
        const QString &audioOutputDeviceId,
        bool clickThrough);
    Q_INVOKABLE bool downloadVoiceModel(const QString &voiceId);
    Q_INVOKABLE bool completeInitialSetup(
        const QString &displayName,
        const QString &spokenName,
        const QString &endpoint,
        const QString &modelId,
        const QString &whisperPath,
        const QString &piperPath,
        const QString &voicePath,
        const QString &ffmpegPath,
        const QString &audioInputDeviceId,
        const QString &audioOutputDeviceId,
        bool clickThrough);
    Q_INVOKABLE bool runSetupScenario(
        const QString &displayName,
        const QString &spokenName,
        const QString &endpoint,
        const QString &modelId,
        const QString &whisperPath,
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
        const QString &piperPath,
        const QString &voicePath,
        const QString &ffmpegPath);
    Q_INVOKABLE void openContainingDirectory(const QString &path);
    Q_INVOKABLE bool autoDetectVoiceTools();
    Q_INVOKABLE bool installAndDetectVoiceTools();
    Q_INVOKABLE void refreshAudioDevices();

signals:
    void stateNameChanged();
    void transcriptChanged();
    void responseTextChanged();
    void statusTextChanged();
    void audioLevelChanged();
    void modelsChanged();
    void selectedModelChanged();
    void overlayVisibleChanged();
    void audioDevicesChanged();
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

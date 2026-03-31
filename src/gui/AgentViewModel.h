#pragma once

#include <QObject>
#include <QString>

class BackendFacade;

class AgentViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString stateName READ stateName NOTIFY stateNameChanged)
    Q_PROPERTY(int uiState READ uiState NOTIFY uiStateChanged)
    Q_PROPERTY(QString transcript READ transcript NOTIFY transcriptChanged)
    Q_PROPERTY(QString responseText READ responseText NOTIFY responseTextChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(int assistantSurfaceState READ assistantSurfaceState NOTIFY assistantSurfaceChanged)
    Q_PROPERTY(QString assistantSurfaceActivityPrimary READ assistantSurfaceActivityPrimary NOTIFY assistantSurfaceChanged)
    Q_PROPERTY(QString assistantSurfaceActivitySecondary READ assistantSurfaceActivitySecondary NOTIFY assistantSurfaceChanged)
    Q_PROPERTY(double audioLevel READ audioLevel NOTIFY audioLevelChanged)
    Q_PROPERTY(int wakeTriggerToken READ wakeTriggerToken NOTIFY wakeTriggerTokenChanged)
    Q_PROPERTY(bool overlayVisible READ overlayVisible NOTIFY overlayVisibleChanged)
    Q_PROPERTY(double presenceOffsetX READ presenceOffsetX NOTIFY presenceOffsetChanged)
    Q_PROPERTY(double presenceOffsetY READ presenceOffsetY NOTIFY presenceOffsetChanged)
    Q_PROPERTY(QString assistantName READ assistantName NOTIFY profileChanged)
    Q_PROPERTY(QString userName READ userName NOTIFY profileChanged)

public:
    enum UiState {
        IDLE = 0,
        LISTENING = 1,
        THINKING = 2,
        EXECUTING = 3
    };
    Q_ENUM(UiState)

    enum SurfaceState {
        READY = 0,
        SURFACE_LISTENING = 1,
        SURFACE_THINKING = 2,
        SURFACE_SPEAKING = 3,
        TOOL_RUNNING = 4,
        ERROR = 5
    };
    Q_ENUM(SurfaceState)

    explicit AgentViewModel(BackendFacade *backend, QObject *parent = nullptr);

    QString stateName() const;
    int uiState() const;
    QString transcript() const;
    QString responseText() const;
    QString statusText() const;
    int assistantSurfaceState() const;
    QString assistantSurfaceActivityPrimary() const;
    QString assistantSurfaceActivitySecondary() const;
    double audioLevel() const;
    int wakeTriggerToken() const;
    bool overlayVisible() const;
    double presenceOffsetX() const;
    double presenceOffsetY() const;
    QString assistantName() const;
    QString userName() const;

    Q_INVOKABLE void toggleOverlay();
    Q_INVOKABLE void submitText(const QString &text);
    Q_INVOKABLE void startListening();
    Q_INVOKABLE void interruptSpeechAndListen();
    Q_INVOKABLE void cancelRequest();

signals:
    void stateNameChanged();
    void uiStateChanged();
    void transcriptChanged();
    void responseTextChanged();
    void statusTextChanged();
    void assistantSurfaceChanged();
    void audioLevelChanged();
    void wakeTriggerTokenChanged();
    void overlayVisibleChanged();
    void presenceOffsetChanged();
    void profileChanged();

private:
    int mapUiState(const QString &stateName) const;
    QString normalizeStateName(const QString &stateName) const;

    BackendFacade *m_backend = nullptr;
};

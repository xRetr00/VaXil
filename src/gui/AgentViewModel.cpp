#include "gui/AgentViewModel.h"

#include "gui/BackendFacade.h"

AgentViewModel::AgentViewModel(BackendFacade *backend, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
{
    if (!m_backend) {
        return;
    }

    connect(m_backend, &BackendFacade::stateNameChanged, this, [this]() {
        emit stateNameChanged();
        emit uiStateChanged();
    });
    connect(m_backend, &BackendFacade::transcriptChanged, this, &AgentViewModel::transcriptChanged);
    connect(m_backend, &BackendFacade::responseTextChanged, this, &AgentViewModel::responseTextChanged);
    connect(m_backend, &BackendFacade::statusTextChanged, this, &AgentViewModel::statusTextChanged);
    connect(m_backend, &BackendFacade::assistantSurfaceChanged, this, &AgentViewModel::assistantSurfaceChanged);
    connect(m_backend, &BackendFacade::audioLevelChanged, this, &AgentViewModel::audioLevelChanged);
    connect(m_backend, &BackendFacade::wakeTriggerTokenChanged, this, &AgentViewModel::wakeTriggerTokenChanged);
    connect(m_backend, &BackendFacade::overlayVisibleChanged, this, &AgentViewModel::overlayVisibleChanged);
    connect(m_backend, &BackendFacade::presenceOffsetChanged, this, &AgentViewModel::presenceOffsetChanged);
    connect(m_backend, &BackendFacade::profileChanged, this, &AgentViewModel::profileChanged);
}

QString AgentViewModel::stateName() const
{
    return normalizeStateName(m_backend ? m_backend->stateName() : QStringLiteral("IDLE"));
}

int AgentViewModel::uiState() const
{
    return mapUiState(stateName());
}

QString AgentViewModel::transcript() const
{
    return m_backend ? m_backend->transcript() : QString();
}

QString AgentViewModel::responseText() const
{
    return m_backend ? m_backend->responseText() : QString();
}

QString AgentViewModel::statusText() const
{
    return m_backend ? m_backend->statusText() : QString();
}

int AgentViewModel::assistantSurfaceState() const
{
    return m_backend ? m_backend->assistantSurfaceState() : READY;
}

QString AgentViewModel::assistantSurfaceActivityPrimary() const
{
    return m_backend ? m_backend->assistantSurfaceActivityPrimary() : QString();
}

QString AgentViewModel::assistantSurfaceActivitySecondary() const
{
    return m_backend ? m_backend->assistantSurfaceActivitySecondary() : QString();
}

double AgentViewModel::audioLevel() const
{
    return m_backend ? m_backend->audioLevel() : 0.0;
}

int AgentViewModel::wakeTriggerToken() const
{
    return m_backend ? m_backend->wakeTriggerToken() : 0;
}

bool AgentViewModel::overlayVisible() const
{
    return m_backend && m_backend->overlayVisible();
}

double AgentViewModel::presenceOffsetX() const
{
    return m_backend ? m_backend->presenceOffsetX() : 0.0;
}

double AgentViewModel::presenceOffsetY() const
{
    return m_backend ? m_backend->presenceOffsetY() : 0.0;
}

QString AgentViewModel::assistantName() const
{
    return m_backend ? m_backend->assistantName() : QStringLiteral("Vaxil");
}

QString AgentViewModel::userName() const
{
    return m_backend ? m_backend->userName() : QString();
}

void AgentViewModel::toggleOverlay()
{
    if (m_backend) {
        m_backend->toggleOverlay();
    }
}

void AgentViewModel::submitText(const QString &text)
{
    if (m_backend) {
        m_backend->submitText(text);
    }
}

void AgentViewModel::startListening()
{
    if (m_backend) {
        m_backend->startListening();
    }
}

void AgentViewModel::interruptSpeechAndListen()
{
    if (m_backend) {
        m_backend->interruptSpeechAndListen();
    }
}

void AgentViewModel::cancelRequest()
{
    if (m_backend) {
        m_backend->cancelRequest();
    }
}

int AgentViewModel::mapUiState(const QString &stateName) const
{
    const QString normalized = normalizeStateName(stateName);
    if (normalized == QStringLiteral("LISTENING")) {
        return LISTENING;
    }
    if (normalized == QStringLiteral("THINKING")) {
        return THINKING;
    }
    if (normalized == QStringLiteral("EXECUTING")) {
        return EXECUTING;
    }
    return IDLE;
}

QString AgentViewModel::normalizeStateName(const QString &stateName) const
{
    const QString normalized = stateName.trimmed().toUpper();
    if (normalized == QStringLiteral("PROCESSING")) {
        return QStringLiteral("THINKING");
    }
    if (normalized == QStringLiteral("SPEAKING")) {
        return QStringLiteral("EXECUTING");
    }
    if (normalized.isEmpty()) {
        return QStringLiteral("IDLE");
    }
    return normalized;
}

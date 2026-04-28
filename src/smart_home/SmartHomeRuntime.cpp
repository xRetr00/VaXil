#include "smart_home/SmartHomeRuntime.h"

#include <algorithm>

#include <QDateTime>
#include <QMutex>
#include <QRegularExpression>
#include <QTimer>
#include <QLocale>

#include "logging/LoggingService.h"
#include "settings/AppSettings.h"
#include "settings/IdentityProfileService.h"
#include "smart_home/DesktopBleIdentityAdapter.h"
#include "smart_home/HomeAssistantSmartHomeAdapter.h"

namespace {
QMutex &sharedSnapshotMutex()
{
    static QMutex mutex;
    return mutex;
}

SmartHomeSnapshot &sharedSnapshot()
{
    static SmartHomeSnapshot snapshot;
    return snapshot;
}

QString safeTokenEnvVarForLog(QString value)
{
    value = value.trimmed();
    static const QRegularExpression envNamePattern(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    if (envNamePattern.match(value).hasMatch()) {
        return value;
    }
    return QStringLiteral("VAXIL_HOME_ASSISTANT_TOKEN");
}

QString localTimeText(qint64 utcMs)
{
    if (utcMs <= 0) {
        return QLocale().toString(QDateTime::currentDateTime(), QStringLiteral("HH:mm"));
    }
    return QLocale().toString(QDateTime::fromMSecsSinceEpoch(utcMs, Qt::UTC).toLocalTime(), QStringLiteral("HH:mm"));
}
}

SmartHomeRuntime::SmartHomeRuntime(AppSettings *settings,
                                   IdentityProfileService *identityProfileService,
                                   LoggingService *loggingService,
                                   QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_identityProfileService(identityProfileService)
    , m_loggingService(loggingService)
    , m_pollTimer(new QTimer(this))
{
    connect(m_pollTimer, &QTimer::timeout, this, &SmartHomeRuntime::pollState);
    if (m_settings) {
        connect(m_settings, &AppSettings::settingsChanged, this, &SmartHomeRuntime::reconfigureFromSettings);
    }
}

void SmartHomeRuntime::start()
{
    reconfigureFromSettings();
}

void SmartHomeRuntime::stop()
{
    m_pollTimer->stop();
    if (m_identityAdapter) {
        m_identityAdapter->stop();
    }
    m_pollInFlight = false;
}

SmartHomeSnapshot SmartHomeRuntime::latestSnapshot() const
{
    return m_latestSnapshot;
}

SmartHomeSnapshot SmartHomeRuntime::latestSharedSnapshot()
{
    QMutexLocker locker(&sharedSnapshotMutex());
    return sharedSnapshot();
}

QString SmartHomeRuntime::renderTemplate(QString templ,
                                         const QString &userName,
                                         const SmartRoomUnknownOccupantEvent &event,
                                         qint64 fallbackTimeMs)
{
    if (templ.trimmed().isEmpty()) {
        templ = QStringLiteral("Welcome back, {user_name}.");
    }
    const QString name = userName.trimmed().isEmpty() ? QStringLiteral("there") : userName.trimmed();
    const qint64 eventTimeMs = event.lastSeenAtUtcMs > 0
        ? event.lastSeenAtUtcMs
        : (event.firstDetectedAtUtcMs > 0 ? event.firstDetectedAtUtcMs : fallbackTimeMs);
    templ.replace(QStringLiteral("{user_name}"), name);
    templ.replace(QStringLiteral("{event_time}"), localTimeText(eventTimeMs));
    templ.replace(QStringLiteral("{room_name}"), QStringLiteral("room"));
    return templ.trimmed();
}

void SmartHomeRuntime::reconfigureFromSettings()
{
    m_config = configFromSettings();
    if (m_adapter == nullptr) {
        m_adapter = new HomeAssistantSmartHomeAdapter(m_config, this);
    } else {
        m_adapter->setConfig(m_config);
    }
    reconfigureIdentityAdapter();

    m_pollTimer->setInterval(std::max(1000, m_config.pollIntervalMs));
    if (!m_config.enabled) {
        stop();
        emit statusChanged(QStringLiteral("Smart room disabled"), false);
        return;
    }

    if (m_loggingService) {
        m_loggingService->infoFor(
            QStringLiteral("tools_mcp"),
            QStringLiteral("[smart_home] provider=%1 enabled=true baseUrlConfigured=%2 presenceEntity=%3 lightEntity=%4 pollIntervalMs=%5 tokenEnv=%6")
                .arg(m_config.provider,
                     m_config.homeAssistantBaseUrl.trimmed().isEmpty() ? QStringLiteral("false") : QStringLiteral("true"),
                     m_config.presenceEntityId,
                     m_config.lightEntityId,
                     QString::number(m_config.pollIntervalMs),
                     safeTokenEnvVarForLog(m_config.homeAssistantTokenEnvVar)));
    }

    if (!m_pollTimer->isActive()) {
        m_pollTimer->start();
    }
    pollState();
}

void SmartHomeRuntime::pollState()
{
    if (!m_config.enabled || m_adapter == nullptr || m_pollInFlight) {
        return;
    }
    m_pollInFlight = true;
    m_adapter->fetchState([this](const SmartHomeSnapshot &snapshot) {
        m_pollInFlight = false;
        handleSnapshot(snapshot);
    });
}

SmartHomeConfig SmartHomeRuntime::configFromSettings() const
{
    SmartHomeConfig config;
    if (!m_settings) {
        return config;
    }
    config.enabled = m_settings->smartHomeEnabled();
    config.provider = m_settings->smartHomeProvider();
    config.homeAssistantBaseUrl = m_settings->smartHomeHomeAssistantBaseUrl();
    config.homeAssistantTokenEnvVar = m_settings->smartHomeHomeAssistantTokenEnvVar();
    config.homeAssistantIdentityEntityId = m_settings->smartHomeHomeAssistantIdentityEntityId();
    config.presenceEntityId = m_settings->smartHomePresenceEntityId();
    config.lightEntityId = m_settings->smartHomeLightEntityId();
    config.identityMode = m_settings->smartHomeIdentityMode();
    config.bleBeaconUuid = m_settings->smartHomeBleBeaconUuid();
    config.pollIntervalMs = m_settings->smartHomePollIntervalMs();
    config.sensorOnlyWelcomeEnabled = m_settings->smartHomeSensorOnlyWelcomeEnabled();
    config.welcomeCooldownMinutes = m_settings->smartHomeWelcomeCooldownMinutes();
    config.roomAbsenceGraceMinutes = m_settings->smartHomeRoomAbsenceGraceMinutes();
    config.requestTimeoutMs = m_settings->smartHomeRequestTimeoutMs();
    config.identityMissingTimeoutMinutes = m_settings->smartHomeIdentityMissingTimeoutMinutes();
    config.bleAwayTimeoutMinutes = m_settings->smartHomeBleMissingTimeoutMinutes();
    config.bleScanIntervalMs = m_settings->smartHomeBleScanIntervalMs();
    config.bleRssiThreshold = m_settings->smartHomeBleRssiThreshold();
    config.welcomeEnabled = m_settings->smartHomeWelcomeEnabled();
    config.welcomeCooldownEnabled = m_settings->smartHomeWelcomeCooldownEnabled();
    config.personalWelcomeEnabled = m_settings->smartHomePersonalWelcomeEnabled();
    config.unknownOccupantBlocksWelcomeEnabled = m_settings->smartHomeUnknownOccupantBlocksWelcomeEnabled();
    config.unknownOccupantSpokenAlertsEnabled = m_settings->smartHomeUnknownOccupantSpokenAlertsEnabled();
    config.personalWelcomeTemplate = m_settings->smartHomePersonalWelcomeTemplate();
    config.personalWelcomeWithAlertTemplate = m_settings->smartHomePersonalWelcomeWithAlertTemplate();
    config.unknownOccupantMessageTemplate = m_settings->smartHomeUnknownOccupantMessageTemplate();
    config.unknownOccupantAlertResponseTemplate = m_settings->smartHomeUnknownOccupantAlertResponseTemplate();
    return config;
}

void SmartHomeRuntime::handleSnapshot(const SmartHomeSnapshot &snapshot)
{
    SmartHomeSnapshot enrichedSnapshot = snapshot;
    if (m_loggingService) {
        m_loggingService->infoFor(
            QStringLiteral("tools_mcp"),
            QStringLiteral("[smart_home.poll] success=%1 httpStatus=%2 latencyMs=%3 presenceAvailable=%4 occupied=%5 lightAvailable=%6 lightOn=%7")
                .arg(snapshot.success ? QStringLiteral("true") : QStringLiteral("false"),
                     QString::number(snapshot.httpStatus),
                     QString::number(snapshot.latencyMs),
                     snapshot.presence.available ? QStringLiteral("true") : QStringLiteral("false"),
                     snapshot.presence.occupied ? QStringLiteral("true") : QStringLiteral("false"),
                     snapshot.light.available ? QStringLiteral("true") : QStringLiteral("false"),
                     snapshot.light.on ? QStringLiteral("true") : QStringLiteral("false")));
    }

    if (!snapshot.success) {
        m_latestSnapshot = enrichedSnapshot;
        emit statusChanged(snapshot.detail.trimmed().isEmpty() ? QStringLiteral("Smart room unavailable") : snapshot.detail, false);
        return;
    }

    emit statusChanged(QStringLiteral("Smart room connected"), true);
    SmartRoomStateMachineConfig stateConfig;
    stateConfig.sensorOnlyWelcomeEnabled = m_config.sensorOnlyWelcomeEnabled;
    stateConfig.roomAbsenceGraceMinutes = m_config.roomAbsenceGraceMinutes;
    stateConfig.identityMissingTimeoutMinutes = m_config.identityMissingTimeoutMinutes;
    stateConfig.bleAwayTimeoutMinutes = m_config.bleAwayTimeoutMinutes;
    const std::optional<BleIdentitySnapshot> identity = m_identityAdapter
        ? m_identityAdapter->latestIdentityPresence()
        : snapshot.identity;
    const SmartRoomTransition transition = m_stateMachine.evaluate({
        .presence = snapshot.presence,
        .identity = identity,
        .config = stateConfig,
        .nowMs = QDateTime::currentMSecsSinceEpoch()
    });
    enrichedSnapshot.identity = identity;
    updateUnknownOccupant(transition, enrichedSnapshot);
    enrichedSnapshot.unknownOccupant = m_unknownOccupant;
    enrichedSnapshot.roomState = transition.currentState;
    enrichedSnapshot.roomReasonCode = transition.reasonCode;
    m_latestSnapshot = enrichedSnapshot;
    {
        QMutexLocker locker(&sharedSnapshotMutex());
        sharedSnapshot() = enrichedSnapshot;
    }
    logTransition(transition);
    emit roomTransitionReady(transition);

    const bool identityAvailable = identity.has_value() && identity->available && !identity->stale;
    const SmartWelcomeDecision welcome = m_behaviorPolicy.evaluateWelcome({
        .transition = transition,
        .welcomeEnabled = m_config.welcomeEnabled,
        .welcomeCooldownEnabled = m_config.welcomeCooldownEnabled,
        .unknownOccupantBlocksWelcomeEnabled = m_config.unknownOccupantBlocksWelcomeEnabled,
        .sensorOnlyWelcomeEnabled = identityAvailable ? false : m_config.sensorOnlyWelcomeEnabled,
        .welcomeCooldownMinutes = m_config.welcomeCooldownMinutes,
        .lastWelcomeAtMs = m_lastWelcomeAtMs,
        .nowMs = transition.occurredAtMs
    });
    logWelcomeDecision(welcome, transition);
    if (welcome.allowed) {
        m_lastWelcomeAtMs = welcome.nextLastWelcomeAtMs;
        SmartWelcomeDecision spokenWelcome = welcome;
        spokenWelcome.message = welcomeMessageForDecision(welcome);
        spokenWelcome.mentionUnknownOccupant = welcome.personal
            && m_unknownOccupant.hasEvent
            && !m_unknownOccupant.acknowledged
            && m_config.unknownOccupantSpokenAlertsEnabled;
        if (spokenWelcome.mentionUnknownOccupant) {
            m_unknownOccupant.acknowledged = true;
            m_latestSnapshot.unknownOccupant = m_unknownOccupant;
            QMutexLocker locker(&sharedSnapshotMutex());
            sharedSnapshot().unknownOccupant = m_unknownOccupant;
        }
        emit welcomeRequested(spokenWelcome.message, spokenWelcome);
    }
}

void SmartHomeRuntime::reconfigureIdentityAdapter()
{
    if (m_config.identityMode == QStringLiteral("desktop_ble_beacon")) {
        if (m_identityAdapter == nullptr) {
            m_identityAdapter = new DesktopBleIdentityAdapter(
                m_config,
                [this](const QString &message) {
                    if (m_loggingService) {
                        m_loggingService->infoFor(QStringLiteral("tools_mcp"), message);
                    }
                },
                this);
        } else {
            m_identityAdapter->setConfig(m_config);
        }
        if (m_config.enabled) {
            m_identityAdapter->start();
        }
        return;
    }

    if (m_identityAdapter) {
        m_identityAdapter->stop();
        m_identityAdapter->deleteLater();
        m_identityAdapter = nullptr;
    }
}

void SmartHomeRuntime::updateUnknownOccupant(const SmartRoomTransition &transition, const SmartHomeSnapshot &snapshot)
{
    const qint64 nowMs = transition.occurredAtMs > 0 ? transition.occurredAtMs : QDateTime::currentMSecsSinceEpoch();
    const qint64 nowUtcMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    if (transition.currentState == SmartRoomOccupancyState::UNKNOWN_OCCUPANT_IN_ROOM) {
        if (!m_unknownOccupant.active) {
            m_unknownOccupant.active = true;
            m_unknownOccupant.hasEvent = true;
            m_unknownOccupant.firstDetectedAtUtcMs = nowUtcMs;
            m_unknownOccupant.lastSeenAtUtcMs = nowUtcMs;
            m_unknownOccupant.acknowledged = false;
            m_unknownOccupant.sourcePresenceEntityId = snapshot.presence.sensorId;
            m_unknownOccupant.reasonCode = QStringLiteral("room_occupied_while_user_away");
            logUnknownOccupant(QStringLiteral("started"), m_unknownOccupant);
        } else {
            m_unknownOccupant.lastSeenAtUtcMs = nowUtcMs;
        }
        return;
    }

    if (!m_unknownOccupant.active) {
        return;
    }

    const int graceMinutes = std::clamp(m_config.roomAbsenceGraceMinutes, 0, 30);
    const qint64 lastSeenLocalMs = m_unknownOccupant.lastSeenAtUtcMs > 0
        ? QDateTime::fromMSecsSinceEpoch(m_unknownOccupant.lastSeenAtUtcMs, Qt::UTC).toLocalTime().toMSecsSinceEpoch()
        : nowMs;
    const qint64 clearGapMs = std::max<qint64>(0, nowMs - lastSeenLocalMs);
    if (snapshot.presence.available && !snapshot.presence.occupied && clearGapMs >= graceMinutes * 60 * 1000) {
        m_unknownOccupant.active = false;
        logUnknownOccupant(QStringLiteral("closed"), m_unknownOccupant);
    }
}

QString SmartHomeRuntime::welcomeMessageForDecision(const SmartWelcomeDecision &decision)
{
    SmartWelcomeDecision mutableDecision = decision;
    const bool hasUnacknowledgedUnknownEvent = m_unknownOccupant.hasEvent && !m_unknownOccupant.acknowledged;
    if (decision.personal && m_config.personalWelcomeEnabled) {
        if (hasUnacknowledgedUnknownEvent && m_config.unknownOccupantSpokenAlertsEnabled) {
            mutableDecision.mentionUnknownOccupant = true;
            return renderTemplate(m_config.personalWelcomeWithAlertTemplate,
                                  m_identityProfileService ? m_identityProfileService->userProfile().userName : QString(),
                                  m_unknownOccupant,
                                  decision.nextLastWelcomeAtMs);
        }
        return renderTemplate(m_config.personalWelcomeTemplate,
                              m_identityProfileService ? m_identityProfileService->userProfile().userName : QString(),
                              SmartRoomUnknownOccupantEvent{},
                              decision.nextLastWelcomeAtMs);
    }
    if (decision.sensorOnlyTest) {
        return QStringLiteral("Welcome back.");
    }
    if (decision.unknownOccupant) {
        return renderTemplate(m_config.unknownOccupantMessageTemplate,
                              QString(),
                              m_unknownOccupant,
                              decision.nextLastWelcomeAtMs);
    }
    return decision.message.trimmed().isEmpty() ? QStringLiteral("Welcome back.") : decision.message.trimmed();
}

void SmartHomeRuntime::logTransition(const SmartRoomTransition &transition) const
{
    if (!m_loggingService) {
        return;
    }
    m_loggingService->infoFor(
        QStringLiteral("tools_mcp"),
        QStringLiteral("[smart_room.transition] previous=%1 current=%2 reason=%3 roomOccupied=%4 phonePresent=%5 identityAvailable=%6 unknownOccupant=%7 sensorOffGapMs=%8 bleMissingMs=%9")
            .arg(smartRoomOccupancyStateName(transition.previousState),
                 smartRoomOccupancyStateName(transition.currentState),
                 transition.reasonCode,
                 transition.roomOccupied ? QStringLiteral("true") : QStringLiteral("false"),
                 transition.phonePresent ? QStringLiteral("true") : QStringLiteral("false"),
                 transition.identityAvailable ? QStringLiteral("true") : QStringLiteral("false"),
                 transition.unknownOccupant ? QStringLiteral("true") : QStringLiteral("false"),
                 QString::number(transition.sensorOffGapMs),
                 QString::number(transition.bleMissingMs)));
}

void SmartHomeRuntime::logWelcomeDecision(const SmartWelcomeDecision &decision, const SmartRoomTransition &transition) const
{
    if (!m_loggingService) {
        return;
    }
    m_loggingService->infoFor(
        QStringLiteral("tools_mcp"),
        QStringLiteral("[smart_room.welcome] allowed=%1 reason=%2 previous=%3 current=%4 welcomeEnabled=%5 cooldownEnabled=%6 cooldownMinutes=%7 sensorOnlyEnabled=%8 unknownOccupantBlocksWelcome=%9")
            .arg(decision.allowed ? QStringLiteral("true") : QStringLiteral("false"),
                 decision.reasonCode,
                 smartRoomOccupancyStateName(transition.previousState),
                 smartRoomOccupancyStateName(transition.currentState),
                 m_config.welcomeEnabled ? QStringLiteral("true") : QStringLiteral("false"),
                 m_config.welcomeCooldownEnabled ? QStringLiteral("true") : QStringLiteral("false"),
                 QString::number(m_config.welcomeCooldownMinutes),
                 m_config.sensorOnlyWelcomeEnabled ? QStringLiteral("true") : QStringLiteral("false"),
                 m_config.unknownOccupantBlocksWelcomeEnabled ? QStringLiteral("true") : QStringLiteral("false")));
}

void SmartHomeRuntime::logUnknownOccupant(const QString &action, const SmartRoomUnknownOccupantEvent &event) const
{
    if (!m_loggingService) {
        return;
    }
    m_loggingService->infoFor(
        QStringLiteral("tools_mcp"),
        QStringLiteral("[smart_room.unknown_occupant] action=%1 active=%2 firstDetectedUtcMs=%3 lastSeenUtcMs=%4 source=%5 reason=%6 acknowledged=%7")
            .arg(action,
                 event.active ? QStringLiteral("true") : QStringLiteral("false"),
                 QString::number(event.firstDetectedAtUtcMs),
                 QString::number(event.lastSeenAtUtcMs),
                 event.sourcePresenceEntityId,
                 event.reasonCode,
                 event.acknowledged ? QStringLiteral("true") : QStringLiteral("false")));
}

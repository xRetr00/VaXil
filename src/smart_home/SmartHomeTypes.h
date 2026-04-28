#pragma once

#include <optional>

#include <QDateTime>
#include <QJsonObject>
#include <QString>

enum class SmartRoomOccupancyState {
    UNKNOWN,
    AWAY,
    HOME_NOT_ROOM,
    IN_ROOM,
    ROOM_OCCUPIED_SENSOR_ONLY,
    UNKNOWN_OCCUPANT_IN_ROOM
};

struct SmartHomeConfig
{
    bool enabled = false;
    QString provider = QStringLiteral("home_assistant");
    QString homeAssistantBaseUrl;
    QString homeAssistantTokenEnvVar = QStringLiteral("VAXIL_HOME_ASSISTANT_TOKEN");
    QString homeAssistantIdentityEntityId;
    QString presenceEntityId;
    QString lightEntityId;
    QString identityMode = QStringLiteral("none");
    QString bleBeaconUuid;
    int pollIntervalMs = 5000;
    bool sensorOnlyWelcomeEnabled = false;
    int welcomeCooldownMinutes = 30;
    int roomAbsenceGraceMinutes = 6;
    int requestTimeoutMs = 5000;
    int identityMissingTimeoutMinutes = 10;
    int bleAwayTimeoutMinutes = 10;
    int bleScanIntervalMs = 1000;
    int bleRssiThreshold = -127;
    bool welcomeEnabled = true;
    bool welcomeCooldownEnabled = true;
    bool personalWelcomeEnabled = true;
    bool unknownOccupantBlocksWelcomeEnabled = true;
    bool unknownOccupantSpokenAlertsEnabled = true;
    QString personalWelcomeTemplate = QStringLiteral("Welcome back, {user_name}.");
    QString personalWelcomeWithAlertTemplate = QStringLiteral("Welcome back, {user_name}. Someone entered your room at {event_time}.");
    QString unknownOccupantMessageTemplate = QStringLiteral("There appears to be someone in the room.");
    QString unknownOccupantAlertResponseTemplate = QStringLiteral("Someone was detected in your room at {event_time}.");
};

struct SmartRoomStateMachineConfig
{
    bool sensorOnlyWelcomeEnabled = false;
    int roomAbsenceGraceMinutes = 6;
    int identityMissingTimeoutMinutes = 10;
    int bleAwayTimeoutMinutes = 10;
};

struct SmartPresenceSnapshot
{
    QString roomId = QStringLiteral("default");
    QString sensorId;
    bool available = false;
    bool occupied = false;
    qint64 observedAtMs = 0;
    QString source;
    QString rawState;
    bool stale = false;
};

struct SmartLightSnapshot
{
    QString lightId;
    bool available = false;
    bool on = false;
    int brightnessPercent = -1;
    QString colorName;
    QString scene;
    bool colorSupported = false;
    qint64 observedAtMs = 0;
    QString source;
    QString rawState;
    bool stale = false;
};

struct BleIdentitySnapshot
{
    QString identityId;
    QString entityId;
    bool available = false;
    bool present = false;
    int rssi = 0;
    qint64 observedAtMs = 0;
    QString source;
    QString rawState;
    bool stale = false;
};

struct SmartRoomUnknownOccupantEvent
{
    bool active = false;
    bool hasEvent = false;
    qint64 firstDetectedAtUtcMs = 0;
    qint64 lastSeenAtUtcMs = 0;
    bool acknowledged = true;
    QString sourcePresenceEntityId;
    QString reasonCode;
};

struct SmartHomeSnapshot
{
    SmartPresenceSnapshot presence;
    SmartLightSnapshot light;
    std::optional<BleIdentitySnapshot> identity;
    SmartRoomUnknownOccupantEvent unknownOccupant;
    SmartRoomOccupancyState roomState = SmartRoomOccupancyState::UNKNOWN;
    QString roomReasonCode;
    bool success = false;
    QString summary;
    QString detail;
    QString errorKind;
    int httpStatus = 0;
    qint64 latencyMs = 0;
};

struct SmartRoomStateMachineInput
{
    std::optional<SmartPresenceSnapshot> presence;
    std::optional<BleIdentitySnapshot> identity;
    SmartRoomStateMachineConfig config;
    qint64 nowMs = 0;
};

struct SmartRoomTransition
{
    SmartRoomOccupancyState previousState = SmartRoomOccupancyState::UNKNOWN;
    SmartRoomOccupancyState currentState = SmartRoomOccupancyState::UNKNOWN;
    QString reasonCode;
    bool phonePresent = false;
    bool roomOccupied = false;
    qint64 bleMissingMs = -1;
    qint64 sensorOffGapMs = -1;
    qint64 occurredAtMs = 0;
    bool unknownOccupant = false;
    bool returnedFromAway = false;
    bool identityAvailable = false;
};

struct SmartWelcomeDecision
{
    bool allowed = false;
    QString reasonCode;
    QString message;
    qint64 nextLastWelcomeAtMs = 0;
    bool personal = false;
    bool sensorOnlyTest = false;
    bool unknownOccupant = false;
    bool mentionUnknownOccupant = false;
};

struct SmartLightCommand
{
    QString action;
    int brightnessPercent = -1;
    QString colorName;
};

struct SmartLightCommandResult
{
    bool success = false;
    QString summary;
    QString detail;
    QString errorKind;
    int httpStatus = 0;
    qint64 latencyMs = 0;
};

QString smartRoomOccupancyStateName(SmartRoomOccupancyState state);

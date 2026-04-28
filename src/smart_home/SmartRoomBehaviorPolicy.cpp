#include "smart_home/SmartRoomBehaviorPolicy.h"

#include <algorithm>

#include <QDateTime>

namespace {
constexpr qint64 kMinuteMs = 60 * 1000;

bool isAwayOrUnknown(SmartRoomOccupancyState state)
{
    return state == SmartRoomOccupancyState::AWAY
        || state == SmartRoomOccupancyState::UNKNOWN;
}
}

SmartWelcomeDecision SmartRoomBehaviorPolicy::evaluateWelcome(const WelcomeInput &input) const
{
    const qint64 nowMs = input.nowMs > 0 ? input.nowMs : QDateTime::currentMSecsSinceEpoch();
    SmartWelcomeDecision decision;
    decision.nextLastWelcomeAtMs = input.lastWelcomeAtMs;

    if (!input.welcomeEnabled) {
        decision.reasonCode = QStringLiteral("welcome.blocked.disabled");
        return decision;
    }

    const int cooldownMinutes = std::clamp(input.welcomeCooldownMinutes, 0, 24 * 60);
    if (input.welcomeCooldownEnabled
        && cooldownMinutes > 0
        && input.lastWelcomeAtMs > 0
        && nowMs - input.lastWelcomeAtMs < cooldownMinutes * kMinuteMs) {
        decision.reasonCode = QStringLiteral("welcome.blocked.cooldown");
        return decision;
    }

    if (input.transition.currentState == SmartRoomOccupancyState::IN_ROOM) {
        if (input.transition.previousState == SmartRoomOccupancyState::AWAY) {
            decision.allowed = true;
            decision.reasonCode = QStringLiteral("welcome.allowed.identity_away_to_in_room");
            decision.message = QStringLiteral("Welcome back.");
            decision.nextLastWelcomeAtMs = nowMs;
            decision.personal = true;
            return decision;
        }
        if (input.transition.previousState == SmartRoomOccupancyState::HOME_NOT_ROOM
            && input.transition.returnedFromAway) {
            decision.allowed = true;
            decision.reasonCode = QStringLiteral("welcome.allowed.identity_returned_home_then_in_room");
            decision.message = QStringLiteral("Welcome back.");
            decision.nextLastWelcomeAtMs = nowMs;
            decision.personal = true;
            return decision;
        }
        if (!input.welcomeCooldownEnabled) {
            decision.allowed = true;
            decision.reasonCode = QStringLiteral("welcome.allowed.in_room_cooldown_disabled");
            decision.message = QStringLiteral("Welcome back.");
            decision.nextLastWelcomeAtMs = nowMs;
            decision.personal = true;
            return decision;
        }
        decision.reasonCode = QStringLiteral("welcome.blocked.not_away_to_in_room");
        return decision;
    }

    if (input.transition.currentState == SmartRoomOccupancyState::UNKNOWN_OCCUPANT_IN_ROOM) {
        if (input.unknownOccupantBlocksWelcomeEnabled) {
            decision.reasonCode = QStringLiteral("welcome.blocked.unknown_occupant");
            return decision;
        }
        decision.allowed = true;
        decision.reasonCode = QStringLiteral("welcome.allowed.unknown_occupant_override");
        decision.message = QStringLiteral("There appears to be someone in the room.");
        decision.nextLastWelcomeAtMs = nowMs;
        decision.unknownOccupant = true;
        return decision;
    }

    if (input.transition.currentState == SmartRoomOccupancyState::ROOM_OCCUPIED_SENSOR_ONLY) {
        if (!input.sensorOnlyWelcomeEnabled) {
            decision.reasonCode = QStringLiteral("welcome.blocked.sensor_only_disabled");
            return decision;
        }
        if (isAwayOrUnknown(input.transition.previousState)) {
            decision.allowed = true;
            decision.reasonCode = QStringLiteral("welcome.allowed.sensor_only_test");
            decision.message = QStringLiteral("Welcome back.");
            decision.nextLastWelcomeAtMs = nowMs;
            decision.sensorOnlyTest = true;
            return decision;
        }
        decision.reasonCode = QStringLiteral("welcome.blocked.sensor_only_not_entry");
        return decision;
    }

    decision.reasonCode = QStringLiteral("welcome.blocked.no_entry_transition");
    return decision;
}

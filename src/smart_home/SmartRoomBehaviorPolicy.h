#pragma once

#include "smart_home/SmartHomeTypes.h"

class SmartRoomBehaviorPolicy
{
public:
    struct WelcomeInput
    {
        SmartRoomTransition transition;
        bool welcomeEnabled = true;
        bool welcomeCooldownEnabled = true;
        bool unknownOccupantBlocksWelcomeEnabled = true;
        bool sensorOnlyWelcomeEnabled = false;
        int welcomeCooldownMinutes = 30;
        qint64 lastWelcomeAtMs = 0;
        qint64 nowMs = 0;
    };

    SmartWelcomeDecision evaluateWelcome(const WelcomeInput &input) const;
};

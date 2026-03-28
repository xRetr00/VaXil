#pragma once

#include <QString>

#include "core/AssistantTypes.h"

class VisionContextGate final
{
public:
    static bool shouldInject(const QString &input,
                             IntentType intent,
                             bool hasFreshSnapshot,
                             bool explicitVisionModeEnabled,
                             bool recentGestureAction);
    static bool isVisionRelevantQuery(const QString &input);
    static bool needsRawVisionDetails(const QString &input);
};

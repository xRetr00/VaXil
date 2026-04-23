#pragma once

#include <QString>

#include "companion/contracts/CooldownState.h"
#include "companion/contracts/FocusModeState.h"

struct ProactiveSpeechDecision
{
    bool shouldSpeak = false;
    QString reasonCode;
    bool cooldownActive = false;
};

class ProactiveSpeechPolicy
{
public:
    [[nodiscard]] static ProactiveSpeechDecision evaluate(const QString &message,
                                                          const QString &surfaceKind,
                                                          const FocusModeState &focusMode,
                                                          const CooldownState &cooldownState,
                                                          qint64 nowMs);
};

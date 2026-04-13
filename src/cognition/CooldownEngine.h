#pragma once

#include "companion/contracts/BehaviorDecision.h"
#include "companion/contracts/CompanionContextSnapshot.h"
#include "companion/contracts/CooldownState.h"
#include "companion/contracts/FocusModeState.h"

class CooldownEngine
{
public:
    struct Input
    {
        CompanionContextSnapshot context;
        CooldownState state;
        FocusModeState focusMode;
        QString priority = QStringLiteral("low");
        double confidence = 0.0;
        double novelty = 0.0;
        qint64 nowMs = 0;
    };

    [[nodiscard]] BehaviorDecision evaluate(const Input &input) const;
    [[nodiscard]] CooldownState advanceState(const Input &input, const BehaviorDecision &decision) const;
};

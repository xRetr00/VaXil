#pragma once

#include <QVariantMap>

#include "companion/contracts/ActionProposal.h"
#include "companion/contracts/BehaviorDecision.h"
#include "companion/contracts/FocusModeState.h"

class ProactiveSuggestionGate
{
public:
    struct Input
    {
        ActionProposal proposal;
        double proposalScore = 0.0;
        QVariantMap sourceMetadata;
        QVariantMap desktopContext;
        qint64 desktopContextAtMs = 0;
        FocusModeState focusMode;
        qint64 nowMs = 0;
    };

    [[nodiscard]] static BehaviorDecision evaluate(const Input &input);

private:
    [[nodiscard]] static bool hasFreshDesktopContext(const Input &input);
    [[nodiscard]] static bool isHighPriority(const QString &priority);
};

#pragma once

#include <QVariantMap>

#include "companion/contracts/BehaviorDecision.h"
#include "companion/contracts/CooldownState.h"
#include "companion/contracts/FocusModeState.h"
#include "core/AssistantTypes.h"

class ProactiveSurfaceGate
{
public:
    struct Input
    {
        BackgroundTaskResult result;
        QVariantMap desktopContext;
        qint64 desktopContextAtMs = 0;
        CooldownState cooldownState;
        FocusModeState focusMode;
        qint64 nowMs = 0;
    };

    [[nodiscard]] static BehaviorDecision evaluateTaskToast(const Input &input);
    [[nodiscard]] static BehaviorDecision evaluateCompletionFollowUp(const Input &input,
                                                                    bool hasArtifacts);

private:
    [[nodiscard]] static bool hasFreshDesktopContext(const Input &input);
    [[nodiscard]] static bool hasMeaningfulThreadShift(const Input &input);
    [[nodiscard]] static bool shouldSuppressForFocusedDesktopWork(const Input &input);
};

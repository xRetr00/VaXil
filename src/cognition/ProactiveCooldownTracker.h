#pragma once

#include <QString>
#include <QVariantMap>

#include "companion/contracts/CompanionContextSnapshot.h"
#include "companion/contracts/CooldownState.h"

struct ProactiveCooldownCommit
{
    CompanionContextSnapshot context;
    CooldownState nextState;
    double confidence = 0.0;
    double novelty = 0.0;
};

class ProactiveCooldownTracker
{
public:
    struct Input
    {
        CooldownState state;
        QVariantMap desktopContext;
        QString taskType;
        QString surfaceKind;
        QString priority = QStringLiteral("medium");
        qint64 nowMs = 0;
    };

    [[nodiscard]] static ProactiveCooldownCommit commitPresentedSurface(const Input &input);
};

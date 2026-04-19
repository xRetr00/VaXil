#pragma once

#include <QList>
#include <QString>
#include <QVariantMap>

#include "core/AssistantTypes.h"

enum class CurrentContextResolutionKind
{
    None,
    Task,
    Clarify,
    Blocked
};

struct CurrentContextResolution
{
    CurrentContextResolutionKind kind = CurrentContextResolutionKind::None;
    InputRouteDecision decision;
    QString message;
    QString status;
    QString reasonCode;
};

struct CurrentContextReferentInput
{
    QString userInput;
    QVariantMap desktopContext;
    qint64 desktopContextAtMs = 0;
    qint64 nowMs = 0;
    QString workspaceRoot;
};

class CurrentContextReferentResolver
{
public:
    [[nodiscard]] static CurrentContextResolution resolve(const CurrentContextReferentInput &input);
};

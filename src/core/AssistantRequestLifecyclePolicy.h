#pragma once

#include <QString>

#include "core/AssistantTypes.h"

class AssistantRequestLifecyclePolicy
{
public:
    [[nodiscard]] static int timeoutMs(RequestKind kind, int configuredTimeoutMs);
    [[nodiscard]] static int heartbeatDelayMs(int heartbeatIndex);
    [[nodiscard]] static QString heartbeatMessage(RequestKind kind, int heartbeatIndex);
    [[nodiscard]] static bool isProviderRateLimitError(const QString &errorText);
};

#pragma once

#include "companion/contracts/ConnectorEvent.h"
#include "core/AssistantTypes.h"

class ConnectorEventBuilder
{
public:
    [[nodiscard]] static ConnectorEvent fromBackgroundTaskResult(const BackgroundTaskResult &result);
};

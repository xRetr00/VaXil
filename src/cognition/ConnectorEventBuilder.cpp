#include "cognition/ConnectorEventBuilder.h"

#include <QUuid>

#include "cognition/ConnectorResultSignal.h"

ConnectorEvent ConnectorEventBuilder::fromBackgroundTaskResult(const BackgroundTaskResult &result)
{
    const ConnectorResultSignal signal = ConnectorResultSignalBuilder::fromBackgroundTaskResult(result);
    if (!signal.isValid()) {
        return {};
    }

    ConnectorEvent event;
    event.eventId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    event.sourceKind = signal.sourceKind;
    event.connectorKind = signal.connectorKind;
    event.taskType = signal.taskType;
    event.summary = signal.summary;
    event.taskKey = result.taskKey;
    event.taskId = result.taskId;
    event.itemCount = signal.itemCount;
    event.priority = result.success ? QStringLiteral("medium") : QStringLiteral("high");
    event.metadata = {
        {QStringLiteral("resultType"), result.type},
        {QStringLiteral("resultSuccess"), result.success},
        {QStringLiteral("resultSummary"), result.summary},
        {QStringLiteral("resultTitle"), result.title}
    };
    return event;
}

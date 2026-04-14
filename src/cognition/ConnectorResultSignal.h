#pragma once

#include <QJsonObject>
#include <QString>

#include "core/AssistantTypes.h"

struct ConnectorResultSignal
{
    QString sourceKind;
    QString taskType;
    QString summary;
    QString connectorKind;
    int itemCount = 0;

    [[nodiscard]] bool isValid() const
    {
        return !sourceKind.trimmed().isEmpty()
            && !taskType.trimmed().isEmpty()
            && !summary.trimmed().isEmpty();
    }
};

class ConnectorResultSignalBuilder
{
public:
    [[nodiscard]] static ConnectorResultSignal fromBackgroundTaskResult(const BackgroundTaskResult &result);

private:
    [[nodiscard]] static ConnectorResultSignal buildScheduleSignal(const BackgroundTaskResult &result,
                                                                   const QJsonObject &payload);
    [[nodiscard]] static ConnectorResultSignal buildInboxSignal(const BackgroundTaskResult &result,
                                                                const QJsonObject &payload);
    [[nodiscard]] static ConnectorResultSignal buildNoteSignal(const BackgroundTaskResult &result,
                                                               const QJsonObject &payload);
    [[nodiscard]] static ConnectorResultSignal buildResearchSignal(const BackgroundTaskResult &result,
                                                                   const QJsonObject &payload);
};
